# MPC Plugin SDK

Build instrument and effect plugins for Akai MPC standalone hardware from a plain-language description, with an AI coding agent, in C++ you never have to read if you do not want to.

```
"Make a tempo-synced tape delay with a feedback filter and a ping-pong switch."
        │
        ▼  (Claude Code / Codex, using this SDK's skills)
plugins/TapeDelay/              ──►  python tools/build.py plugins/TapeDelay
├── plugin.json                       │
├── src/TapeDelay.cpp                 ▼
├── ui.json                     plugins/TapeDelay/build/"Acme - VST - Tape Delay"/
└── README.md                   ├── plugin.json  version.xml
                                ├── VST/tape-delay-arm32.so   VST/tape-delay-arm64.so
                                └── Plugin Skins/TUI.json  Q-Links.json
                                        │
                                        ▼  copy into  <USB or SD>/VST2 Plugins/
                                     MPC: Preferences → Enhancements (Beta) → VST2 Support
```

Runs on MPC X, X SE, Live, Live II, One, One+, Key 61, Key 37, Force (32-bit) and MPC Live III, MPC XL and later (64-bit).

**Requires MPC OS 3.10.1 or later, with VST plugins enabled on the device**: Preferences → Enhancements (Beta) → turn on *Enable Enhancements* and *VST2 Support*. Earlier firmware ignores the `VST2 Plugins` folder entirely, and with the setting off the plugins stay hidden.

## Start here

- **[GETTING_STARTED.md](GETTING_STARTED.md)**: clone, build an example, put it on your MPC, then describe your own plugin.
- Point Claude Code or Codex at this folder. `AGENTS.md` and the skills in `.claude/skills/` / `.agents/skills/` teach the agent the whole workflow.

## Example prompts

Paste one of these into your agent with the SDK folder open:

> Create an MPC instrument called "Dune Bass": a monophonic bass synth with saw and square oscillators, sub oscillator, a resonant low-pass filter with envelope amount, glide, and a drive stage. Put oscillator controls on page one and filter/envelope on page two. Add 8 presets ranging from clean sub to aggressive acid.

> Build an MPC effect called "Shimmer": a reverb with an octave-up pitch-shifted feedback path, pre-delay, damping, size, mix. Keep CPU low enough for two instances on an MPC Live II.

> Make an MPC drum synth with 8 voices on notes 36–43 (kick, snare, rim, clap, closed hat, open hat, tom, perc). Each voice has tune, decay and level. Kick and snare go to the main stereo output, everything else to a second stereo output. One page per pair of drums.

> I want a generative melody instrument: while the MPC plays, it picks notes from a selectable scale on every 8th or 16th note (density control), with octave range and rest probability, played through a simple FM pluck with decay and brightness controls.

> Port the DSP from this open-source filter I pasted into an MPC effect with cutoff, resonance, drive and mode (LP/BP/HP) controls. Credit the original author and licence in the README.

> My plugin "Dune Bass" makes a click when I load a project. Find out why and fix it.

## What is in the box

| Folder | Contents |
|---|---|
| `framework/` | `mpc_plugin.h`: header-only plugin framework (parameters, MIDI, transport, state, programs). `mpc_vst2_abi.h`: clean-room description of the VST 2.4 binary interface. `dsp/`: oscillators, filters, envelopes, delay lines, LFOs, voice allocation, smoothing, tempo helpers. |
| `tools/` | `build.py` (one-command cross-compile for both MPC CPU families + native), `host-sim/` (a command-line host that behaves like the MPC for testing on your computer), `validate.py`, `gen_ui.py`, `package.py`, `mpc_remote.py` (install and manage plugins on an MPC over the network), `new_plugin.py`, `make_ui_kit.py` (film-strip knobs, faders, switches), `make_knob.py`, `docker/`. |
| `examples/` | `SimpleSynth` (8-voice subtractive synth with presets), `StereoDelay` (tempo-synced effect), `DrumSynth` (multi-output drum instrument). Complete projects you can build and copy. |
| `templates/` | Minimal instrument and effect projects used by `new_plugin.py`. |
| `docs/` | The specifications: plugin folder and `plugin.json`, the host contract, the scripted UI JSON system (`ui-reference.md`), building, testing, presets, troubleshooting, FAQ and legal notes. |
| `ui-kit/` | Stylish unipolar/bipolar film-strip knobs and faders plus matching switches, in several colour themes. `ui.json` uses them through the `knob`, `fader` and `switch` widgets. |
| `schemas/` | JSON Schemas for `plugin.json` and `ui.json`. |
| `.claude/skills/`, `.agents/skills/` | Agent skills: new, dsp, ui, build, test, package, presets, troubleshoot. |

## Requirements

- Python 3.9 or newer. Everything else (the Zig cross compiler) is fetched on first build, or use your own GNU cross compilers / Docker (`docs/building.md`).
- macOS, Windows or Linux.
- An MPC running MPC OS 3.10.1 or later, with VST2 Support enabled in Preferences → Enhancements (Beta), plus a USB drive or SD card.

## How it works, briefly

An MPC plugin is a Linux shared library following the VST 2.4 binary interface, in a folder with a `plugin.json` manifest and optional JSON "skins" for the touch screen and the 16 hardware knobs. The MPC loads it in-process; there are no plugin windows, so the UI is data. The SDK framework hides the binary interface behind a small C++ class: declare parameters, implement `process()`, done. `docs/overview.md` has the full picture.

## Safety

Third-party plugins run inside the MPC application without protection. A bug can freeze or restart the MPC. Test with the host simulator first, save your work often, and remove a misbehaving plugin's folder from the drive to recover. inMusic does not test or support third-party plugins.

## Licence

MIT. Contains no Steinberg VST SDK code and no MPC firmware files. "VST" is a trademark of Steinberg Media Technologies GmbH. See `LICENSE` and `docs/faq-and-legal.md`.
