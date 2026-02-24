/*
 * NewperSaw DSP Plugin for Move Anything
 *
 * Polyphonic sawtooth synthesizer with Butterworth resonant lowpass filter,
 * ADSR amp and filter envelopes.
 *
 * V2 API - instance-based for multi-instance support
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Include plugin API */
extern "C" {
#include <stdint.h>

#define MOVE_PLUGIN_API_VERSION 1
#define MOVE_SAMPLE_RATE 44100
#define MOVE_FRAMES_PER_BLOCK 128
#define MOVE_MIDI_SOURCE_INTERNAL 0
#define MOVE_MIDI_SOURCE_EXTERNAL 2

typedef struct host_api_v1 {
    uint32_t api_version;
    int sample_rate;
    int frames_per_block;
    uint8_t *mapped_memory;
    int audio_out_offset;
    int audio_in_offset;
    void (*log)(const char *msg);
    int (*midi_send_internal)(const uint8_t *msg, int len);
    int (*midi_send_external)(const uint8_t *msg, int len);
} host_api_v1_t;

#define MOVE_PLUGIN_API_VERSION_2 2

typedef struct plugin_api_v2 {
    uint32_t api_version;
    void* (*create_instance)(const char *module_dir, const char *json_defaults);
    void (*destroy_instance)(void *instance);
    void (*on_midi)(void *instance, const uint8_t *msg, int len, int source);
    void (*set_param)(void *instance, const char *key, const char *val);
    int (*get_param)(void *instance, const char *key, char *buf, int buf_len);
    int (*get_error)(void *instance, char *buf, int buf_len);
    void (*render_block)(void *instance, int16_t *out_interleaved_lr, int frames);
} plugin_api_v2_t;

typedef plugin_api_v2_t* (*move_plugin_init_v2_fn)(const host_api_v1_t *host);
#define MOVE_PLUGIN_INIT_V2_SYMBOL "move_plugin_init_v2"

/* Engine */
#include "newpersaw_engine.h"
}

/* Include param helper */
#include "param_helper.h"

/* Host API reference */
static const host_api_v1_t *g_host = NULL;

static void plugin_log(const char *msg) {
    if (g_host && g_host->log) {
        char buf[256];
        snprintf(buf, sizeof(buf), "[nsaw] %s", msg);
        g_host->log(buf);
    }
}

/* =====================================================================
 * Parameter definitions
 * ===================================================================== */

enum {
    P_CUTOFF = 0,
    P_RESONANCE,
    P_F_AMOUNT,
    P_ATTACK,
    P_DECAY,
    P_SUSTAIN,
    P_RELEASE,
    P_F_ATTACK,
    P_F_DECAY,
    P_F_SUSTAIN,
    P_F_RELEASE,
    P_VOLUME,
    P_VEL_SENS,
    P_BEND_RANGE,
    P_COUNT
};

static const param_def_t g_shadow_params[] = {
    {"cutoff",      "Cutoff",       PARAM_TYPE_FLOAT, P_CUTOFF,     0.0f, 1.0f},
    {"resonance",   "Resonance",    PARAM_TYPE_FLOAT, P_RESONANCE,  0.0f, 1.0f},
    {"f_amount",    "Filt Env Amt", PARAM_TYPE_FLOAT, P_F_AMOUNT,   0.0f, 1.0f},
    {"attack",      "Attack",       PARAM_TYPE_FLOAT, P_ATTACK,     0.0f, 1.0f},
    {"decay",       "Decay",        PARAM_TYPE_FLOAT, P_DECAY,      0.0f, 1.0f},
    {"sustain",     "Sustain",      PARAM_TYPE_FLOAT, P_SUSTAIN,    0.0f, 1.0f},
    {"release",     "Release",      PARAM_TYPE_FLOAT, P_RELEASE,    0.0f, 1.0f},
    {"f_attack",    "F Attack",     PARAM_TYPE_FLOAT, P_F_ATTACK,   0.0f, 1.0f},
    {"f_decay",     "F Decay",      PARAM_TYPE_FLOAT, P_F_DECAY,    0.0f, 1.0f},
    {"f_sustain",   "F Sustain",    PARAM_TYPE_FLOAT, P_F_SUSTAIN,  0.0f, 1.0f},
    {"f_release",   "F Release",    PARAM_TYPE_FLOAT, P_F_RELEASE,  0.0f, 1.0f},
    {"volume",      "Volume",       PARAM_TYPE_FLOAT, P_VOLUME,     0.0f, 1.0f},
    {"vel_sens",    "Vel Sens",     PARAM_TYPE_FLOAT, P_VEL_SENS,   0.0f, 1.0f},
    {"bend_range",  "Bend Range",   PARAM_TYPE_FLOAT, P_BEND_RANGE, 0.0f, 1.0f},
};

