# How plugins work on the MPC

Read this once; it is the mental model behind everything else in the SDK.

## The pieces

```
Your description ──► AI agent ──► C++ source (src/MyPlugin.cpp)   ─┐
                                   ui.json (pages of controls)     ─┤──► tools/build.py ──► "<Manufacturer> - VST - <Name>/" ──► VST2 Plugins/ on a drive ──► MPC
                                   plugin.json (name, id, kind)    ─┘
```

1. **The plugin is a shared library** (`.so`) that follows the VST 2.4 plugin binary interface. The MPC loads it with the same kind of host that desktop DAWs use. The SDK gives you a header-only framework so you never touch that interface: you write a class with parameters and a `process()` function.
2. **Two builds, one source.** MPCs come in two CPU families: 32-bit ARM (MPC X, Live, Live II, One, One+, Key 61, Key 37, Force) and 64-bit ARM (MPC Live III, MPC XL and newer). The build script cross-compiles both and puts them in the plugin folder; `plugin.json` tells each MPC which to load.
3. **The folder is the unit of installation.** `plugin.json` describes the plugin so the MPC can list it without loading it. The folder name encodes manufacturer and plugin name. Users drop the folder into `VST2 Plugins` on any drive and switch on the "VST2 Support" enhancement.
4. **The UI is data, not code.** The MPC has no plugin windows. It draws the plugin's screen from JSON "skins" in the plugin folder, binding on-screen controls and the 16 hardware knobs (Q-Links) to parameter indices. The SDK generates those files from a short page list; without them you get a generic but usable UI.
5. **Parameters are the interface.** Everything the user can touch is a parameter: a float 0–1 to the host, mapped by the framework to a real range, an integer, a switch or a list of choices, with a name and value text shown on screen. Parameters are also what gets automated and saved.
6. **State is a chunk.** The MPC saves an opaque blob from the plugin in the project. The framework writes every parameter by name plus anything extra you add, so projects survive plugin updates.

## What makes the MPC different from a desktop DAW

- Fixed 44.1 kHz; small blocks (default 128 frames, as low as 32).
- Modest CPUs. A 32-bit MPC is roughly a 2013 phone. Budget a few percent of one core per plugin instance.
- No plugin GUI; no mouse. Touch, 16 knobs, a data wheel, function keys.
- No crash protection. Your plugin runs inside the MPC application.
- Effects get no MIDI; MIDI out from plugins goes nowhere.
- The MPC loads your binary from a drive that may be removed; keep everything inside the folder.

## The workflow

1. Describe the plugin to your AI agent (see the README for prompts). It scaffolds a project under `plugins/`.
2. Build: `python tools/build.py plugins/MyPlugin`. Fix compile errors until it passes.
3. Test on your computer: `.sdk-tools/host_sim build/native/<slug>.dylib test --wav out.wav` and listen.
4. Arrange the UI: edit `ui.json`, rebuild.
5. Copy the output folder to `VST2 Plugins` on a drive, try it on the MPC.
6. Package: `python tools/package.py plugins/MyPlugin` and share the zip.
