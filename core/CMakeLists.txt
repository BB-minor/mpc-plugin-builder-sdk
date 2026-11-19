# infurno_core - the Drum Infurno DSP, with no host, format or SDK dependency.
#
# Both wrappers link this: the MPC build (plugins/DrumInfurno, via a one-line unity TU
# because the MPC build script compiles only src/*.cpp) and the desktop VST3/AU build
# (desktop/). There is exactly one copy of the DSP.
cmake_minimum_required(VERSION 3.22)

add_library(infurno_core STATIC
    src/Engine.cpp
)

target_include_directories(infurno_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include   # consumers see only infurno/Engine.h
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src       # stages and the vendored dsp primitives
)

target_compile_features(infurno_core PUBLIC cxx_std_17)
set_target_properties(infurno_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if (MSVC)
    target_compile_options(infurno_core PRIVATE /W4)
else()
    target_compile_options(infurno_core PRIVATE -Wall -Wextra)
endif()
