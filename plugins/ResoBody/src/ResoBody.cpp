// ResoBody.cpp - a dynamic modal resonator ("body") insert effect for the MPC, by INFUR.
//
// SPDX-License-Identifier: MIT
//
// The plugin is called "Reason 2 Scream" on the MPC; ResoBody is the engine's internal
// name and the name of this project folder. The MPC-visible strings are in makeInfo()
// below and must stay equal to "name" / "manufacturer" / "uid" in plugin.json.
//
// WHAT IT IS
//   The resonant-body half of a classic drum-colouring distortion unit, on its own.
//   There is deliberately NO distortion and NO bitcrush here: the MPC already has both
//   on board. ResoBody is meant to sit *after* a native distortion and *before* a
//   bitcrush, and give the already-dirty signal a physical body to ring in.
//
// SIGNAL FLOW (per channel, stereo, in place)
//
//            ┌────────────────────────── dry ──────────────────────────┐
//            │                                                         │
//   in ──────┼──► 4 x resonant band (modal bank, parallel) ──► Σ ──► tanh soft clip ──► wet
//            │         ▲            ▲                                  │
//            │      centre        Q (RESO)                             ▼
//            │      (SCALE)         ▲                            dry/wet MIX ──► OUTPUT trim ──► out
//            │                      │
//            └──► envelope follower (INPUT ONLY) ──► AUTO depth ──┘
//                 ATK / REL                          (+ a little SCALE)
//
//   The four bands are a "modal fingerprint": BODY TYPE (A..E) recalls a fixed set of
//   four frequency ratios, relative gains and Q weights. SCALE multiplies all four
//   centre frequencies together (body size). RESO scales all four Qs together. AUTO
//   pushes RESO (and 18 % as much SCALE) upward on transients, so the body blooms on
//   every hit and settles between them.
//
// WHY THE FOLLOWER WATCHES THE INPUT AND NOT THE OUTPUT
//   An envelope follower on the resonator output is a feedback loop: more ring raises
//   the envelope, which raises Q, which raises the ring. Driving it from the input only
//   makes the modulation strictly feed-forward, so it cannot run away no matter how the
//   knobs are set. That is the single most important stability decision in this file.
//
// STABILITY (this plugin runs inside the MPC application with no sandbox)
//   * Q is hard-clamped to kQCeil (20) after every modulation. The TPT state-variable
//     filter only self-oscillates at k = 1/Q = 0, i.e. Q = infinity, so a finite ceiling
//     means it can never self-oscillate. The SVF resonance input is independently
//     clamped to kResoCeil as a second belt.
//   * Every band is first normalised to unity peak gain (bp * k, see updateCoefficients)
//     and then given a BOUNDED resonance makeup of at most +9.5 dB. Without the
//     normalisation the SVF's bandpass peaks at Q, so high RESO would be a +26 dB
//     surprise; without the makeup a narrow band captures so little of a drum's
//     broadband energy that the body disappears. Together the wet level stays inside a
//     10 dB window across the whole RESO range, and the ceiling is a constant, so no
//     knob combination can produce unbounded gain.
//   * Every coefficient is range-checked and NaN-checked before it reaches a filter, and
//     the wet sum is sanitised per sample. If anything non-finite ever appears, the bank
//     is zeroed at the end of the block (clearing floats; no allocation).
//   * Denormals: the framework sets flush-to-zero around process(), and the follower
//     state and wet sums are flushed explicitly because they are the long-tail paths.
//
// REALTIME RULES OBSERVED
//   * process() allocates nothing, locks nothing, logs nothing, touches no file.
//   * Parameters are read once per block into a snapshot; filter coefficients are
//     recomputed at control rate (every 16 samples) because they cost a tan().
//   * The control-rate counter runs across block boundaries, so the sound is identical
//     at 32, 128 and 512 frames per call.
//   * Parameter 0 is OUTPUT, a smoothed gain: the MPC nudges parameter 0 to 1.0/0.0 and
//     back on every prepare, and that must be inaudible and side-effect free.

#include "mpc_plugin.h"
#include "dsp/math.h"
#include "dsp/svf.h"
#include "dsp/smoother.h"

#include <cmath>
#include <cstdio>

using namespace mpc::dsp;

