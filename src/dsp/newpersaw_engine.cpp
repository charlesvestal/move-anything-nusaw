/*
 * newpersaw_engine.cpp - NewperSaw polyphonic synthesizer engine
 *
 * Sawtooth oscillator with PolyBLEP, TPT/SVF resonant lowpass filter,
 * ADSR amp and filter envelopes, 8-voice polyphony.
 */

#include "newpersaw_engine.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* =====================================================================
 * Helpers
 * ===================================================================== */

/* Convert 0.0-1.0 parameter to time in seconds (1ms to 10s, exponential) */
static inline float param_to_seconds(float p) {
    if (p < 0.001f) return 0.001f;
    return 0.001f * powf(10000.0f, p);
}

/* Convert MIDI note to frequency (A4 = 440Hz) */
static inline float note_to_freq(int note) {
    return 440.0f * powf(2.0f, (note - 69) / 12.0f);
}

/* PolyBLEP residual for anti-aliased sawtooth */
static inline float polyblep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    } else if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

/* =====================================================================
 * Engine init
 * ===================================================================== */

void nsaw_engine_init(nsaw_engine_t *engine) {
    memset(engine, 0, sizeof(nsaw_engine_t));
    engine->sample_rate = NSAW_SAMPLE_RATE;
    engine->voice_counter = 0;

    /* Default parameter values */
    engine->cutoff = 0.7f;
    engine->resonance = 0.0f;
    engine->f_amount = 0.5f;
    engine->attack = 0.01f;
    engine->decay = 0.3f;
    engine->sustain = 0.7f;
    engine->release = 0.2f;
    engine->f_attack = 0.01f;
    engine->f_decay = 0.3f;
    engine->f_sustain = 0.3f;
    engine->f_release = 0.2f;
    engine->volume = 0.7f;
    engine->vel_sens = 0.5f;
    engine->bend_range = 0.167f;  /* ~2 semitones */
    engine->octave_transpose = 0;
    engine->current_bend = 0.0f;

    for (int i = 0; i < NSAW_MAX_VOICES; i++) {
        engine->voices[i].active = 0;
        engine->voices[i].amp_env.stage = NSAW_ENV_OFF;
        engine->voices[i].filt_env.stage = NSAW_ENV_OFF;
    }
}

/* =====================================================================
 * Voice allocation
 * ===================================================================== */

static int find_free_voice(nsaw_engine_t *engine) {
    /* First: find an inactive voice */
    for (int i = 0; i < NSAW_MAX_VOICES; i++) {
        if (!engine->voices[i].active && engine->voices[i].amp_env.stage == NSAW_ENV_OFF) {
            return i;
        }
    }
    /* Second: find a releasing voice (steal the oldest) */
    int oldest_idx = -1;
    uint32_t oldest_age = 0xFFFFFFFF;
    for (int i = 0; i < NSAW_MAX_VOICES; i++) {
        if (engine->voices[i].amp_env.stage == NSAW_ENV_RELEASE && engine->voices[i].age < oldest_age) {
            oldest_age = engine->voices[i].age;
            oldest_idx = i;
        }
    }
    if (oldest_idx >= 0) return oldest_idx;

    /* Last resort: steal the oldest active voice */
    oldest_age = 0xFFFFFFFF;
    for (int i = 0; i < NSAW_MAX_VOICES; i++) {
        if (engine->voices[i].age < oldest_age) {
            oldest_age = engine->voices[i].age;
            oldest_idx = i;
        }
    }
    return oldest_idx >= 0 ? oldest_idx : 0;
}

/* =====================================================================
 * MIDI handlers
 * ===================================================================== */