/* =====================================================================
 * Preset system
 * ===================================================================== */

#define MAX_PRESETS 32

struct NsawPreset {
    char name[32];
    float params[P_COUNT];
};

/*
 * Factory presets
 * Parameter order: cutoff, resonance, f_amount,
 *                  attack, decay, sustain, release,
 *                  f_attack, f_decay, f_sustain, f_release,
 *                  volume, vel_sens, bend_range
 */
static const NsawPreset g_factory_presets[] = {
    /* 0: Init - bright open saw */
    {"Init", {
        0.7f, 0.0f, 0.5f,
        0.01f, 0.3f, 0.7f, 0.2f,
        0.01f, 0.3f, 0.3f, 0.2f,
        0.7f, 0.5f, 0.167f
    }},
    /* 1: Pluck - short decay, filter sweep */
    {"Pluck", {
        0.3f, 0.2f, 0.8f,
        0.0f, 0.2f, 0.0f, 0.15f,
        0.0f, 0.15f, 0.0f, 0.1f,
        0.8f, 0.7f, 0.167f
    }},
    /* 2: Pad - slow attack, warm */
    {"Pad", {
        0.45f, 0.1f, 0.4f,
        0.6f, 0.4f, 0.8f, 0.5f,
        0.5f, 0.4f, 0.5f, 0.4f,
        0.7f, 0.3f, 0.167f
    }},
    /* 3: Bass - low cutoff, punchy */
    {"Bass", {
        0.3f, 0.4f, 0.6f,
        0.0f, 0.2f, 0.7f, 0.1f,
        0.0f, 0.15f, 0.0f, 0.1f,
        0.8f, 0.5f, 0.167f
    }},
    /* 4: Lead - bright, resonant */
    {"Lead", {
        0.6f, 0.5f, 0.6f,
        0.01f, 0.25f, 0.6f, 0.2f,
        0.01f, 0.3f, 0.2f, 0.2f,
        0.7f, 0.6f, 0.167f
    }},
    /* 5: Sweep - full filter envelope */
    {"Sweep", {
        0.1f, 0.6f, 1.0f,
        0.01f, 0.3f, 0.7f, 0.3f,
        0.3f, 0.5f, 0.0f, 0.3f,
        0.7f, 0.5f, 0.167f
    }},
    /* 6: Strings - slow attack pad */
    {"Strings", {
        0.5f, 0.0f, 0.3f,
        0.7f, 0.3f, 0.9f, 0.6f,
        0.6f, 0.3f, 0.6f, 0.5f,
        0.6f, 0.3f, 0.167f
    }},
    /* 7: Acid - high resonance, low cutoff */
    {"Acid", {
        0.2f, 0.85f, 0.9f,
        0.0f, 0.15f, 0.0f, 0.1f,
        0.0f, 0.12f, 0.0f, 0.08f,
        0.8f, 0.8f, 0.167f
    }},
};

#define FACTORY_PRESET_COUNT (int)(sizeof(g_factory_presets) / sizeof(g_factory_presets[0]))

/* =====================================================================
 * Instance
 * ===================================================================== */

typedef struct {
    char module_dir[256];
    nsaw_engine_t engine;
    int current_preset;
    int preset_count;
    char preset_name[64];
    float params[P_COUNT];
    NsawPreset presets[MAX_PRESETS];
    int octave_transpose;
} nsaw_instance_t;

static void apply_params_to_engine(nsaw_instance_t *inst);
static void apply_preset(nsaw_instance_t *inst, int preset_idx);

/* =====================================================================
 * Parameter application
 * ===================================================================== */

