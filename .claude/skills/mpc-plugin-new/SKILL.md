---
name: mpc-plugin-new
description: Create a new MPC plugin project from a plain-language description (a synth, drum machine, sequencer-instrument or audio effect for Akai MPC standalone hardware). Use when the user asks to make, start, scaffold or build a new plugin, or describes an instrument or effect they want on their MPC.
---

# Create a new MPC plugin

You turn a description into a complete plugin project under `plugins/<Name>/` that builds, tests and packages with the SDK tools. Work in this order; do not skip the build and test steps.

## 1. Decide the shape (ask only if truly ambiguous)

From the description, settle:
- **Kind**: `instrument` (makes sound from MIDI notes: synths, drum machines, samplers, sequencer-instruments) or `effect` (processes the track's audio; receives NO MIDI). An arpeggiator or step sequencer must be an instrument that makes its own sound, because plugins cannot send MIDI to the MPC.
- **Name** (short, 1–3 words) and **manufacturer** (the user's name or brand; ask if unknown, default `"MPC Vibecoder"`).
- **Parameters**: 6–24 is typical. Each gets a short screen name (≤ 12 chars), a kind (float / int / bool / choice), range, unit, default. Group them into pages of ≤ 16 for the UI.
- **Channels**: instruments 0 in / 2 out (multi-out: 4, 6 or 8 outs as stereo pairs); effects 2 in / 2 out.

## 2. Scaffold

```
python tools/new_plugin.py "<Name>" --vendor "<Manufacturer>" --kind instrument|effect
```

This creates `plugins/<Name>/` with `plugin.json`, `src/<Class>.cpp`, `ui.json`, `README.md`. The printed `uid` is already written into both `plugin.json` and the C++ `MPC_FOURCC(...)`; they must stay equal.

## 3. Write the plugin

Edit `src/<Class>.cpp`. Read `framework/mpc_plugin.h` once (it is short) and follow `docs/dsp-cookbook.md`. Use the helpers in `framework/dsp/` (`oscillator.h`, `svf.h`, `adsr.h`, `delay_line.h`, `smoother.h`, `voice.h`, `lfo.h`, `biquad.h`, `tempo.h`, `math.h`) rather than writing filters and envelopes from scratch. Study `examples/SimpleSynth`, `examples/StereoDelay` and `examples/DrumSynth` for structure.

Rules that are not negotiable:
- No allocation, locks, logging or file I/O inside `process()`. Allocate in `prepare()`.
- Treat `maxBlockSize` as a maximum; `ctx.numFrames` varies between 32 and 512.
- Parameter 0 gets nudged by the host on every load: make it harmless (master volume) and keep `parameterChanged()` side-effect free.
- Clamp everything that feeds a filter or a divide. Reset state in `reset()`.
- Keep CPU modest: the MPC is far slower than a laptop. Per-block coefficient updates, per-sample arithmetic only.
- Parameter `id` strings and the `uid` are permanent once users have projects.

Then edit `ui.json` using the `mpc-plugin-ui` skill: give every control a kit widget (`knob`, `fader` or `switch`, see its heuristics) instead of the generic slider, and pick one theme. Then write `README.md` (what it does, the parameter list, tips).

## 4. Build, test, iterate

```
python tools/build.py plugins/<Name>
.sdk-tools/host_sim "plugins/<Name>/build/native/<slug>.dylib" test --seconds 3 --wav plugins/<Name>/build/test.wav
```
(`.so` on Linux, `.dll` on Windows.) Fix every compiler error and every `FAIL` in the report; take `WARN` lines seriously (silence, clipping, CPU). Rebuild until clean. See the `mpc-plugin-build` and `mpc-plugin-test` skills.

## 5. Hand over

Tell the user: the output folder path (`plugins/<Name>/build/<Manufacturer> - VST - <Name>/`), how to copy it to `VST2 Plugins` on a drive, that the MPC needs MPC OS 3.10.1 or later, and that VST plugins must be enabled on the device (Preferences → Enhancements (Beta) → Enable Enhancements and VST2 Support), and offer to package it (`mpc-plugin-package` skill). Summarise the parameters and pages you created.
