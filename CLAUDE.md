# CLAUDE.md

Instructions for Claude Code when working with this repository.

## Project Overview

NuSaw module for Move Anything - a polyphonic detuned supersaw synthesizer with sine sub oscillator, TPT/SVF resonant lowpass filter, ADSR amp and filter envelopes, Juno-style chorus, and stereo ping-pong delay.

## Architecture

```
src/
  dsp/
    nusaw_plugin.cpp  # Main plugin wrapper (V2 API)
    nusaw_engine.h    # Synth engine header
    nusaw_engine.cpp  # Synth engine implementation
    param_helper.h        # Shared parameter helper
  ui.js                   # JavaScript UI (uses shared sound_generator_ui)
  module.json             # Module metadata
```

## Key Implementation Details

### Plugin API

Implements Move Anything plugin_api_v2 (multi-instance):
- `create_instance`: Initializes synth engine, loads factory presets
- `destroy_instance`: Cleanup
- `on_midi`: Routes MIDI to synth engine (polyphonic, 8 voices)
- `set_param`: preset, octave_transpose, and 24 synth/fx parameters
- `get_param`: preset_name, preset_count, ui_hierarchy, chain_params, state
- `render_block`: Renders stereo synth output with chorus and delay effects

### Synth Engine

Polyphonic synthesizer (8 voices) with:
- 7 detuned sawtooth oscillators per voice (1 center + 3 pairs) with PolyBLEP anti-aliasing
- Exponential detune spacing (1:3:6 ratio), piecewise-linear detune curve
- Analog pitch drift (slow random walk per oscillator)
- Stereo panning of detuned pairs (constant-power pan law)
- Sine sub oscillator with configurable octave offset (-2, -1, 0)
- RMS-based gain normalization for consistent loudness
- Post-mix DC-blocking HPF
- TPT/SVF 2nd-order resonant lowpass filter
- ADSR amp and filter envelopes
- Velocity sensitivity, pitch bend support
- Oldest-note voice stealing

### Effects

- Juno-60 style chorus (dual triangle LFOs, stereo, equal-power crossfade)
- Stereo ping-pong delay (20ms-1s, tone filter, soft-saturated feedback)

### Parameters (24 total)

**Oscillator**: `detune`, `spread`, `sub_level`, `sub_octave` (-2 to 0)
**Filter**: `cutoff`, `resonance`, `f_amount`
**Amp Envelope**: `attack`, `decay`, `sustain`, `release`
**Filter Envelope**: `f_attack`, `f_decay`, `f_sustain`, `f_release`
**Chorus**: `chorus_mix`, `chorus_depth`
**Delay**: `delay_time`, `delay_fback`, `delay_mix`, `delay_tone`
**Performance**: `volume`, `vel_sens`, `bend_range`
**Other**: `octave_transpose` (plugin-level, -3 to +3 octaves)

### Factory Presets (27)

**Leads**: Festival Lead, Sunrise Lead, Razor Lead, Dream Lead, Trance Lead, Anthem
**Stabs**: Big Stab, Filtered Stab
**Pads**: Anthem Pad, Dark Pad, Glass Pad, Evolving Pad
**Strings**: Warm Strings, Bright Strings, Cinematic Strings
**Bass**: Trance Bass, Sub Bass, Growl Bass, Pluck Bass
**Special**: Init, Arp Pluck, Hardstyle, Solo Saw, Warm Lead, Acid, Hoover, Vapor

## Signal Chain Integration

Module declares `"chainable": true` and `"component_type": "sound_generator"` in module.json.

## Build Commands

```bash
./scripts/build.sh           # Build with Docker (ARM64 cross-compilation)
./scripts/install.sh          # Deploy to Move device
```

## License

MIT License