static void apply_params_to_engine(nsaw_instance_t *inst) {
    nsaw_engine_t *e = &inst->engine;

    e->cutoff      = inst->params[P_CUTOFF];
    e->resonance   = inst->params[P_RESONANCE];
    e->f_amount    = inst->params[P_F_AMOUNT];
    e->attack      = inst->params[P_ATTACK];
    e->decay       = inst->params[P_DECAY];
    e->sustain     = inst->params[P_SUSTAIN];
    e->release     = inst->params[P_RELEASE];
    e->f_attack    = inst->params[P_F_ATTACK];
    e->f_decay     = inst->params[P_F_DECAY];
    e->f_sustain   = inst->params[P_F_SUSTAIN];
    e->f_release   = inst->params[P_F_RELEASE];
    e->volume      = inst->params[P_VOLUME];
    e->vel_sens    = inst->params[P_VEL_SENS];
    e->bend_range  = inst->params[P_BEND_RANGE];
}

static void apply_preset(nsaw_instance_t *inst, int preset_idx) {
    if (preset_idx < 0 || preset_idx >= inst->preset_count) return;

    NsawPreset *p = &inst->presets[preset_idx];
    memcpy(inst->params, p->params, sizeof(float) * P_COUNT);
    snprintf(inst->preset_name, sizeof(inst->preset_name), "%s", p->name);
    inst->current_preset = preset_idx;

    apply_params_to_engine(inst);
}

/* =====================================================================
 * JSON helper
 * ===================================================================== */

static int json_get_number(const char *json, const char *key, float *out) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *pos = strstr(json, search);
    if (!pos) return -1;
    pos += strlen(search);
    while (*pos == ' ') pos++;
    *out = (float)atof(pos);
    return 0;
}

/* =====================================================================
 * Plugin API v2
 * ===================================================================== */

static void* v2_create_instance(const char *module_dir, const char *json_defaults) {
    (void)json_defaults;

    nsaw_instance_t *inst = (nsaw_instance_t*)calloc(1, sizeof(nsaw_instance_t));
    if (!inst) return NULL;

    strncpy(inst->module_dir, module_dir, sizeof(inst->module_dir) - 1);

    /* Initialize engine */
    nsaw_engine_init(&inst->engine);

    /* Load factory presets */
    inst->preset_count = FACTORY_PRESET_COUNT;
    for (int i = 0; i < FACTORY_PRESET_COUNT; i++) {
        memcpy(&inst->presets[i], &g_factory_presets[i], sizeof(NsawPreset));
    }

    /* Apply first preset */
    apply_preset(inst, 0);

    plugin_log("NewperSaw v2: Instance created");
    return inst;
}

static void v2_destroy_instance(void *instance) {
    nsaw_instance_t *inst = (nsaw_instance_t*)instance;
    if (!inst) return;
    free(inst);
    plugin_log("NewperSaw v2: Instance destroyed");
}

static void v2_on_midi(void *instance, const uint8_t *msg, int len, int source) {
    nsaw_instance_t *inst = (nsaw_instance_t*)instance;
    if (!inst || len < 2) return;
    (void)source;

    uint8_t status = msg[0] & 0xF0;
    uint8_t data1 = msg[1];
    uint8_t data2 = (len > 2) ? msg[2] : 0;

    switch (status) {
        case 0x90:
            if (data2 > 0) {
                nsaw_engine_note_on(&inst->engine, data1, data2 / 127.0f);
            } else {
                nsaw_engine_note_off(&inst->engine, data1);
            }
            break;
        case 0x80:
            nsaw_engine_note_off(&inst->engine, data1);
            break;
        case 0xB0:
            switch (data1) {
                case 123: /* All notes off */
                    nsaw_engine_all_notes_off(&inst->engine);
                    break;
            }
            break;
        case 0xE0: { /* Pitch bend */
            int bend = ((data2 << 7) | data1) - 8192;
            nsaw_engine_pitch_bend(&inst->engine, bend / 8192.0f);
            break;
        }
    }
}

