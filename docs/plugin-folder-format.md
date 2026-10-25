# Plugin folder format

This is the exact on-disk format the MPC expects for a third-party plugin. The SDK's `tools/build.py` produces it for you; read this when you need to know *why* the folder looks the way it does, or when you build it by hand.

## The folder

Every plugin is one folder, named after the manufacturer and plugin name:

```
<Manufacturer> - VST - <Plugin Name>/
├── plugin.json              required: tells the MPC what the plugin is and where its binaries are
├── version.xml              optional: declares the UI format version the skins need
├── VST/
│   ├── <slug>-arm32.so      32-bit ARM build (MPC X, Live, Live II, One, One+, Key 61, Key 37, Force)
│   └── <slug>-arm64.so      64-bit ARM build (MPC Live III, MPC XL and later 64-bit products)
├── Content/                 optional: your own files (samples, wavetables, impulse responses, presets data)
└── Plugin Skins/            optional: the touch-screen UI and knob mapping
    ├── TUI.json
    ├── Q-Links.json
    └── *.png                images referenced by TUI.json
```

Rules:

- The folder name is `"<manufacturer> - VST - <name>"`, with the literal text ` - VST - ` in the middle. The two parts must equal the `manufacturer` and `name` strings in `plugin.json`, which must in turn equal the vendor and effect-name strings the binary reports to the host. The MPC finds the plugin's skins and `version.xml` by building this name from the strings the loaded plugin reports, so if they disagree the plugin still loads but gets the default UI.
- When the MPC builds the name it removes these characters: `" # @ , ; : < > * ^ | ? \ /`. Use the same rule when you name the folder (the SDK tools do). Spaces and UTF-8 are fine. Keep the whole name under 128 characters.
- If `manufacturer` is empty the folder is named `"VST - <name>"`. Always set a manufacturer.
- The binaries can live anywhere inside the folder (the `VST/` subfolder is a convention); `plugin.json` gives their paths. They cannot be outside the folder.
- Keep the folder self-contained. Anything the plugin needs at run time (samples, wavetables, impulse responses) goes inside it, in a `Content/` folder beside `Plugin Skins/`. The MPC does not look inside `Content/`; it is purely for your plugin's code.

## Content: loading your own files at run time

The plugin project keeps these files in `plugins/<Name>/Content/` (any subfolders); `tools/build.py` copies it verbatim to `Content/` in the output folder, and `package.py` / `mpc_remote.py` ship it with the plugin.

The VST2 host gives a plugin no path to its own folder, so the SDK provides one: `framework/mpc_resources.h` (included by `mpc_plugin.h`). The library asks the OS where its code was loaded from (`/proc/self/maps` on the MPC) and walks up to the folder holding `plugin.json`.

```cpp
std::string dir  = mpc::pluginFolder();               // ".../<Manufacturer> - VST - <Name>"
std::string path = mpc::contentPath("Samples/kick.wav");   // ".../Content/Samples/kick.wav"
std::vector<uint8_t> bytes;  mpc::readFile(path, bytes);   // raw file
dsp::WavData wav;  dsp::loadWav(path, wav);                // framework/dsp/wav.h: PCM 8/16/24/32 or float WAV -> float
```

Rules: load in the constructor or `prepare()` (never `process()`: no file I/O or allocation there), keep the data in members, always handle a missing file (fall back to a generated sound), and use `/` in paths. Under `host_sim` the native test library is found inside the project (`plugins/<Name>/build/native/` is inside the project folder that holds `plugin.json`), so `Content/` is read straight from the project; set the environment variable `MPC_PLUGIN_FOLDER` to point at any other folder. Mind memory: the MPC has limited RAM and every instance loads its own copy, so keep samples short or mono/16-bit, and do not load a large library per instance if a few MB will do.

## Where the folder goes on the MPC

Put the plugin folder inside a folder named exactly `VST2 Plugins` at the root of a drive:

```
<drive root>/VST2 Plugins/<Manufacturer> - VST - <Plugin Name>/
```

The drive can be a USB stick, an SD card, or the MPC's internal drive. The MPC watches the `VST2 Plugins` folder of every attached drive. Only the direct children of `VST2 Plugins` are examined; nothing is searched recursively, and loose files there are ignored.

Third-party plugins are loaded only by **MPC OS 3.10.1 or later**; earlier firmware ignores the `VST2 Plugins` folder. The user must also enable VST plugins on the device: **Preferences → Enhancements (Beta) → Enable Enhancements** and **VST2 Support**. With the setting off, plugins are hidden and projects that use them will not load them.

Plugins appear in the plugin browser under **External VST Plugins**. Instruments are used as plugin programs; effects are used as inserts.

## plugin.json

A JSON object. Unknown keys are ignored, so you may add `description`, `author`, `url` and `license` for packaging.

