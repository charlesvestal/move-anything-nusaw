/*
 * newpersaw_engine.h - NewperSaw polyphonic synthesizer engine
 *
 * Single sawtooth oscillator with PolyBLEP anti-aliasing,
 * 2nd-order resonant lowpass filter (TPT/SVF Butterworth at Q=0.707),
 * ADSR amp envelope, ADSR filter envelope.
 *
 * 8-voice polyphony with oldest-note stealing.
 */

#ifndef NEWPERSAW_ENGINE_H
#define NEWPERSAW_ENGINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NSAW_MAX_VOICES 8
#define NSAW_SAMPLE_RATE 44100
#define NSAW_MAX_RENDER 256

/* Envelope stages */
typedef enum {
    NSAW_ENV_OFF = 0,
    NSAW_ENV_ATTACK,
    NSAW_ENV_DECAY,
    NSAW_ENV_SUSTAIN,
    NSAW_ENV_RELEASE
} nsaw_env_stage_t;

/* Per-voice envelope state */
typedef struct {
    nsaw_env_stage_t stage;
    float level;
} nsaw_envelope_t;

/* Per-voice state */
typedef struct {
    int active;
    int note;
    float velocity;
    float phase;        /* 0.0 to 1.0 sawtooth phase accumulator */
    float freq;         /* Current frequency in Hz */

    nsaw_envelope_t amp_env;
    nsaw_envelope_t filt_env;

    /* TPT/SVF filter state (2 integrators) */
    float ic1eq;
    float ic2eq;

    uint32_t age;       /* For voice stealing (oldest = lowest age) */
} nsaw_voice_t;

/* Engine state */
typedef struct {
    float sample_rate;

    /* Voices */
    nsaw_voice_t voices[NSAW_MAX_VOICES];
    uint32_t voice_counter;

    /* Parameters (0.0 to 1.0 unless noted) */
    float cutoff;           /* Filter cutoff */
    float resonance;        /* Filter resonance */
    float f_amount;         /* Filter envelope amount */

    float attack;           /* Amp envelope attack time */
    float decay;            /* Amp envelope decay time */
    float sustain;          /* Amp envelope sustain level */
    float release;          /* Amp envelope release time */

    float f_attack;         /* Filter envelope attack time */
    float f_decay;          /* Filter envelope decay time */
    float f_sustain;        /* Filter envelope sustain level */
    float f_release;        /* Filter envelope release time */

    float volume;           /* Master volume */
    float vel_sens;         /* Velocity sensitivity */
    float bend_range;       /* Pitch bend range (maps to 0-12 semitones) */

    int octave_transpose;   /* -3 to +3 octaves */

    /* Pitch bend state */
    float current_bend;     /* -1.0 to 1.0 */

} nsaw_engine_t;

/* Initialize engine */
void nsaw_engine_init(nsaw_engine_t *engine);

/* MIDI handlers */
void nsaw_engine_note_on(nsaw_engine_t *engine, int note, float velocity);
void nsaw_engine_note_off(nsaw_engine_t *engine, int note);
void nsaw_engine_pitch_bend(nsaw_engine_t *engine, float bend);
void nsaw_engine_all_notes_off(nsaw_engine_t *engine);

/* Render audio block (mono float output) */
void nsaw_engine_render(nsaw_engine_t *engine, float *output, int frames);

#ifdef __cplusplus
}
#endif

#endif /* NEWPERSAW_ENGINE_H */