namespace {

// ---------------------------------------------------------------------------
// The modal fingerprints: BODY TYPE A..E.
//
// Four modes each, as a ratio of the base frequency (100 Hz) at SCALE = 1.00x,
// a relative gain in dB (0 dB = the loudest mode of that body) and a Q weight
// that RESO is multiplied by. The ratios are deliberately inharmonic - real
// boxes, cones and shells do not resonate in integer multiples - which is what
// makes the bank sound like a body rather than a chord.
//
//   A  tight, low, small sealed cabinet            72 .. 405 Hz
//   B  bigger cabinet, the general-purpose body    100 .. 585 Hz
//   C  hollow wooden shell                         128 .. 860 Hz
//   D  bright, metallic, tom/rim territory         165 .. 1260 Hz
//   E  wide and open, hats and top end             210 .. 2150 Hz
// ---------------------------------------------------------------------------
struct Mode
{
    float ratio;    // multiplied by kBaseHz and by SCALE
    float gainDb;   // relative level of this mode within the body
    float qWeight;  // this mode's share of the global RESO setting
};

constexpr int kNumModes = 4;
constexpr int kNumTypes = 5;

const Mode kBodies[kNumTypes][kNumModes] = {
    // A - Tight
    { { 0.72f,  0.0f, 1.15f }, { 1.48f,  -3.5f, 1.00f }, { 2.42f,  -7.0f, 0.85f }, { 4.05f, -11.0f, 0.70f } },
    // B - Cab
    { { 1.00f,  0.0f, 1.10f }, { 2.05f,  -2.5f, 1.00f }, { 3.42f,  -5.5f, 0.90f }, { 5.85f,  -9.0f, 0.75f } },
    // C - Wood
    { { 1.28f,  0.0f, 1.00f }, { 2.72f,  -2.0f, 1.05f }, { 5.10f,  -4.5f, 0.95f }, { 8.60f,  -7.5f, 0.80f } },
    // D - Bright
    { { 1.65f, -1.0f, 0.90f }, { 3.95f,   0.0f, 1.05f }, { 7.40f,  -3.0f, 1.00f }, { 12.60f, -6.0f, 0.90f } },
    // E - Wide
    { { 2.10f, -2.0f, 0.85f }, { 5.30f,   0.0f, 0.95f }, { 11.20f, -2.5f, 1.05f }, { 21.50f, -5.0f, 1.00f } },
};

const char* const kTypeNames[kNumTypes] = { "A Tight", "B Cab", "C Wood", "D Bright", "E Wide" };

// --- tuning constants -------------------------------------------------------
constexpr float kBaseHz   = 100.0f;   // the ratios above are relative to this at SCALE 1.00x
constexpr float kMinHz    = 20.0f;    // lowest centre frequency we will ask a filter for
constexpr float kMaxHz    = 16000.0f; // highest (the SVF clamps at 0.49 * sr = 21.6 kHz anyway)

constexpr float kQMin     = 0.70f;    // RESO 0 %   -> a gentle, wide colouration
constexpr float kQMax     = 14.0f;    // RESO 100 % -> a clear ring, before AUTO is added
constexpr float kQOctaves = 4.321928f;// log2(kQMax / kQMin): RESO is exponential in Q
constexpr float kQFloor   = 0.50f;    // after the per-mode Q weight
constexpr float kQCeil    = 20.0f;    // HARD ceiling, AUTO included: never self-oscillates
constexpr float kResoCeil = 0.985f;   // second clamp, on the SVF's own 0..1 resonance input

constexpr float kEnvDrive      = 3.0f;  // envelope sensitivity before the soft bound
constexpr float kAutoResoSpan  = 0.55f; // AUTO at 100 % adds this much normalised RESO
constexpr float kAutoScaleSpan = 0.18f; // ... and this much SCALE (the "small amount")
constexpr float kQMakeupMax    = 3.0f;  // +9.5 dB ceiling on the resonance makeup (see below)
constexpr float kBankTrim      = 1.0f;  // makeup into the soft clipper
constexpr int   kCtrlInterval  = 16;    // samples between coefficient updates (power of two)

// ---------------------------------------------------------------------------
// Envelope follower: rectify, then a one-pole with separate attack and release
// coefficients. The SDK has no envelope-follower class, so this is built from
// onePoleCoeff() and flushDenormal() in dsp/math.h rather than hand-rolled maths.
// Allocation free; two multiplies and a compare per sample.
// ---------------------------------------------------------------------------
class EnvFollower
{
public:
    void setTimes(float attackMs, float releaseMs, double sampleRate)
    {
        attack_  = clamp(onePoleCoeff(clamp(attackMs,  0.05f, 1000.0f), sampleRate), 0.0f, 1.0f);
        release_ = clamp(onePoleCoeff(clamp(releaseMs, 1.0f, 5000.0f), sampleRate), 0.0f, 1.0f);
    }
    void reset() { env_ = 0.0f; }
    float value() const { return env_; }

