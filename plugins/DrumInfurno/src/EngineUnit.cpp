// EngineUnit.cpp - compiles the shared DSP core into the MPC build.
//
// SPDX-License-Identifier: MIT
//
// tools/build.py compiles every .cpp in this folder and nothing else, so the core needs
// one translation unit here to be part of the MPC binary. This is a unity include, not a
// copy: there is still exactly one Engine.cpp, in core/src/, and the desktop CMake build
// compiles that same file. Do not add code to this file.
#include "../../../core/src/Engine.cpp"
