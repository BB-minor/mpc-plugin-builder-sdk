# Building for the MPC

The MPC runs Linux on ARM. You cross-compile two shared libraries from one source file: a 32-bit ARM build and a 64-bit ARM build. The SDK's build script does all of this with one command; this page explains what it does and the alternatives.

## The one command

```
python tools/build.py plugins/MyPlugin
```

This:

1. Compiles a **native** build for your own computer (`build/native/`) and runs the host simulator against it to extract the parameter list (`build/params.json`).
2. Cross-compiles **arm32** and **arm64** builds into the output folder `build/<Manufacturer> - VST - <Name>/VST/`.
3. Copies `plugin.json`, generates (or copies) the `Plugin Skins`, copies the project's `Content/` folder (samples, wavetables, ...) if there is one, writes `version.xml`.
4. Runs the validator.

Options: `--target native|arm32|arm64|desktop|all`, `--debug`, `--no-ui`, `--no-validate`, `--toolchain zig|gcc|docker`, `--define NAME=VALUE`, `--verbose`.

### Desktop VST2 (`--target desktop`)

`python tools/build.py plugins/<Name> --target desktop` builds the same plugin as a generic VST2 for the computer you are on, so it can be loaded in a desktop VST2 host (Reaper, MPC Software, and so on). It does not build the MPC folder. There is no UI: the host shows its own parameter list (the MPC `Plugin Skins` are not used).

- **macOS**: `build/desktop/<Name>.vst`, a universal (arm64 + x86_64) bundle, ad-hoc signed, exporting both `VSTPluginMain` and `main_macho`. Copy it to `~/Library/Audio/Plug-Ins/VST/`. Needs Apple's command line tools (`xcode-select --install`) for the universal build; otherwise it falls back to a build for this CPU only.
- **Windows**: `build/desktop/<Name>/<slug>.dll` (64-bit) with `plugin.json` and `Content/` beside it. Copy the whole folder into the host's VST2 folder.
- **Linux**: `build/desktop/<Name>/<slug>.so`, same layout; copy it to `~/.vst`.
- `Content/` goes with the plugin (inside the bundle's `Contents/Resources` on macOS), and `mpc_resources.h` finds it there.
- The desktop build is for a quick listen and for hosts like Reaper; many current hosts have dropped VST2, so check yours. The Windows and Linux outputs are cross-compile-checked only; the macOS bundle has been run through `host_sim`.

## Toolchain: zig (default, recommended)

The SDK uses [Zig](https://ziglang.org) as a cross compiler because it is a single download that works the same on macOS, Windows and Linux, needs no Docker, no sysroots, and can target a specific glibc version. `zig c++` is Clang under the hood. The C++ runtime (LLVM's libc++) is linked statically, so the resulting `.so` depends only on the C library that every MPC has.

`build.py` finds `zig` on your PATH, or installs the `ziglang` Python package into `.sdk-tools/zigenv` inside the SDK on first use (a one-time ~50 MB download). You can also install Zig any other way and put it on your PATH, or point `ZIG=/path/to/zig`.

Exact flags used, so you can reproduce them by hand:

```
# 32-bit ARM (MPC X, Live, Live II, One, One+, Key 61, Key 37, Force)
zig c++ -target arm-linux-gnueabihf.2.31 -mcpu=cortex_a17 \
  -std=c++17 -O2 -fPIC -shared -fvisibility=hidden -fvisibility-inlines-hidden \
  -ffunction-sections -fdata-sections -Wall -Wextra \
  -I<sdk>/framework -I<project>/src \
  -Wl,--gc-sections -Wl,-s -Wl,--version-script=<sdk>/tools/exports.map \
  -o "<out>/VST/<slug>-arm32.so" <project>/src/*.cpp

# 64-bit ARM (MPC Live III, MPC XL, later 64-bit products)
zig c++ -target aarch64-linux-gnu.2.31 -mcpu=cortex_a55 ...same flags... -o "<out>/VST/<slug>-arm64.so" <project>/src/*.cpp

# Native (for the host simulator)
zig c++ -std=c++17 -O2 -fPIC -shared -I<sdk>/framework -I<project>/src -o build/native/<slug>.dylib <project>/src/*.cpp
```

Why those targets:

- `.2.31` pins the glibc version the binary will ask for. Every supported MPC has glibc 2.39 or newer in MPC OS 3; 2.31 leaves plenty of headroom and also works on older MPC OS 2.x units should they ever gain the feature.
- `cortex_a17` and `cortex_a55` are the lowest-common CPU of each family. Do not use `-mcpu=native` or newer cores.
- `-fvisibility=hidden` plus the version script makes `VSTPluginMain` the only exported symbol, which avoids clashes with the host's own C++ runtime.

## Toolchain: GNU cross compilers

If you prefer GCC, Debian/Ubuntu packages work:

```
sudo apt install g++-arm-linux-gnueabihf g++-aarch64-linux-gnu
python tools/build.py plugins/MyPlugin --toolchain gcc
```

Flags: `-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard` for arm32, `-march=armv8-a` for arm64, plus `-static-libstdc++ -static-libgcc -fvisibility=hidden`. Use a distribution whose glibc is 2.39 or older (Debian 12 "bookworm", Ubuntu 22.04/24.04 are fine; Debian 13 and Ubuntu 24.10+ ship a newer glibc and the binaries will not load). The Arm GNU Toolchain 13.x releases also work with the same caveat.

## Toolchain: Docker / Podman

`tools/docker/Dockerfile` builds a Debian bookworm image with both GNU cross compilers. `python tools/build.py plugins/MyPlugin --toolchain docker` runs the GCC build inside it.

## Rules the validator enforces

- ELF 32-bit ARM for `file32`, ELF 64-bit AArch64 for `file64`.
- `VSTPluginMain` exported.
- Only `libc`, `libm`, `libpthread`, `libdl`, `librt`, `libgcc_s` and the dynamic loader as dependencies. A dynamic `libstdc++` dependency is a warning.
- Highest required glibc symbol version ≤ 2.39 (warning above 2.36); `GLIBCXX` ≤ 3.4.32 if present.

Run it by hand with `python tools/validate.py plugins/MyPlugin`.

## Debug builds

`--debug` compiles with `-O0 -g` and no stripping. The MPC prints nothing from your plugin unless you write to `stderr`, which ends up in the system log; the practical way to debug DSP is the host simulator on your own machine (`docs/install-and-test.md`).

## Using other plugin frameworks

You do not need JUCE, DPF or iPlug2, and the SDK does not support them out of the box: they need their own VST2 headers (JUCE requires Steinberg's, which can no longer be licensed) and GUI dependencies that do not exist on the MPC. The SDK framework is a few hundred lines, header-only, and covers everything the MPC host uses. If you port existing DSP code, keep the DSP and drop the framework glue.