void nsaw_engine_note_on(nsaw_engine_t *engine, int note, float velocity) {
    int vi = find_free_voice(engine);
    nsaw_voice_t *v = &engine->voices[vi];

    v->active = 1;
    v->note = note;
    v->velocity = velocity;
    v->freq = note_to_freq(note + engine->octave_transpose * 12);
    v->phase = 0.0f;
    v->age = engine->voice_counter++;

    /* Reset filter state for new voice */
    v->ic1eq = 0.0f;
    v->ic2eq = 0.0f;

    /* Trigger amp envelope */
    v->amp_env.stage = NSAW_ENV_ATTACK;
    /* Smooth retrigger: start from current level */

    /* Trigger filter envelope */
    v->filt_env.stage = NSAW_ENV_ATTACK;
}

void nsaw_engine_note_off(nsaw_engine_t *engine, int note) {
    for (int i = 0; i < NSAW_MAX_VOICES; i++) {
        nsaw_voice_t *v = &engine->voices[i];
        if (v->active && v->note == note && v->amp_env.stage != NSAW_ENV_RELEASE) {
            v->active = 0;
            v->amp_env.stage = NSAW_ENV_RELEASE;
            v->filt_env.stage = NSAW_ENV_RELEASE;
        }
    }
}

void nsaw_engine_pitch_bend(nsaw_engine_t *engine, float bend) {
    engine->current_bend = bend;
}

void nsaw_engine_all_notes_off(nsaw_engine_t *engine) {
    for (int i = 0; i < NSAW_MAX_VOICES; i++) {
        engine->voices[i].active = 0;
        engine->voices[i].amp_env.stage = NSAW_ENV_OFF;
        engine->voices[i].amp_env.level = 0.0f;
        engine->voices[i].filt_env.stage = NSAW_ENV_OFF;
        engine->voices[i].filt_env.level = 0.0f;
        engine->voices[i].ic1eq = 0.0f;
        engine->voices[i].ic2eq = 0.0f;
    }
}

/* =====================================================================
 * Envelope processing (per sample)
 * ===================================================================== */

static inline void process_envelope(nsaw_envelope_t *env,
                                     float attack_rate, float decay_coeff,
                                     float sustain_level, float release_coeff) {
    switch (env->stage) {
        case NSAW_ENV_ATTACK:
            env->level += attack_rate;
            if (env->level >= 1.0f) {
                env->level = 1.0f;
                env->stage = NSAW_ENV_DECAY;
            }
            break;
        case NSAW_ENV_DECAY:
            env->level = sustain_level + (env->level - sustain_level) * decay_coeff;
            if (env->level <= sustain_level + 0.0001f) {
                env->level = sustain_level;
                env->stage = NSAW_ENV_SUSTAIN;
            }
            break;
        case NSAW_ENV_SUSTAIN:
            env->level = sustain_level;
            break;
        case NSAW_ENV_RELEASE:
            env->level *= release_coeff;
            if (env->level < 0.0001f) {
                env->level = 0.0f;
                env->stage = NSAW_ENV_OFF;
            }
            break;
        case NSAW_ENV_OFF:
        default:
            env->level = 0.0f;
            break;
    }
}

/* =====================================================================
 * Render block
 * ===================================================================== */

