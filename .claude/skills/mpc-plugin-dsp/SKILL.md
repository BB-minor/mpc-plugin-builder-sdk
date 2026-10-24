---
name: mpc-plugin-dsp
description: Write or change the audio code (DSP, voices, MIDI handling, parameters, state) of an MPC plugin using the SDK framework. Use when adding features, fixing sound problems, adding parameters, changing synthesis or effect algorithms, or porting existing DSP to an MPC plugin.
---

# Working on plugin DSP

## Know the framework

Read `framework/mpc_plugin.h` (class `mpc::Plugin`) before editing. Key points:
- Declare parameters in the constructor with `addParam(mpc::floatParam/logParam/intParam/boolParam/choiceParam(...))`; keep the returned index in a member. Order = parameter index = what the MPC sees. Append new parameters at the end; never reorder or rename `id` strings.
- Read values in `process()` with `get(i)` (plain value), `getBool(i)`, `getInt(i)`. Smooth anything audible with `dsp::Smoother`.
- `process(mpc::ProcessContext& ctx)`: `ctx.inputs[ch][i]` (may be null for 0-input instruments), `ctx.outputs[ch][i]` (already zeroed), `ctx.numFrames`, `ctx.midi[0..numMidiEvents)` sorted by `sampleOffset`, `ctx.transport` (bpm, ppqPos, playing), `ctx.sampleRate`.
- `prepare(sampleRate, maxBlock)` allocates and sets coefficients; `reset()` clears state (called on resume and after state load).
- Extra state beyond parameters: `saveExtraState` / `loadExtraState` (byte vector). Built-in presets: `addProgram`.
- Custom value text: override `formatValue(index, plain, out)`.

## Patterns

- **Poly synth**: `dsp::VoiceAllocator<8>` + per-voice oscillator/filter/envelopes; render voices into the output, apply master gain. See `examples/SimpleSynth`.
- **Drum instrument**: map notes to voices; one-shot envelopes; multi-out by writing to `ctx.outputs[2..3]`. See `examples/DrumSynth`.
- **Effect**: read `ctx.inputs`, write `ctx.outputs`; dry/wet mix; tempo-sync via `ctx.transport.bpm` and `dsp/tempo.h`. See `examples/StereoDelay`.
- **Sequencer-instrument**: derive the step from `ctx.transport.ppqPos` when `playing`, trigger internal voices; never use wall-clock time.
- **MIDI**: handle `isNoteOn`, `isNoteOff`, `isAllNotesOff`, `isPitchBend` (`pitchBend()` −8192..8191), `isController` (CC 1 mod wheel, 64 sustain). Note-on with velocity 0 is a note-off (`isNoteOff()` already covers it).

## Realtime rules

No `new`/`malloc`/`std::vector::push_back`/`std::string` in `process`; no mutex; no `printf`; no file access. Treat `maxBlockSize` as a ceiling. Check array indices (note numbers 0–127, voice indices, delay reads). Avoid per-sample `std::pow`, `std::exp`, `std::sin` in inner loops; compute coefficients per block or use `dsp/math.h` approximations. Denormals are flushed by the framework.

## Performance budget

One instance should use well under 15% of a Cortex-A17 core at 128 frames / 44.1 kHz. Rough costs: a PolyBLEP oscillator ≈ cheap, a TPT SVF ≈ cheap, `std::sin` per sample ≈ expensive, 8 voices × (2 osc + filter + 2 env) ≈ fine, oversampling ≈ multiply everything.

## After changing code

Rebuild and run the host simulator (`mpc-plugin-test` skill). If you added parameters, update `ui.json` and the README's parameter list, and bump `version` in `plugin.json`.

More: `docs/dsp-cookbook.md`, `docs/host-contract.md`.
