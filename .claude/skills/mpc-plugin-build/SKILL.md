---
name: mpc-plugin-build
description: Build an MPC plugin project into the deployable "<Manufacturer> - VST - <Name>" folder (native + 32-bit ARM + 64-bit ARM), and fix compiler, linker, toolchain and validation errors. Use whenever a plugin needs compiling or a build fails.
---

# Build

```
python tools/build.py plugins/<Name>            # everything: native, arm32, arm64, skins, version.xml, validate
python tools/build.py plugins/<Name> --target native   # quick compile check + params.json + skins
python tools/build.py plugins/<Name> --debug    # -O0 -g for the native build when debugging
```

The first run may install the Zig cross compiler into `.sdk-tools/` (one-time download). `--toolchain gcc` uses installed `arm-linux-gnueabihf-g++` / `aarch64-linux-gnu-g++`; `--toolchain docker` uses `tools/docker/Dockerfile`.

Output: `plugins/<Name>/build/<Manufacturer> - VST - <Name>/` with `plugin.json`, `version.xml`, `VST/<slug>-arm32.so`, `VST/<slug>-arm64.so`, `Plugin Skins/`, and `Content/` (verbatim copy of the project's `Content/`, if it has one).

## Fixing failures

- **Compiler errors**: read them verbatim; they are from Clang. Usual causes: wrong framework method names (check `framework/mpc_plugin.h`), missing `#include`, narrowing conversions in `{}` initialisers, unused-variable warnings are not errors.
- **Linker `undefined symbol`**: a library or OS function that the MPC does not provide. Only the C and C++ standard libraries are available; compile third-party DSP sources into `src/`.
- **Validator errors**: the message says what to change (`uid` mismatch, folder name, `numOutputs`, glibc too new, missing export). `uid` in `plugin.json` must equal `MPC_FOURCC(...)` in the C++; `name`/`manufacturer` in `plugin.json` must equal `PluginInfo::name`/`vendor`.
- **Host-sim dump fails** (so no `params.json`): the plugin crashes at load. Run `.sdk-tools/host_sim build/native/<slug>.dylib test --verbose` to see which opcode.
- **Zig missing / no network**: install Zig from ziglang.org, put it on PATH or set `ZIG=`.

Build until there are no errors and the validator prints only OK lines (warnings acceptable if understood). Then run the `mpc-plugin-test` skill.

Details: `docs/building.md`.

## Desktop VST2 (listen on this computer, e.g. in Reaper)

`python tools/build.py plugins/<Name> --target desktop` builds a generic, UI-less VST2 for the current computer into `plugins/<Name>/build/desktop/` (macOS: signed universal `<Name>.vst`; Windows/Linux: a folder with the `.dll`/`.so`, `plugin.json` and `Content/`). Tell the user where to copy it (printed at the end), that the host shows its own parameter list, and that the host must support VST2. This is not a substitute for the MPC build and `host_sim` checks.