void nsaw_engine_render(nsaw_engine_t *engine, float *output, int frames) {
    if (frames > NSAW_MAX_RENDER) frames = NSAW_MAX_RENDER;

    /* Precompute envelope coefficients from parameters */
    float sr = engine->sample_rate;

    /* Amp envelope coefficients */
    float amp_attack_time = param_to_seconds(engine->attack);
    float amp_decay_time = param_to_seconds(engine->decay);
    float amp_sustain = engine->sustain;
    float amp_release_time = param_to_seconds(engine->release);

    float amp_attack_rate = 1.0f / (amp_attack_time * sr);
    float amp_decay_coeff = expf(-4.0f / (amp_decay_time * sr));
    float amp_release_coeff = expf(-4.0f / (amp_release_time * sr));

    /* Filter envelope coefficients */
    float filt_attack_time = param_to_seconds(engine->f_attack);
    float filt_decay_time = param_to_seconds(engine->f_decay);
    float filt_sustain = engine->f_sustain;
    float filt_release_time = param_to_seconds(engine->f_release);

    float filt_attack_rate = 1.0f / (filt_attack_time * sr);
    float filt_decay_coeff = expf(-4.0f / (filt_decay_time * sr));
    float filt_release_coeff = expf(-4.0f / (filt_release_time * sr));

    /* Filter base cutoff: exponential mapping 20Hz to 20kHz */
    float base_cutoff_hz = 20.0f * powf(1000.0f, engine->cutoff);
    if (base_cutoff_hz > 20000.0f) base_cutoff_hz = 20000.0f;

    /* Resonance: map 0-1 to Q 0.5 to 20 (0.5 = Butterworth at ~0.707 with mild undershoot) */
    /* Q = 0.707 is true Butterworth; above that we get resonance peak */
    float q = 0.5f + engine->resonance * 19.5f;

    /* Filter envelope amount in octaves (0 = none, 1.0 = 8 octaves) */
    float f_env_octaves = engine->f_amount * 8.0f;

    /* Pitch bend ratio */
    float bend_semitones = engine->current_bend * engine->bend_range * 12.0f;
    float bend_ratio = powf(2.0f, bend_semitones / 12.0f);

    /* Master volume with headroom for polyphony */
    float master_vol = engine->volume * 0.3f;

    /* Clear output */
    memset(output, 0, frames * sizeof(float));

    /* Process each voice */
    for (int vi = 0; vi < NSAW_MAX_VOICES; vi++) {
        nsaw_voice_t *v = &engine->voices[vi];
        if (v->amp_env.stage == NSAW_ENV_OFF) continue;

        float freq = v->freq * bend_ratio;
        float dt = freq / sr;  /* Phase increment per sample */

        /* Velocity scaling */
        float vel_gain = 1.0f - engine->vel_sens + engine->vel_sens * v->velocity;

        for (int i = 0; i < frames; i++) {
            /* --- Sawtooth oscillator with PolyBLEP --- */
            float saw = 2.0f * v->phase - 1.0f;  /* Naive saw: -1 to +1 */
            saw -= polyblep(v->phase, dt);         /* Anti-aliasing */

            /* Advance phase */
            v->phase += dt;
            if (v->phase >= 1.0f) v->phase -= 1.0f;

            /* --- Process envelopes --- */
            process_envelope(&v->amp_env, amp_attack_rate, amp_decay_coeff,
                           amp_sustain, amp_release_coeff);
            process_envelope(&v->filt_env, filt_attack_rate, filt_decay_coeff,
                           filt_sustain, filt_release_coeff);

            /* --- Filter with envelope modulation --- */
            /* Modulated cutoff frequency */
            float mod_cutoff_hz = base_cutoff_hz * powf(2.0f, v->filt_env.level * f_env_octaves);
            if (mod_cutoff_hz > 20000.0f) mod_cutoff_hz = 20000.0f;
            if (mod_cutoff_hz < 20.0f) mod_cutoff_hz = 20.0f;

            /* TPT/SVF 2nd-order lowpass filter */
            float g = tanf((float)M_PI * mod_cutoff_hz / sr);
            float k = 1.0f / q;  /* Damping: k = 1/Q; Butterworth = sqrt(2) */
            float a1 = 1.0f / (1.0f + g * (g + k));
            float a2 = g * a1;
            float a3 = g * a2;

            float v3 = saw - v->ic2eq;
            float v1 = a1 * v->ic1eq + a2 * v3;
            float v2 = v->ic2eq + a2 * v->ic1eq + a3 * v3;
            v->ic1eq = 2.0f * v1 - v->ic1eq;
            v->ic2eq = 2.0f * v2 - v->ic2eq;

            float filtered = v2;  /* Lowpass output */

            /* --- Apply amp envelope and velocity --- */
            float sample = filtered * v->amp_env.level * vel_gain;

            output[i] += sample * master_vol;
        }
    }
}
