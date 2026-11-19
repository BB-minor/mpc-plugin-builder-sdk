// infurno/Engine.h - the Drum Infurno DSP core.
//
// SPDX-License-Identifier: MIT
//
// ONE ENGINE, TWO WRAPPERS.
//   This library holds the entire signal chain and knows nothing about any plugin format.
//   It has no dependency on the MPC SDK, on the VST2 ABI, on JUCE, or on any host at all:
//   the only things it includes are <cstddef> and <memory>. Both the MPC build
//   (plugins/DrumInfurno) and the desktop VST3/AU build (desktop/) are thin wrappers that
//   push parameters in and call process(). Neither owns a copy of the DSP.
//
//   The parameter table below is also shared, so the two wrappers cannot drift apart on
//   ranges, defaults, names or value text.
//
// THE CHAIN
//
//   in ──┬──────────────── dry, delay-aligned ───────────────────────────────┐
//        │                                                                   │
//        ├─► KEEP LO ─ low band ──────────── clean, untouched ──────────┐    │
//        │      │                                                       │    │
//        │   high band                                                  ▼    ▼
//        └──────┴─► DAMAGE ─► TAME ─► CUT ─► BODY ─► AUTO-MAKEUP ─► + ─► MIX ─► OUTPUT ─► SAFETY CLIP ─► out
//
// THREADING
//   setParam() and process() must be called from the SAME thread (the audio thread).
//   The engine holds plain floats, not atomics, on purpose: every host already gives its
//   wrapper a thread-safe way to read a parameter, so adding a second layer of atomics
//   here would cost something and buy nothing. The wrapper snapshots its parameters at
//   the top of each block, calls setParam() for each, then calls process().
//
//   prepare() and reset() allocate; process() never does.
//
// CHANNELS
//   Stereo. process() takes two in-place buffers. A mono host should pass the same
//   pointer twice, or duplicate afterwards.

#ifndef INFURNO_ENGINE_H
#define INFURNO_ENGINE_H

#include <cstddef>
#include <memory>

namespace infurno {

// ---------------------------------------------------------------------------
// Parameters. The order is PERMANENT: it is the MPC's saved-state order and its
// Q-Link order. Append only; never insert, never renumber, never rename an id.
// ---------------------------------------------------------------------------
enum ParamId
{
    kOutput = 0,   // dB  - parameter 0 on purpose: the MPC nudges index 0 on every prepare
    kMix,          // %   - global dry/wet, band-for-band (see Engine.cpp)
    kDamageType,   // Tube / Tape / Overdrive
    kDrive,        // %   - 0 = clean, Damage bit-exactly bypassed
    kShape,        // +-  - Focus / Reel / Slope, depending on Damage type
    kGrain,        // %   - Heat / Squeeze / Bite, depending on Damage type
    kCutLo,        // dB  - low shelf, 100 Hz
    kCutMid,       // dB  - bell, 900 Hz
    kCutHi,        // dB  - high shelf, 4.5 kHz
    kChamber,      // A-E - modal set
    kBore,         // x   - body size
    kRing,         // %   - resonance intensity
    kBloom,        // %   - envelope depth onto Ring
    kRise,         // ms  - Bloom attack
    kFall,         // ms  - Bloom release
    kMakeup,       // 0/1 - auto-makeup on
    kKeepLo,       // Hz  - crossover below which the signal bypasses the chain
    kTame,         // Hz  - ladder lowpass after Damage
    kCore,         // %   - Body through-path
    kNumParams
};

enum class Scale { Linear, Logarithmic };

struct ParamInfo
{
    const char* id;              // stable machine name, used in saved state. Never rename.
    const char* name;            // short display name (<= 12 chars for the MPC screen)
    float       min, max, def;
    const char* unit;            // "dB", "%", "ms", "Hz", "x", or ""
    int         decimals;
    Scale       scale;
    const char* const* choices;  // nullptr for continuous parameters
    int         numChoices;      // 0 for continuous, 2 for a bool
};

const ParamInfo& paramInfo(int id);

// Value text, shared so the MPC screen and the desktop editor never disagree. SHAPE and
// GRAIN carry the name of whatever they currently do, which depends on the Damage type,
// so that one is passed in. Writes at most `size` bytes including the terminator.
void formatValue(int id, float plainValue, int damageType, char* out, std::size_t size);

// What SHAPE and GRAIN are called for a given Damage type.
const char* shapeLabel(int damageType);   // "Focus" / "Reel"    / "Slope"
const char* grainLabel(int damageType);   // "Heat"  / "Squeeze" / "Bite"

// Factory programs, shared by both wrappers.
int          numPrograms();
const char*  programName(int index);
const float* programValues(int index);    // kNumParams plain values, in ParamId order

// Constant across Damage algorithms, so switching type never changes latency.
int latencySamples();

// ---------------------------------------------------------------------------
// The engine.
// ---------------------------------------------------------------------------
class Engine
{
public:
    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Allocates. Call before processing and whenever the sample rate changes.
    void prepare(double sampleRate, int maxBlockSize);

    // Clears every filter, delay and envelope. Allocation free.
    void reset();

    // Plain (real-world) units, matching paramInfo(id). Out-of-range values are clamped
    // by the stages that use them. Audio thread, alongside process().
    void setParam(int id, float plainValue);
    float getParam(int id) const;

    // In place, stereo. io[0] and io[1] are read and written; they may be the same
    // pointer for mono. Never allocates, locks, logs or touches a file.
    void process(float* const* io, int numSamples);

    // The safety clipper is always on in normal use. The offline test harness switches it
    // off to measure what the chain produces before the ceiling. Not a user control.
    void setSafetyClipEnabled(bool on);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace infurno

#endif // INFURNO_ENGINE_H
