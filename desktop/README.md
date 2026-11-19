# Drum Infurno — desktop VST3 / AU

The same plugin as the MPC build, in a JUCE wrapper. Both link `../core` (`infurno_core`);
there is no DSP in this folder and no second copy of the engine anywhere.

## What you need on the Mac

Nothing is assumed to be installed. You need three things:

```sh
xcode-select --install                 # Xcode command line tools, if you don't have them
brew install cmake                     # or the installer from cmake.org (3.22+)
# git comes with the command line tools
```

JUCE is **not** a prerequisite — CMake fetches it (pinned to 8.0.15) on the first configure.
That first configure clones JUCE and builds its `juceaide` helper, so expect it to take a
few minutes. Later configures are fast.

## Build

```sh
cd desktop
cmake -B build -G Xcode
cmake --build build --config Release
```

Prefer command line tools over Xcode? This works too and is usually quicker:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

The build is **universal (arm64 + x86_64)** by default. For a faster build that only runs
on your own machine:

```sh
cmake -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES=arm64
```

### Where the plugins land

```
desktop/build/DrumInfurno_artefacts/Release/VST3/Drum Infurno.vst3
desktop/build/DrumInfurno_artefacts/Release/AU/Drum Infurno.component
```

`COPY_PLUGIN_AFTER_BUILD` is ON by default, so they are also installed into
`~/Library/Audio/Plug-Ins/VST3/` and `~/Library/Audio/Plug-Ins/Components/`. Turn that off
with `-DINFURNO_COPY_AFTER_BUILD=OFF`.

## Validate

### pluginval — VST3 and AU

```sh
curl -L -o pluginval.zip \
  https://github.com/Tracktion/pluginval/releases/download/v1.0.3/pluginval_macOS.zip
unzip pluginval.zip

PV=./pluginval.app/Contents/MacOS/pluginval

"$PV" --strictness-level 10 --validate-in-process --timeout-ms 600000 \
      --validate "build/DrumInfurno_artefacts/Release/VST3/Drum Infurno.vst3"

"$PV" --strictness-level 10 --validate-in-process --timeout-ms 600000 \
      --validate "build/DrumInfurno_artefacts/Release/AU/Drum Infurno.component"
```

Exit code 0 means everything passed. Strictness 10 covers 44.1 / 48 / 96 kHz against block
sizes 64 / 128 / 256 / 512 / 1024, plus editor open-while-processing, state restoration,
background-thread parameter writes and parameter fuzzing.

> The first run of the AU may fail to even load until macOS has registered the component —
> see the auval note below about `AudioComponentRegistrar`.

### auval — AU only

`auval` is part of macOS; nothing to install. The component has to be in
`~/Library/Audio/Plug-Ins/Components/`, which the build puts it in.

```sh
killall -9 AudioComponentRegistrar 2>/dev/null   # force macOS to rescan
auval -a | grep -i "Drum Infurno"                # confirm it is registered
auval -v aufx Dinf Infr                          # the actual validation
```

`aufx` is the effect type, `Dinf` the plugin code and `Infr` the manufacturer code, as set
in `CMakeLists.txt`. A clean run ends with **AU VALIDATION SUCCEEDED**.

If `auval -a` doesn't list it: confirm `~/Library/Audio/Plug-Ins/Components/Drum Infurno.component`
exists, then log out and back in. macOS caches the component registry aggressively.

### Codesigning

For your own machine nothing is needed; the binaries are ad-hoc signed by the toolchain and
Logic, Live and Reaper will load them. Distribution needs a Developer ID and notarisation,
which is out of scope here.

## What has already been verified, and what hasn't

The project was built and validated on **Linux** in the container where it was written:

- VST3 compiles, links and loads
- `pluginval --strictness-level 10` — **25 test sections, exit code 0, no failures or
  warnings**, across 44.1 / 48 / 96 kHz × block sizes 64–1024
- the DSP core is bit-exact against the MPC build (see `core/README.md`)

**Not verified, because it needs your Mac:** the AU build, `auval`, the universal binary,
and anything Xcode-specific. The plugin code is platform independent and the Linux VST3
passes, so the likely failures are environmental (toolchain, codesigning, macOS component
registration) rather than logic.

## Layout

```
desktop/
├── CMakeLists.txt              JUCE via FetchContent, links ../core
└── Source/
    ├── PluginProcessor.*       parameter layout, state, buses, processBlock
    └── PluginEditor.*          one control per parameter, grouped by signal order
```

### Parameters

Every parameter is built from the core's shared table (`core/src/Engine.cpp`), so ranges,
defaults, names and value text cannot drift from the MPC build. Logarithmic parameters use
the same `min * (max/min)^norm` mapping the MPC uses, so a knob at the same position gives
the same value in both.

State save and recall goes through `AudioProcessorValueTreeState` as XML, and the core's
eight factory programs are exposed as the plugin's programs.

**Shape** and **Grain** keep fixed names but change function with the Damage type. The
editor shows the live function (Focus/Heat, Reel/Squeeze, Slope/Bite) in a label under each
knob. In the host's own automation lane they show a plain number, because a parameter's
text function has no reliable way to read another parameter's current value.
