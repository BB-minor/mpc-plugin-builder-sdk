# AGENTS.md — instructions for AI coding agents

You are working inside the **MPC Plugin SDK**: a toolkit for building instrument and effect plugins that run on Akai MPC standalone hardware (MPC X, Live, Live II, One, One+, Key 61, Key 37, Force, Live III, XL). The person you are helping is probably not a C++ programmer. Your job is to take their description and deliver a plugin folder that works on their MPC.

## What a finished plugin is

A folder `"<Manufacturer> - VST - <Name>/"` containing `plugin.json`, `version.xml`, `VST/<slug>-arm32.so`, `VST/<slug>-arm64.so` and `Plugin Skins/` (touch UI). `tools/build.py` produces it from a project in `plugins/<Name>/` (`plugin.json`, `src/*.cpp`, `ui.json`, `README.md`). Users copy the folder into a `VST2 Plugins` folder on a drive. The MPC must run MPC OS 3.10.1 or later, and VST plugins must be enabled on the device (Preferences → Enhancements (Beta) → Enable Enhancements and VST2 Support). Always tell the user both of these when handing over a plugin.

## Skills

Use the skills in `.claude/skills/` (Claude Code) or `.agents/skills/` (Codex); they are the same files:

| Skill | When |
|---|---|
| `mpc-plugin-new` | Start a plugin from a description (scaffold → code → UI → build → test) |
| `mpc-plugin-dsp` | Write or change the audio code, parameters, MIDI, state |
| `mpc-plugin-ui` | Pages, knobs, layout, custom skins |
| `mpc-plugin-build` | Compile for native + arm32 + arm64, fix build/validation errors |
| `mpc-plugin-test` | Run the host simulator, interpret the report, render WAVs |
| `mpc-plugin-package` | Zip with install instructions for sharing |
| `mpc-plugin-deploy` | Install/list/update/remove/export plugins on an MPC over the network (`tools/mpc_remote.py`), and set the MPC up for it |
| `mpc-plugin-presets` | Built-in programs |
| `mpc-plugin-troubleshoot` | Problems on the MPC or with the tools |

## The workflow, every time

0. **Manufacturer name**: if the user has not given their name or brand in their prompt, ask for it before doing anything else (it becomes the plugin manufacturer). In the same message, show the plugin name you propose and let them change it. Never fall back to a generic default such as "MPC Vibecoder".
1. `python tools/new_plugin.py "<Name>" --vendor "<Manufacturer>" --kind instrument|effect`
2. Write `src/<Class>.cpp` against `framework/mpc_plugin.h` using `framework/dsp/*.h`; write `ui.json`; write `README.md`.
3. `python tools/build.py plugins/<Name>` until it is clean.
4. `.sdk-tools/host_sim plugins/<Name>/build/native/<slug>.<dylib|so|dll> test --wav plugins/<Name>/build/test.wav` until no FAIL and the WARNs are understood.
5. Tell the user where the output folder is and how to install it; offer `python tools/package.py plugins/<Name>`, and, if their MPC is on the same network, offer to install it for them with the `mpc-plugin-deploy` skill.

Never report a plugin as done without steps 3 and 4 passing.

Commands here say `python`; on macOS and some Linux systems only `python3` exists, so use that if `python` is not found.

## Hard rules

- **Realtime**: no allocation, locks, logging or I/O in `process()`. Block size is a maximum; frames per call vary (32–512). Clamp inputs to filters and divisions.
- **Identity is permanent**: `uid` (plugin.json and `MPC_FOURCC` in code must match), parameter `id` strings, and parameter order must not change once a user has projects. Append new parameters at the end.
- **Ask for the manufacturer name** when the prompt does not contain one, and show the proposed plugin name for confirmation. Do not invent a generic default.
- **Names must agree**: `plugin.json` `name`/`manufacturer` == `PluginInfo` `name`/`vendor` == folder name parts. The UI is found by those strings.
- **Effects get no MIDI; plugins cannot send MIDI to the MPC.** Sequencers and arpeggiators are instruments that make their own sound.
- **Parameter 0 is nudged by the host on every load.** Make it harmless (master volume) with no side effects.
- **CPU**: the MPC is a slow ARM device. Aim for a few percent of one core per instance; no per-sample transcendental functions in inner loops.
- **Nothing proprietary**: do not fetch, copy or depend on files from MPC firmware, Steinberg's VST SDK, or code with licences incompatible with the user's intent. Note the licence of any DSP you borrow in the project README.
- **No system modification**: never suggest rooting, firmware patches or editing MPC system files. The official route is the `VST2 Plugins` folder and the preference switch. The one remote route is `tools/mpc_remote.py` (skill `mpc-plugin-deploy`), which uses the MPC's official developer mode and only ever writes inside a drive's `VST2 Plugins` folder. Do not run other commands on the device over SSH, and do not hand-write SSH/scp commands to copy plugins.

## Where to read

| Need | File |
|---|---|
| Mental model | `docs/overview.md` |
| Folder, plugin.json, version.xml | `docs/plugin-folder-format.md` |
| What the MPC host does | `docs/host-contract.md` |
| Writing DSP | `docs/dsp-cookbook.md`, `framework/mpc_plugin.h`, `framework/dsp/` |
| Simple UI | `docs/ui-guide.md`, `schemas/ui.schema.json` |
| Knob / fader / switch graphics | `ui-kit/README.md`, `ui-kit/preview.png`, `tools/make_ui_kit.py` |
| Full UI format | `docs/ui-reference.md` |
| Toolchains and flags | `docs/building.md` |
| Testing and installing | `docs/install-and-test.md` |
| Presets | `docs/presets.md` |
| Problems | `docs/troubleshooting.md` |
| Legal, JUCE question, community | `docs/faq-and-legal.md` |
| Worked examples | `examples/SimpleSynth`, `examples/StereoDelay`, `examples/DrumSynth` |

## Style for generated plugins

- One `.cpp` per plugin unless it grows past ~800 lines; then split DSP into headers under `src/`.
- Comment the signal flow at the top of the file and each parameter's purpose. The user will read it.
- Short, unit-bearing parameter names and sensible defaults; a master volume parameter first; 3–8 built-in programs.
- A `ui.json` that uses the UI kit: `knob`, `fader` and `switch` widgets (heuristics in the `mpc-plugin-ui` skill), not pages of generic sliders. Arrange controls in blocks of four (a row or a column, one related set each) so the Q-Link focus rectangle encloses them cleanly.
- Keep the project README honest: what works, what is not implemented, CPU expectations.
