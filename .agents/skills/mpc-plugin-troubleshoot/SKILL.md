---
name: mpc-plugin-troubleshoot
description: Diagnose an MPC plugin that does not appear, fails to load, shows no UI tabs, crashes the MPC, sounds wrong, glitches, or loses settings. Use when the user reports a problem on the MPC hardware or with the build/test tools.
---

# Troubleshooting checklist

Work top-down; stop when the cause is found. Full list in `docs/troubleshooting.md`.

1. **Not listed on the MPC** → MPC OS 3.10.1 or later (Preferences → Info)? Older firmware ignores the folder. Preferences → Enhancements (Beta): both *Enable Enhancements* and *VST2 Support* on? Folder inside `VST2 Plugins` at the drive root? `plugin.json` valid, `numOutputs > 0`, correct `file32`/`file64` for that MPC's architecture? Run `python tools/validate.py plugins/<Name>`.
2. **"VST2 failed to load"** → wrong architecture, glibc too new, extra library dependency, crash in `VSTPluginMain`. Validator + `host_sim ... test --verbose`. Try the internal drive instead of USB.
3. **Loads, no tabs on the edit screen** → `TUI.json` failed to parse (version numbers, missing keys, trailing commas). Validate; regenerate from `ui.json` if hand edits broke it.
4. **Knobs move the wrong thing** → `Q-Links.json` uses 0-based parameter indices and 1-based `Tab`; compare with `build/params.json`.
5. **Default UI shown instead of the skin** → folder name, `plugin.json` name/manufacturer and `PluginInfo` name/vendor disagree.
6. **No sound / wrong sound** → host simulator with `--wav`; check MIDI handling, envelopes, gain, filter stability.
7. **Glitches / dropouts** → CPU or a lock/allocation in `process`. Simplify; raise the MPC's block size in audio preferences to confirm.
8. **Crash / freeze** → remove the folder to recover; reproduce in the simulator with `--block 32 --seconds 30`; look for out-of-range indices and uninitialised state.
9. **Settings lost after update** → `uid` or parameter `id` strings changed. Restore the originals.
10. **Click on load** → parameter 0 is nudged by the host; make it harmless and smoothed.

When reporting back, say what was checked, what was found, and what changed.