    float process(float x)
    {
        const float rectified = std::fabs(x);
        env_ += (rectified > env_ ? attack_ : release_) * (rectified - env_);
        env_ = flushDenormal(env_);
        if (!std::isfinite(env_)) env_ = 0.0f;
        return env_;
    }

private:
    float env_ = 0.0f, attack_ = 1.0f, release_ = 0.001f;
};

} // namespace

class ResoBody : public mpc::Plugin
{
public:
    // Parameter order is permanent: it is the saved-state order, the Program-mode
    // Q-Link order and what the MPC lists. Never reorder; append only.
    enum Params
    {
        kOutput = 0,   // dB   - parameter 0 on purpose: a smoothed gain tolerates the host's nudge
        kMix,          // %    - dry/wet
        kScale,        // x    - body size: multiplies all four centre frequencies
        kReso,         // %    - global resonance intensity (all four Qs together)
        kAuto,         // %    - envelope-follower depth onto RESO (and a little SCALE)
        kAttack,       // ms   - follower attack
        kRelease,      // ms   - follower release
        kType,         // A..E - the modal fingerprint
        kNumParams
    };

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name        = "Reason 2 Scream";                 // == "name" in plugin.json
        i.vendor      = "INFUR";                           // == "manufacturer" in plugin.json
        i.uniqueId    = MPC_FOURCC('I', 'Y', 'k', 'r');    // == "uid" in plugin.json ("IYkr")
        i.version     = 1000;
        i.isInstrument = false;    // an insert effect: the MPC sends it no MIDI
        i.numInputs   = 2;
        i.numOutputs  = 2;
        i.wantsMidi   = false;
        i.sendsMidi   = false;
        i.tailSamples = 88200;     // ~2 s: the longest ring a 20 Hz mode at Q 20 can leave
        return i;
    }

    ResoBody() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;

        addParam(floatParam("out",   "Output",    -24.0f, 12.0f,  0.0f, "dB", 1));
        addParam(floatParam("mix",   "Mix",         0.0f, 100.0f, 50.0f, "%", 0));
        addParam(logParam  ("scale", "Scale",       0.25f, 4.0f,  1.0f, "x", 2));
        addParam(floatParam("reso",  "Reso",        0.0f, 100.0f, 45.0f, "%", 0));
        addParam(floatParam("auto",  "Auto",        0.0f, 100.0f, 35.0f, "%", 0));
        addParam(logParam  ("atk",   "Atk",         0.3f, 100.0f,  3.0f, "ms", 1));
        addParam(logParam  ("rel",   "Rel",        10.0f, 1000.0f, 150.0f, "ms", 0));
        addParam(choiceParam("type", "Body Type",
                             { kTypeNames[0], kTypeNames[1], kTypeNames[2], kTypeNames[3], kTypeNames[4] }, 1));

        if (numParams() != kNumParams)
            mpc::log("ResoBody: parameter count mismatch (%d vs %d)", numParams(), (int)kNumParams);

        // dB -> linear once, here, so the audio thread never calls pow() for the bank gains.
        for (int t = 0; t < kNumTypes; ++t)
            for (int m = 0; m < kNumModes; ++m)
                gainLin_[t][m] = dbToGain(kBodies[t][m].gainDb);

        // ----- factory programs: plain values, in parameter order ------------
        //                       out    mix   scale  reso  auto   atk    rel   type
        addProgram("Init",          {  0.0f,  50.0f, 1.00f, 45.0f, 35.0f,  3.0f, 150.0f, 1.0f });
        addProgram("Kick Weight",   {  0.0f,  40.0f, 0.80f, 55.0f, 30.0f,  5.0f, 220.0f, 0.0f });
        addProgram("Snare Crack",   { -1.0f,  55.0f, 1.35f, 60.0f, 55.0f,  2.0f, 120.0f, 2.0f });
        addProgram("Hat Tighten",   { -2.0f,  35.0f, 2.60f, 50.0f, 40.0f,  1.0f,  60.0f, 4.0f });
        addProgram("Room Bloom",    { -1.5f,  65.0f, 1.00f, 40.0f, 75.0f,  8.0f, 400.0f, 1.0f });
        addProgram("Metal Ring",    { -3.0f,  70.0f, 1.80f, 85.0f, 45.0f,  1.5f, 300.0f, 3.0f });
        addProgram("808 Body",      {  0.0f,  45.0f, 0.50f, 65.0f, 25.0f, 10.0f, 320.0f, 0.0f });
        addProgram("Full Resonator",{ -4.0f, 100.0f, 1.20f, 75.0f, 60.0f,  2.0f, 250.0f, 2.0f });
    }

    // The MPC shows the parameter name and this text, and does NOT append the unit
    // label (see docs/host-contract.md), so the units live in the value text.
    // Called on the UI thread only.
    bool formatValue(int index, float plain, std::string& out) override
    {
        char buf[32];
        switch (index)
        {
            case kOutput:  std::snprintf(buf, sizeof(buf), "%+.1f dB", (double)plain); break;
            case kMix:
            case kReso:
            case kAuto:    std::snprintf(buf, sizeof(buf), "%.0f %%", (double)plain); break;
            case kScale:   std::snprintf(buf, sizeof(buf), "%.2fx", (double)plain); break;
            case kAttack:  std::snprintf(buf, sizeof(buf), plain < 10.0f ? "%.1f ms" : "%.0f ms", (double)plain); break;
            case kRelease: std::snprintf(buf, sizeof(buf), "%.0f ms", (double)plain); break;
            default: return false;   // Body Type: the choice label is right already
        }
        out = buf;
        return true;
    }

    // ----- lifecycle --------------------------------------------------------
    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        for (int c = 0; c < 2; ++c)
            for (int m = 0; m < kNumModes; ++m)
                svf_[c][m].setSampleRate(sampleRate);

        // Scale is smoothed in log2 space so a sweep is even in pitch, not in Hz.
        scaleOct_.setTime(30.0f, sampleRate);
        reso_.setTime(30.0f, sampleRate);
        mix_.setTime(20.0f, sampleRate);
        outGain_.setTime(20.0f, sampleRate);

        for (int c = 0; c < 2; ++c) follower_[c].setTimes(get(kAttack), get(kRelease), sampleRate);

        snapSmoothers();
    }

    void reset() override
    {
        for (int c = 0; c < 2; ++c)
        {
            follower_[c].reset();
            for (int m = 0; m < kNumModes; ++m) svf_[c][m].reset();
        }
        ctrl_ = 0;
        snapSmoothers();
    }

    // ----- audio ------------------------------------------------------------
    void process(mpc::ProcessContext& ctx) override
    {
        if (ctx.inputs == nullptr || ctx.outputs == nullptr || ctx.numOutputs < 1) return;

        // ----- once per block: snapshot the parameters (they are atomics) -----
        type_    = clamp(getInt(kType), 0, kNumTypes - 1);
        autoAmt_ = clamp(get(kAuto) * 0.01f, 0.0f, 1.0f);

        scaleOct_.setTarget(std::log2(clamp(get(kScale), 0.25f, 4.0f)));
        reso_.setTarget(clamp(get(kReso) * 0.01f, 0.0f, 1.0f));
        mix_.setTarget(clamp(get(kMix) * 0.01f, 0.0f, 1.0f));
        outGain_.setTarget(dbToGain(clamp(get(kOutput), -24.0f, 12.0f)));

        for (int c = 0; c < 2; ++c) follower_[c].setTimes(get(kAttack), get(kRelease), ctx.sampleRate);

        const float* inL = ctx.inputs[0];
        const float* inR = ctx.numInputs > 1 && ctx.inputs[1] != nullptr ? ctx.inputs[1] : inL;
        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : ctx.outputs[0];
        if (inL == nullptr || outL == nullptr) return;

        bool sawBadValue = false;

        // ----- per sample ---------------------------------------------------
        for (int i = 0; i < ctx.numFrames; ++i)
        {
            // In place: the input and output buffers are the same memory, so read
            // this frame's inputs into locals before writing this frame's outputs.
            const float dryL = inL[i];
            const float dryR = inR[i];

            // The followers watch the INPUT only (see the header comment).
            const float envL = follower_[0].process(dryL);
            const float envR = follower_[1].process(dryR);

            // Advance the smoothers every sample; the coefficient update reads them.
            scaleOct_.process();
            reso_.process();

            if ((ctrl_ & (kCtrlInterval - 1)) == 0) updateCoefficients(0.5f * (envL + envR));
            ++ctrl_;

            float wetL = 0.0f, wetR = 0.0f;
            for (int m = 0; m < kNumModes; ++m)
            {
                float lp, bp, hp;
                svf_[0][m].processAll(dryL, lp, bp, hp);
                wetL += bp * norm_[m];
                svf_[1][m].processAll(dryR, lp, bp, hp);
                wetR += bp * norm_[m];
            }

            wetL = flushDenormal(wetL);
            wetR = flushDenormal(wetR);

            // A filter state can only misbehave if something upstream went wrong, but
            // this plugin runs in the MPC's own process, so never let it reach the DAC.
            if (!std::isfinite(wetL) || !std::isfinite(wetR))
            {
                wetL = wetR = 0.0f;
                sawBadValue = true;
            }

            // Gentle soft clip on the wet path after the resonators: unity gain for
            // small signals, catches the peaks a high-Q mode can throw.
            wetL = fastTanh(wetL * kBankTrim);
            wetR = fastTanh(wetR * kBankTrim);

            const float mix = mix_.process();
            const float g   = outGain_.process();

            outL[i] = (dryL + (wetL - dryL) * mix) * g;
            outR[i] = (dryR + (wetR - dryR) * mix) * g;
        }

        // Clearing float state is realtime safe (no allocation), unlike reset() on the
        // UI thread. Only ever reached if a non-finite value appeared above.
        if (sawBadValue)
            for (int c = 0; c < 2; ++c)
                for (int m = 0; m < kNumModes; ++m) svf_[c][m].reset();
    }

