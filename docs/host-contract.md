# Host contract: what the MPC does with your plugin

The MPC hosts third-party plugins through a standard VST 2.4 host. This page lists what that host actually does, so you know which parts of the interface matter. If you use the SDK framework (`framework/mpc_plugin.h`) all of this is handled for you; the page explains the behaviour you will observe and the rules your DSP code must follow.

## Audio format

| Property | Value |
|---|---|
| Sample rate | 44100 Hz, fixed |
| Sample format | 32-bit float, non-interleaved, one buffer per channel |
| Block size announced to the plugin | at least 128 frames |
| Frames per process call | the user can choose 32, 64, 96, 128, 192, 256 or 512 in Preferences. **Treat the announced block size as a maximum and handle any smaller count.** |
| Instrument channels | 0 inputs (or 2, silenced) and 2 outputs by default; more outputs are allowed and are routable as stereo pairs |
| Effect channels | 2 inputs, 2 outputs. An effect declaring more inputs than the track provides is skipped. A mono output is copied to both sides. |

The MPC's own build uses flush-to-zero on 32-bit products but not on 64-bit ones. The framework flushes denormals around every process call on every CPU, so you do not have to think about it unless you bypass the framework.

## Threads

- `process` runs on a real-time audio thread. Never allocate, lock, log, read files or block there.
- Parameter changes made from the screen and knobs are delivered on the audio thread between process calls. Parameter changes from project load, preset load and automation can come from the UI thread **while process is running**. State save/load, parameter name and value-text queries, and lifecycle calls (`open`, `prepare`, `close`) happen on the UI thread, also potentially concurrent with process.
- The framework stores parameters as atomics and defers "state loaded" handling to the audio thread so your code sees a consistent picture. If you keep extra state (patterns, sample data), swap it in a thread-safe way; see `dsp-cookbook.md`.
- The plugin runs inside the MPC process with no sandbox. A crash in your plugin crashes the MPC. A busy loop starves the audio engine. Test with the host simulator before putting a build on hardware.

## Load sequence

What the host sends, in order, when the user adds the plugin to a track:

1. Loads the library and calls `VSTPluginMain`. The returned effect must have the right magic value, a non-null object pointer and `resvd2` zero.
2. Sample rate and block size, then `open`.
3. Bus queries: input and output pin properties, speaker arrangement.
4. "Can be automated" for every parameter (cached; only parameters that answer yes can be automated or appear in automation lanes), and whether the plugin handles bypass itself.
5. Sample rate and block size again, vendor string, and `open` **a second time**. Then program 0 is selected, the plugin category is read, program names are enumerated, the MIDI capability is queried, latency is read, and the effect name is read.
6. Saved state is restored if the project or a preset has some (see State).
7. Prepare to play: speaker arrangement, sample rate, block size, resume (`mainsChanged 1`), then start processing. On every prepare the host also nudges **parameter 0** to 1.0 or 0.0 and straight back to its previous value (a quirk that only happens for plugins without a native editor, which includes every SDK plugin). Parameter 0 must tolerate this: do nothing expensive or audible in response to a parameter write.
8. The whole prepare step repeats whenever the sample rate or block size changes, and after a suspend/resume.
9. On removal: stop processing, suspend, `close`.

## Processing

- The host calls `processReplacing` with float buffers. The legacy accumulating `process` and the double-precision path are not used.
- **Processing is in place**: the input and output pointers refer to the same memory. Read a frame's input samples before writing that frame's output, and never read later input frames after writing earlier outputs. The framework clears only output channels that are not also inputs; the host simulator fails a plugin whose output vanishes in place.
- Before each block, instruments receive their MIDI events (`processEvents`), even when the list is empty.
- Transport information (tempo, position in beats, bar start, time signature, playing/recording/looping, loop points) is available to the plugin on every block. The framework exposes it as `ctx.transport`.

## MIDI

- **Instruments** receive note on/off, controllers, pitch bend, aftertouch, program change and sysex from the track, with sample-accurate offsets within the block.
- **Effects receive no MIDI at all.**
- MIDI output from a plugin is accepted by the host but then discarded. Do not design a plugin around sending MIDI to the MPC's tracks; it will not arrive.
- Program changes on the MIDI channel are delivered as MIDI; the host does not translate them into program selection.

## Parameters

- Every parameter is a float in the range 0.0 to 1.0 as far as the host is concerned. The framework maps those to real ranges, integers, booleans and choices for you.
- The host reads the parameter count once at load. Never change it at run time.
- Names and value text: the host asks the plugin for the name, the display text and the unit label of each parameter, and shows the name and display text on screen (the unit label is not appended). There is no hard length limit, but the screen is small: keep names to about 12 characters and value text to about 8.
- Names and display text are re-read when the plugin asks for a display update (the framework's `requestDisplayUpdate()`), which also rebuilds the plugin's UI page. Use it sparingly.
- The host does not use the extended parameter properties (steps, integer ranges); it does not call the text-to-value opcode either. Discrete steps on the MPC's data wheel come from `replacementStrings` in the skin (see `ui-guide.md`).
- Default values are not read from the plugin; the UI's "reset" uses 0 unless the skin says otherwise. Give your plugin sensible defaults in code and expose built-in programs.

## Programs (built-in presets)

- The host selects program 0 after loading and reads every program name. If your plugin reports it cannot name programs by index, the host walks through them by selecting each one in turn, which is slow and audible, so always answer the indexed-name query (the framework does).
- The program number is saved in the project and restored on load.

## State

- If the plugin says it supports chunks (the framework does), the host saves the opaque chunk the plugin returns (bank form, index 0) in the project and in preset files, and restores it with the matching set-chunk call. Chunks up to 128 MB are accepted; keep yours small.
- Plugins without chunk support are also supported: the host saves every parameter value of every program, and restores by writing each parameter back.
- The framework's chunk stores every parameter by its stable `id` string plus whatever you add in `saveExtraState`, so reordering or adding parameters in a later version does not break old projects.

## Capability questions the host asks

The host asks the plugin whether it can `receiveVstMidiEvent` (instruments should say yes), `sendVstMidiEvent`, `bypass` (if yes the host sends bypass on/off to the plugin instead of bypassing it itself) and `MPE`. Everything else is left at "don't know".

## What the host tells the plugin about itself

Vendor and product string `MPC 3`, VST version 2400. The host answers yes to sending and receiving MIDI events and time info, idle, window sizing and shell category queries.

## UI

The MPC never opens a plugin's own graphical editor. Leave the "has editor" flag clear (the framework does). The on-screen UI comes from the JSON skins in the plugin folder, or from the default skin.

## Things to avoid

- Depending on the current working directory. It is set to the plugin folder during creation only. If you need to load files, locate them relative to your own library's path (`dladdr`), or embed them.
- Spawning threads that touch audio state without synchronisation.
- Using more than a few MB of stack: audio threads on some products have a 1 MB stack. Put big buffers on the heap, allocated in `prepare`.
- Large allocations at run time: the MPC locks its memory and checks free memory before loading a plugin.
