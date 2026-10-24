# Troubleshooting

Symptoms first, then what to do. Most problems show up in `tools/validate.py` or the host simulator before they reach the MPC.

## Build problems

**`error: unable to find zig` / pip fails**
Install Zig manually (https://ziglang.org/download, any 0.13+ release) and put it on your PATH, or `pip install ziglang` into any Python and set `ZIG=/path/to/zig`.

**Compile errors in `mpc_plugin.h`**
You are probably calling a framework method that does not exist. Check the API summary in `framework/mpc_plugin.h`; the class is short. The usual suspects: `get(index)` returns the plain value (a float); `getBool`/`getInt` for switches and choices; `ctx.inputs` is null for an instrument with 0 inputs.

**`undefined symbol` at validate time or load time**
You used a library the MPC does not have, or an OS function that is not in glibc 2.31. Keep to the C/C++ standard library. If you need a third-party DSP library, compile its sources into the plugin (add them under `src/`).

**Validator: `GLIBC_2.4x` too new**
Your toolchain targets a newer glibc than the MPC has. Use the zig toolchain (which pins 2.31) or an older Debian/Ubuntu cross compiler.

**Validator: dynamic `libstdc++.so.6`**
Add `-static-libstdc++ -static-libgcc` (GCC) or use zig. A dynamic libstdc++ works on today's MPCs but ties you to their compiler version.

## Host simulator problems

**`FAIL: NaN/Inf in output`**
Division by zero, log of zero, or an unstable filter. Clamp parameters before use (`cutoff` never 0 Hz, resonance never 1.0), reset filters in `reset()`, and check feedback paths.

**`FAIL: state round trip`**
A parameter is not being restored. Usually the parameter's `id` string is duplicated or empty, or `loadExtraState` reads a different layout than `saveExtraState` writes.

**`WARN: instrument produced silence`**
No voice is triggered. Check that you handle `MidiEvent::isNoteOn()` and that the envelope actually opens. Check your output buffers are written, not just the inputs.

**`WARN: clipping`**
Scale down. Sum of voices, resonance boosts and drive stages add up. A master volume parameter defaulting to about -6 dB is normal.

**`WARN: avg CPU > 50%`** (measured on your computer)
The MPC is 10–40× slower than a laptop. Reduce oversampling, voice count, per-sample `std::pow`/`std::exp` calls (use tables or per-block coefficient updates), and allocation in `process` (there must be none).

**Crash (exit code 2)**
The report says which phase. Crashes in `prepare` are usually buffers sized from a block size of 0; crashes in `process` are usually out-of-range indices (voice arrays, delay lines, MIDI note numbers above 127, negative sample offsets).

## On the MPC

**The plugin does not appear in the browser**
- Is `VST2 Support` switched on under Preferences → Enhancements (Beta)? Both the master switch and the VST2 switch must be on.
- Is the folder inside a folder named exactly `VST2 Plugins` at the root of the drive?
- Is `plugin.json` valid JSON with a `name` and the right `file32`/`file64` for this MPC? A 32-bit MPC needs `file32`.
- Is `numOutputs` greater than 0?
- Run `tools/validate.py` on the built folder.

**"VST2 failed to load" message**
The library could not be loaded or returned nothing. Causes: wrong architecture file (validator checks this), a glibc symbol too new, a missing shared library dependency, a crash during `VSTPluginMain` (test natively first), or the drive's mount options preventing code execution. Try the MPC's internal drive instead of a USB stick.

**The plugin loads but the edit screen has no tabs**
`TUI.json` failed to parse. Common causes: a `version` number the MPC does not accept, a missing required key, a trailing comma. `tools/validate.py` checks the generated skins; for hand-written ones compare against `docs/ui-reference.md`.

**Controls are there but the knobs move the wrong parameter**
`Q-Links.json` indices are 0-based parameter indices and `Tab` numbers are 1-based. The parameter order is the order of `addParam` calls in your constructor; `build/params.json` shows the indices the MPC sees.

**Parameter names are wrong or the skin is missing after renaming the plugin**
The folder name, `plugin.json` and the strings in `PluginInfo` must all agree. Rebuild and copy the whole folder again; delete the old folder.

**Projects lose the plugin after an update**
The `uid` changed, or the parameter `id` strings changed. Both must stay stable for the life of the plugin.

**Glitches or dropouts**
Too much CPU, or a lock/allocation in `process`. Measure in the host simulator, then simplify. Multiple instances multiply the cost. Larger block sizes in the MPC's audio preferences reduce overhead.

**Audible click when loading or when a project opens**
The host nudges parameter 0 on every prepare. Make parameter 0 something harmless (master volume is common) and keep `parameterChanged` side-effect free, or smooth the parameter.

**The MPC freezes or restarts**
A crash in your plugin. Remove the folder from the drive to recover, then reproduce in the host simulator with `--block 32` and `--seconds 30`.
