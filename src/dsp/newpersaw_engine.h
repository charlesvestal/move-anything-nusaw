/*
 * newpersaw_engine.h - NewperSaw polyphonic synthesizer engine
 *
 * Detuned multi-voice sawtooth oscillator (7 voices: 1 center + 3 pairs)
 * with PolyBLEP anti-aliasing, analog pitch drift, stereo panning of
 * detuned pairs, sine sub oscillator (configurable octave offset),
 * post-mix 1-pole DC-blocking HPF, 2nd-order resonant lowpass filter
 * (TPT/SVF), ADSR amp and filter envelopes.
 *
 * 8-voice polyphony with oldest-note stealing.
 */

#ifndef NEWPERSAW_ENGINE_H
#define NEWPERSAW_ENGINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NSAW_MAX_VOICES 8       /* Polyphonic voices */
#define NSAW_SAMPLE_RATE 44100
#define NSAW_MAX_RENDER 256

/* Detuned oscillator configuration (compile-time)
 * M detuned pairs + 1 center = 2*M+1 total oscillator voices per poly voice */
#define NSAW_DETUNE_PAIRS 3
#define NSAW_OSC_VOICES (2 * NSAW_DETUNE_PAIRS + 1)  /* 7 */

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

/* Per-polyphonic-voice state */
typedef struct {
    int active;
    int note;
    float velocity;
    float freq;                         /* Base frequency in Hz */

    /* Multi-voice sawtooth phases (7 oscillators) */
    float phase[NSAW_OSC_VOICES];

    /* Analog pitch drift state per oscillator (lowpass-filtered noise) */
    float drift[NSAW_OSC_VOICES];

    /* Sub oscillator phase (sine, -1 octave) */
    float sub_phase;

    /* Post-mix DC-blocking HPF state (1-pole, stereo) */
    float hpf_x_prev_l, hpf_y_prev_l;
    float hpf_x_prev_r, hpf_y_prev_r;

    /* Envelopes */
    nsaw_envelope_t amp_env;
    nsaw_envelope_t filt_env;

    /* TPT/SVF lowpass filter state (2 integrators, stereo) */
    float ic1eq_l, ic2eq_l;
    float ic1eq_r, ic2eq_r;

    uint32_t age;                       /* For voice stealing */
} nsaw_voice_t;

/* Engine state */
typedef struct {
    float sample_rate;

    /* Polyphonic voices */
    nsaw_voice_t voices[NSAW_MAX_VOICES];
    uint32_t voice_counter;

    /* PRNG state for random phase and drift */
    uint32_t rng_state;

    /* Parameters (0.0 to 1.0 unless noted) */
    float cutoff;           /* Filter cutoff */
    float resonance;        /* Filter resonance */
    float detune;           /* Oscillator detune amount */
    float spread;           /* Detuned voice level (side-voice contribution) */
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
    float sub_level;        /* Sub oscillator level (sine) */
    int sub_octave;         /* Sub oscillator octave offset (-2, -1, 0) */

    int octave_transpose;   /* -3 to +3 octaves */

    /* Pitch bend state */
    float current_bend;     /* -1.0 to 1.0 */

    /* Smoothed parameter state (for zipper-free modulation) */
    float smooth_detune;
    float smooth_spread;

} nsaw_engine_t;

/* Initialize engine */
void nsaw_engine_init(nsaw_engine_t *engine);

/* MIDI handlers */
void nsaw_engine_note_on(nsaw_engine_t *engine, int note, float velocity);
void nsaw_engine_note_off(nsaw_engine_t *engine, int note);
void nsaw_engine_pitch_bend(nsaw_engine_t *engine, float bend);
void nsaw_engine_all_notes_off(nsaw_engine_t *engine);

/* Render audio block (stereo float output) */
void nsaw_engine_render(nsaw_engine_t *engine, float *out_left, float *out_right, int frames);

#ifdef __cplusplus
}
#endif

#endif /* NEWPERSAW_ENGINE_H */