private:
    void snapSmoothers()
    {
        scaleOct_.snap(std::log2(clamp(get(kScale), 0.25f, 4.0f)));
        reso_.snap(clamp(get(kReso) * 0.01f, 0.0f, 1.0f));
        mix_.snap(clamp(get(kMix) * 0.01f, 0.0f, 1.0f));
        outGain_.snap(dbToGain(clamp(get(kOutput), -24.0f, 12.0f)));
    }

    // Control rate (every kCtrlInterval samples): one exp2 for SCALE, one for Q,
    // and one tan() per filter inside SVF::set(). Everything is clamped twice: once
    // musically (frequency, Q) and once at the filter's own input (resonance).
    void updateCoefficients(float env)
    {
        // Bound the envelope into 0..1 with a soft curve, so AUTO behaves the same on a
        // quiet loop and a hot one, and can never exceed its budget.
        const float envN = clamp(fastTanh(clamp(env, 0.0f, 8.0f) * kEnvDrive), 0.0f, 1.0f);

        const float resoEff  = clamp(reso_.value() + autoAmt_ * envN * kAutoResoSpan, 0.0f, 1.0f);
        const float scaleEff = std::exp2(scaleOct_.value()) * (1.0f + autoAmt_ * envN * kAutoScaleSpan);
        const float qGlobal  = kQMin * std::exp2(resoEff * kQOctaves);

        for (int m = 0; m < kNumModes; ++m)
        {
            float hz = kBaseHz * kBodies[type_][m].ratio * scaleEff;
            float q  = qGlobal * kBodies[type_][m].qWeight;

            hz = clamp(hz, kMinHz, kMaxHz);
            q  = clamp(q, kQFloor, kQCeil);

            // SVF resonance from Q: the filter uses k = 1/Q = 2 - 1.98 * resonance.
            float resonance = clamp((2.0f - 1.0f / q) / 1.98f, 0.0f, kResoCeil);

            if (!std::isfinite(hz) || !std::isfinite(resonance))
            {
                hz = 1000.0f;           // inaudible fallback rather than an unknown state
                resonance = 0.0f;
                norm_[m] = 0.0f;
                svf_[0][m].set(hz, resonance);
                svf_[1][m].set(hz, resonance);
                continue;
            }

            // The SVF's bandpass peaks at 1/k = Q, so multiplying by k normalises the
            // band to unity peak gain. That alone makes a high-Q body nearly inaudible:
            // a narrow band only catches a sliver of a drum's broadband energy, and for
            // a constant-peak bandpass the captured amplitude falls as 1/sqrt(Q). So add
            // sqrt(Q) back as a makeup, hard-limited to kQMakeupMax, which keeps the wet
            // level roughly constant as RESO sweeps while bounding the gain by a
            // constant the knobs cannot influence.
            const float k = 2.0f - 1.98f * resonance;
            const float makeup = clamp(std::sqrt(q), 1.0f, kQMakeupMax);
            norm_[m] = k * makeup * gainLin_[type_][m];

            svf_[0][m].set(hz, resonance);
            svf_[1][m].set(hz, resonance);
        }
    }

    SVF         svf_[2][kNumModes];        // [channel][mode], parallel bank
    EnvFollower follower_[2];              // input-driven, one per channel
    Smoother    scaleOct_, reso_, mix_, outGain_;

    float gainLin_[kNumTypes][kNumModes] = {{0}};   // dB table converted once in the constructor
    float norm_[kNumModes] = {0};                   // per-band output gain (unity-peak * mode gain)
    int   type_ = 1;
    float autoAmt_ = 0.0f;
    unsigned ctrl_ = 0;                             // runs across blocks: block-size independent
};

MPC_PLUGIN_ENTRY(ResoBody)
