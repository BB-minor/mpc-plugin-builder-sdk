# infurno_core

The Drum Infurno signal chain as a plain C++17 library: **Keep Lo → Damage → Tame → Cut →
Body → auto-makeup → Mix → Output → safety clip**.

It depends on nothing. No MPC SDK, no VST2 ABI, no JUCE, no host. The public header
includes `<cstddef>` and `<memory>` and that is all.

```
core/
├── include/infurno/Engine.h   the whole public API
├── src/Engine.cpp             the chain, and the shared parameter / program tables
├── src/damage.h               Tube / Tape / Overdrive
├── src/body.h                 the modal resonator
├── src/ladder.h               the Tame ladder and the Keep Lo crossover
├── src/env_follower.h
└── src/dsp/                   DSP primitives (see below)
```

## Who uses it

| Build | Wrapper | How it links |
|---|---|---|
| MPC standalone | `plugins/DrumInfurno/src/DrumInfurno.cpp` | `src/EngineUnit.cpp`, a one-line unity TU, because `tools/build.py` compiles only `src/*.cpp` |
| Desktop VST3 / AU | `desktop/Source/PluginProcessor.cpp` | CMake `target_link_libraries(... infurno_core)` |

Both build their parameter lists, ranges, defaults, value text and factory programs from
the tables in `Engine.cpp`, so the two cannot drift apart.

## `src/dsp/` — vendored, unmodified

These nine headers are copied verbatim from the SDK's `framework/dsp/`, which is MIT
licensed like the rest of this repository. They are copied rather than included from
there so that the library genuinely stands alone and can be dropped into any project.

They are byte-identical to their originals, which is worth keeping true — `cmp` each
against `framework/dsp/` before and after touching anything here:

```sh
for f in core/src/dsp/*.h; do cmp "$f" "framework/dsp/$(basename "$f")" || echo "DIVERGED: $f"; done
```

If the SDK ever updates those primitives, re-copy them and re-run the bit-exactness test
below rather than hand-merging.

## Threading and realtime

`setParam()` and `process()` are called from the same (audio) thread; the engine holds
plain floats, not atomics, because every host already gives its wrapper a thread-safe way
to read a parameter. `prepare()` and `reset()` allocate. `process()` never allocates,
locks, logs or touches a file.

## Proving the extraction changed nothing

The offline harness renders 29 WAVs. The refactor is only correct if they are byte-identical
to the pre-refactor renders:

```sh
c++ -std=c++17 -O2 -I framework -I core/include -I core/src -I plugins/DrumInfurno/src \
    -o /tmp/di_test plugins/DrumInfurno/test/drum_test.cpp
/tmp/di_test /tmp/after
md5sum /tmp/after/*.wav | md5sum      # must equal the reference
```