| Key | Required | Type | Meaning |
|---|---|---|---|
| `name` | yes | string | Plugin name. Must be non-empty and must equal the effect name the binary reports. |
| `manufacturer` | yes (strongly) | string | Manufacturer / vendor. Must equal the vendor string the binary reports. |
| `uid` | yes (strongly) | string or int | The plugin's unique id. Must equal the `uniqueId` the binary reports. Write it as a four-character code string such as `"AcWb"`. A decimal integer, a decimal string, or a `"0x…"` hex string (max 10 characters) are also accepted. If missing or invalid the MPC makes one up from the file path, which will not match the loaded plugin, and projects will fail to find it later. |
| `file32` | see note | string | Path, relative to the plugin folder, of the 32-bit ARM binary. |
| `file64` | see note | string | Path, relative to the plugin folder, of the 64-bit ARM binary. |
| `file` | see note | string | Single binary for both. Only used when neither `file32` nor `file64` is present. |
| `kind` | no | `"instrument"` or `"effect"` | Default is instrument. Case-insensitive. |
| `isInstrument` | no | bool | Overrides `kind` if present. |
| `numInputs` | no | int | Audio inputs. Default 0. |
| `numOutputs` | no | int | Audio outputs. Default 2. **Must be greater than 0** or the plugin never appears in the browser. |
| `version` | no | string | Free text, e.g. `"1.0.0"`. |
| `category` | no | string | Free text, e.g. `"Synth"`, `"Effect"`, `"Delay"`. |

Binary selection rules:

- If `file32` or `file64` is present, `file` is ignored. A 32-bit MPC uses only `file32`; a 64-bit MPC uses only `file64`. If the key for the current MPC is missing, the plugin is ignored on that MPC.
- The path must exist, must be a file, must be relative, and must resolve inside the plugin folder (`../x.so` and absolute paths are rejected). Subfolders are fine.
- Ship both builds. The SDK builds both from one source.

Example:

```json
{
  "name": "Wobble",
  "manufacturer": "Acme",
  "version": "1.0.0",
  "kind": "instrument",
  "uid": "AcWb",
  "category": "Synth",
  "numInputs": 0,
  "numOutputs": 2,
  "file32": "VST/wobble-arm32.so",
  "file64": "VST/wobble-arm64.so",
  "description": "A wobbly bass synth with a tempo-synced LFO."
}
```

Notes on `uid`: the four-character code is interpreted with the first character as the most significant byte, so `"AcWb"` equals `('A' << 24) | ('c' << 16) | ('W' << 8) | 'b'`. In SDK code this is `MPC_FOURCC('A','c','W','b')`. Choose something unlikely to clash with other plugins (two letters of your name, two of the plugin's).

The `kind`, `numInputs` and `numOutputs` values are used to list the plugin before it is ever loaded. After loading, the MPC reads the real values from the binary, so keep them consistent.

## The binaries

Each binary is a Linux ELF shared object that exports one function, `VSTPluginMain`, following the VST 2.4 plugin binary interface. The SDK's framework produces this for you. Requirements:

- 32-bit: ARM v7-A, hard-float ABI, NEON (`arm-linux-gnueabihf`). Targets Cortex-A17 class CPUs.
- 64-bit: AArch64 (`aarch64-linux-gnu`). Targets Cortex-A55/A76 class CPUs. Use conservative `armv8-a` code generation.
- glibc symbol versions no newer than 2.39. The SDK targets 2.31 for headroom.
- Link the C++ runtime statically, or do not use it. The SDK's default toolchain does this automatically. Depend on nothing beyond `libc`, `libm`, `libpthread` and `libdl`.
- Export nothing except `VSTPluginMain`.

See `building.md` for toolchains and flags, and `host-contract.md` for what the MPC does with the binary.

## version.xml

Optional. Lives at the root of the plugin folder.

```xml
<?xml version="1.0" encoding="utf-8"?>
<plugincontent version="1.0">
    <version>1.0.0.0</version>
    <uiversion>0.1.0.0</uiversion>
</plugincontent>
```

- `<version>` is informational (four dot-separated numbers).
- `<uiversion>` declares the minimum scripted-UI version the skins require. The MPC's scripted UI version is **0.1.0.0**. If `<uiversion>` is greater than what the MPC has, the MPC refuses to load the plugin and asks the user to update. **Never write `1`**: it is parsed as 1.0.0.0 and the plugin will be refused. Write `0.1.0.0`, or omit the element.
- If the file is missing the plugin is treated as compatible.

`tools/build.py` writes this file, adding `<uiversion>` only when the plugin ships skins.

## Plugin Skins

The touch UI (`TUI.json`) and knob mapping (`Q-Links.json`). Both optional: without them the MPC shows a generic UI with 16 parameter sliders per page and maps the knobs in order. See `ui-guide.md` for the easy way (a short `ui.json` that the SDK turns into these files) and `ui-reference.md` for the full format.

Hardware-specific variants are allowed, for example `Q-Links - 8by1.json` for the Force's 8-in-a-row knob layout; the most specific matching file wins.

## Presets

Optional. The reliable way to ship presets is built-in programs in code (`addProgram` in the framework), which the MPC lists in its preset popup and stores by number in projects. See `presets.md` for that and for the MPC's `.xpl` preset file format.

## Packaging for distribution

Zip the plugin folder together with an `INSTALL.txt` (`tools/package.py` does this). Users unzip and copy the folder into `VST2 Plugins` on a drive.
