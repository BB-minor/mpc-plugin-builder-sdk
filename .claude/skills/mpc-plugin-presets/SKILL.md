---
name: mpc-plugin-presets
description: Add factory presets (built-in programs) to an MPC plugin, or explain MPC preset (.xpl) files. Use when the user asks for presets, patches, programs, an init patch or a sound bank for their plugin.
---

# Presets

The reliable mechanism is built-in programs declared in code. They appear in the MPC's preset selector and are stored by number in projects.

1. Get the parameter order: `.sdk-tools/host_sim build/native/<slug>.dylib dump` (or read the constructor's `addParam` order).
2. In the constructor, after all `addParam` calls, add one `addProgram("Name", { plain values in parameter order })` per preset. Values are plain (Hz, dB, 0/1 for bools, choice index for choices), not normalised. Make program 0 a sensible "Init".
3. Keep lists complete and in order; a comment per value helps future edits. 8–16 presets covering distinct uses is a good factory set.
4. Rebuild, then verify with `dump` (it lists program names) and `test`.

`.xpl` preset files are the MPC's user-saved preset format; they are tied to the plugin's path on the device and are not browsed from inside a plugin folder, so the SDK does not generate them. Details and the XML layout: `docs/presets.md`.
