# Presets

## Built-in programs (recommended)

Declare presets in code with `addProgram("Name", { ...plain values in parameter order... })`. The MPC lists them in the plugin's preset selector, remembers the chosen program number in the project, and your parameters snap to the stored values when one is chosen. This works on every product, needs no files, and cannot get out of sync with the plugin.

```cpp
// After addParam(...) calls, in the constructor:
addProgram("Init",      { 0.5f, 1000.0f, 0.2f, 0 });
addProgram("Deep Bass", { 0.9f,  220.0f, 0.6f, 1 });
```

Order matters: values are matched to parameters by index. Changing parameters later means updating the lists; the framework ignores extra values and leaves missing ones alone.

## Preset files (.xpl), optional

The MPC's own preset files are XML with the extension `.xpl`. Users create them by saving a preset from the plugin's edit screen; they are stored under the user's `Plugin Presets` folder in a subfolder named like the plugin folder. The MPC does not browse factory preset files inside a third-party plugin's folder, so shipping `.xpl` files with a plugin is of limited use; they are documented here for completeness and for sharing presets between users.

Structure:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<pluginstate>
  <version file="1" date="2026-10-24-12-00"/>
  <PLUGIN name="Wobble" format="VST" category="Synth" manufacturer="Acme" version="1.0.0"
          file="/path/to/VST2 Plugins/Acme - VST - Wobble/VST/wobble-arm32.so"
          uid="1094796898" isInstrument="1" numInputs="0" numOutputs="2"/>
  <preset>Deep Bass</preset>
  <state64>BASE64-ENCODED-STATE-CHUNK</state64>
</pluginstate>
```

- `PLUGIN` describes the plugin exactly as the MPC saw it, including the full path of the binary on the device and the decimal `uid`. The MPC matches presets to plugins by this description, so a preset made on one drive path may not match the same plugin installed elsewhere.
- `state64` is the plugin's state chunk (the same bytes the project stores), base64 encoded. For SDK plugins that chunk is the framework format: a header, each parameter's id and value, then any extra state.

Because the file is tied to a device path, the SDK does not generate `.xpl` files. Prefer built-in programs.