static void v2_set_param(void *instance, const char *key, const char *val) {
    nsaw_instance_t *inst = (nsaw_instance_t*)instance;
    if (!inst) return;

    /* State restore from patch save */
    if (strcmp(key, "state") == 0) {
        float fval;

        if (json_get_number(val, "preset", &fval) == 0) {
            int idx = (int)fval;
            if (idx >= 0 && idx < inst->preset_count) {
                apply_preset(inst, idx);
            }
        }

        if (json_get_number(val, "octave_transpose", &fval) == 0) {
            inst->octave_transpose = (int)fval;
            inst->engine.octave_transpose = inst->octave_transpose;
        }

        /* Restore individual params */
        for (int i = 0; i < (int)PARAM_DEF_COUNT(g_shadow_params); i++) {
            if (json_get_number(val, g_shadow_params[i].key, &fval) == 0) {
                if (fval < g_shadow_params[i].min_val) fval = g_shadow_params[i].min_val;
                if (fval > g_shadow_params[i].max_val) fval = g_shadow_params[i].max_val;
                inst->params[g_shadow_params[i].index] = fval;
            }
        }
        apply_params_to_engine(inst);
        return;
    }

    if (strcmp(key, "preset") == 0) {
        int idx = atoi(val);
        if (idx >= 0 && idx < inst->preset_count) {
            apply_preset(inst, idx);
        }
    }
    else if (strcmp(key, "octave_transpose") == 0) {
        inst->octave_transpose = atoi(val);
        if (inst->octave_transpose < -3) inst->octave_transpose = -3;
        if (inst->octave_transpose > 3) inst->octave_transpose = 3;
        inst->engine.octave_transpose = inst->octave_transpose;
    }
    else if (strcmp(key, "all_notes_off") == 0) {
        nsaw_engine_all_notes_off(&inst->engine);
    }
    else {
        /* Named parameter access */
        for (int i = 0; i < (int)PARAM_DEF_COUNT(g_shadow_params); i++) {
            if (strcmp(key, g_shadow_params[i].key) == 0) {
                float fval = (float)atof(val);
                if (fval < g_shadow_params[i].min_val) fval = g_shadow_params[i].min_val;
                if (fval > g_shadow_params[i].max_val) fval = g_shadow_params[i].max_val;
                inst->params[g_shadow_params[i].index] = fval;
                apply_params_to_engine(inst);
                return;
            }
        }
    }
}

static int v2_get_param(void *instance, const char *key, char *buf, int buf_len) {
    nsaw_instance_t *inst = (nsaw_instance_t*)instance;
    if (!inst) return -1;

    if (strcmp(key, "preset") == 0) {
        return snprintf(buf, buf_len, "%d", inst->current_preset);
    }
    if (strcmp(key, "preset_count") == 0) {
        return snprintf(buf, buf_len, "%d", inst->preset_count);
    }
    if (strcmp(key, "preset_name") == 0) {
        return snprintf(buf, buf_len, "%s", inst->preset_name);
    }
    if (strcmp(key, "name") == 0) {
        return snprintf(buf, buf_len, "NewperSaw");
    }
    if (strcmp(key, "octave_transpose") == 0) {
        return snprintf(buf, buf_len, "%d", inst->octave_transpose);
    }

    /* Named parameter access via helper */
    int result = param_helper_get(g_shadow_params, PARAM_DEF_COUNT(g_shadow_params),
                                  inst->params, key, buf, buf_len);
    if (result >= 0) return result;

    /* UI hierarchy for shadow parameter editor */
    if (strcmp(key, "ui_hierarchy") == 0) {
        const char *hierarchy = "{"
            "\"modes\":null,"
            "\"levels\":{"
                "\"root\":{"
                    "\"list_param\":\"preset\","
                    "\"count_param\":\"preset_count\","
                    "\"name_param\":\"preset_name\","
                    "\"children\":\"main\","
                    "\"knobs\":[\"cutoff\",\"resonance\",\"f_amount\",\"attack\",\"decay\",\"sustain\",\"release\",\"volume\"],"
                    "\"params\":[]"
                "},"
                "\"main\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"cutoff\",\"resonance\",\"f_amount\",\"attack\",\"decay\",\"sustain\",\"release\",\"volume\"],"
                    "\"params\":["
                        "{\"level\":\"filter\",\"label\":\"Filter\"},"
                        "{\"level\":\"filt_env\",\"label\":\"Filter Env\"},"
                        "{\"level\":\"amp_env\",\"label\":\"Amp Env\"},"
                        "{\"level\":\"performance\",\"label\":\"Performance\"}"
                    "]"
                "},"
                "\"filter\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"cutoff\",\"resonance\",\"f_amount\"],"
                    "\"params\":[\"cutoff\",\"resonance\",\"f_amount\"]"
                "},"
                "\"filt_env\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"f_attack\",\"f_decay\",\"f_sustain\",\"f_release\",\"f_amount\"],"
                    "\"params\":[\"f_attack\",\"f_decay\",\"f_sustain\",\"f_release\",\"f_amount\"]"
                "},"
                "\"amp_env\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"attack\",\"decay\",\"sustain\",\"release\"],"
                    "\"params\":[\"attack\",\"decay\",\"sustain\",\"release\"]"
                "},"
                "\"performance\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"volume\",\"vel_sens\",\"bend_range\",\"octave_transpose\"],"
                    "\"params\":[\"volume\",\"vel_sens\",\"bend_range\",\"octave_transpose\"]"
                "}"
            "}"
        "}";
        int len = strlen(hierarchy);
        if (len < buf_len) {
            strcpy(buf, hierarchy);
            return len;
        }
        return -1;
    }

    /* State serialization for patch save/load */
    if (strcmp(key, "state") == 0) {
        int offset = 0;
        offset += snprintf(buf + offset, buf_len - offset,
            "{\"preset\":%d,\"octave_transpose\":%d",
            inst->current_preset, inst->octave_transpose);

        for (int i = 0; i < (int)PARAM_DEF_COUNT(g_shadow_params); i++) {
            float val = inst->params[g_shadow_params[i].index];
            offset += snprintf(buf + offset, buf_len - offset,
                ",\"%s\":%.4f", g_shadow_params[i].key, val);
        }

        offset += snprintf(buf + offset, buf_len - offset, "}");
        return offset;
    }

    /* Chain params metadata */
    if (strcmp(key, "chain_params") == 0) {
        int offset = 0;
        offset += snprintf(buf + offset, buf_len - offset,
            "[{\"key\":\"preset\",\"name\":\"Preset\",\"type\":\"int\",\"min\":0,\"max\":9999},"
            "{\"key\":\"octave_transpose\",\"name\":\"Octave\",\"type\":\"int\",\"min\":-3,\"max\":3}");

        for (int i = 0; i < (int)PARAM_DEF_COUNT(g_shadow_params) && offset < buf_len - 100; i++) {
            offset += snprintf(buf + offset, buf_len - offset,
                ",{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"%s\",\"min\":%g,\"max\":%g}",
                g_shadow_params[i].key,
                g_shadow_params[i].name[0] ? g_shadow_params[i].name : g_shadow_params[i].key,
                g_shadow_params[i].type == PARAM_TYPE_INT ? "int" : "float",
                g_shadow_params[i].min_val,
                g_shadow_params[i].max_val);
        }
        offset += snprintf(buf + offset, buf_len - offset, "]");
        return offset;
    }

    return -1;
}

