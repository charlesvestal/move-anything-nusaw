# CLAUDE.md

Instructions for Claude Code when working with this repository.

## Project Overview

NewperSaw module for Move Anything - a polyphonic sawtooth synthesizer with Butterworth resonant lowpass filter, ADSR amp and filter envelopes.

## Architecture

```
src/
  dsp/
    newpersaw_plugin.cpp  # Main plugin wrapper (V2 API)
    newpersaw_engine.h    # Synth engine header
    newpersaw_engine.cpp  # Synth engine implementation
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
- `set_param`: preset, octave_transpose, and 14 synth parameters
- `get_param`: preset_name, preset_count, ui_hierarchy, chain_params, state
- `render_block`: Renders synth output (mono to stereo)

### Synth Engine

Polyphonic synthesizer (8 voices) with:
- Sawtooth oscillator with PolyBLEP anti-aliasing
- TPT/SVF 2nd-order resonant lowpass filter (Butterworth at Q=0.707)
- Amplitude ADSR envelope
- Filter ADSR envelope with amount control
- Velocity sensitivity
- Pitch bend support
- Oldest-note voice stealing

### Parameters (14 total)

**Filter**: `cutoff`, `resonance`, `f_amount` (filter envelope amount)
**Amp Envelope**: `attack`, `decay`, `sustain`, `release`
**Filter Envelope**: `f_attack`, `f_decay`, `f_sustain`, `f_release`
**Performance**: `volume`, `vel_sens`, `bend_range`
**Other**: `octave_transpose` (plugin-level, -3 to +3 octaves)

### Factory Presets (8)

Init, Pluck, Pad, Bass, Lead, Sweep, Strings, Acid

## Signal Chain Integration

Module declares `"chainable": true` and `"component_type": "sound_generator"` in module.json.

## Build Commands

```bash
./scripts/build.sh           # Build with Docker (ARM64 cross-compilation)
./scripts/install.sh          # Deploy to Move device
```

## License

MIT License