static void v2_render_block(void *instance, int16_t *out_interleaved_lr, int frames) {
    nsaw_instance_t *inst = (nsaw_instance_t*)instance;
    if (!inst) {
        memset(out_interleaved_lr, 0, frames * 4);
        return;
    }

    /* Render mono audio */
    float mono_buf[256];
    if (frames > 256) frames = 256;

    nsaw_engine_render(&inst->engine, mono_buf, frames);

    /* Convert to stereo int16 with soft clipping */
    for (int i = 0; i < frames; i++) {
        float sample = mono_buf[i];

        /* Soft clip via tanh */
        if (sample > 0.9f || sample < -0.9f) {
            sample = tanhf(sample);
        }

        int32_t s = (int32_t)(sample * 32767.0f);
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;

        out_interleaved_lr[i * 2]     = (int16_t)s;
        out_interleaved_lr[i * 2 + 1] = (int16_t)s;
    }
}

static int v2_get_error(void *instance, char *buf, int buf_len) {
    (void)instance;
    (void)buf;
    (void)buf_len;
    return 0;
}

/* v2 API table */
static plugin_api_v2_t g_plugin_api_v2;

extern "C" plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t *host) {
    g_host = host;

    memset(&g_plugin_api_v2, 0, sizeof(g_plugin_api_v2));
    g_plugin_api_v2.api_version = MOVE_PLUGIN_API_VERSION_2;
    g_plugin_api_v2.create_instance = v2_create_instance;
    g_plugin_api_v2.destroy_instance = v2_destroy_instance;
    g_plugin_api_v2.on_midi = v2_on_midi;
    g_plugin_api_v2.set_param = v2_set_param;
    g_plugin_api_v2.get_param = v2_get_param;
    g_plugin_api_v2.get_error = v2_get_error;
    g_plugin_api_v2.render_block = v2_render_block;

    return &g_plugin_api_v2;
}
