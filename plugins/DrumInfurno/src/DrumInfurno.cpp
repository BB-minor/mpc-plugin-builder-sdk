// DrumInfurno.cpp - Drum Infurno, a combined drum-colouration insert effect by INFUR.
//
// SPDX-License-Identifier: MIT
//
// SIGNAL ORDER (strict, as specified)
//
//   in ──┬─────────────── dry, delay-aligned ──────────────────────────┐
//        │                                                             │
//        └─► DAMAGE ─► TAME ─► CUT ─► BODY ─► AUTO-MAKEUP ──────────────┤
//            (3 algos) (ladder)(EQ)  (modal)  (slow RMS)                │
//            ▲ only the band above KEEP LO enters here; the band below   │
//              bypasses the chain and rejoins after the makeup ──────────┤
//                                                                      ▼
//                                            global MIX ─► OUTPUT trim ─► SAFETY SOFT-CLIP ─► out
//
// The three stages live in their own headers:
//   damage.h - Tube / Tape / Overdrive, with SHAPE and GRAIN re-targeted per algorithm
//   body.h   - the ResoBody modal resonator, ported unchanged (see that file's header)
//
// WHAT IS AND IS NOT A CLIP
//   There is exactly ONE safety clipper, at the very end. The waveshapers inside DAMAGE
//   are the effect, not safety stages, and the BODY's own wet-path tanh was removed when
//   it was ported, precisely so clips are not stacked. Upstream stability comes from the
//   BODY's coefficient clamps (its Q ceiling makes self-oscillation arithmetically
//   impossible) and from DAMAGE's post-trim, which keeps headroom going into the
//   resonators - a hot distorted signal into high-Q bands is what spikes.
//
// LATENCY
//   The wet path always runs through the Tape flutter delay line, whatever the algorithm,
//   so switching TYPE never changes latency. The nominal offset is kFlutterNominal
//   samples; the dry path is delayed to match so the global MIX stays phase-coherent, and
//   the plugin reports it to the host.
//
// REALTIME RULES
//   process() allocates nothing, locks nothing, logs nothing, touches no file. Parameters
//   are snapshotted once per block. Biquad coefficients (sin/cos/pow) are computed per
//   block; the BODY's SVF coefficients at control rate; waveshaper gains per sample from
//   smoothers. Parameter 0 is OUTPUT, a smoothed gain, because the MPC nudges parameter 0
//   to 1.0/0.0 and back on every prepare and that must be inaudible.

#include "mpc_plugin.h"
#include "dsp/math.h"
#include "dsp/biquad.h"
#include "dsp/delay_line.h"
#include "dsp/smoother.h"

#include "damage.h"
#include "body.h"
#include "ladder.h"

#include <cmath>
#include <cstdio>

using namespace mpc::dsp;

namespace {

// --- the final safety clipper ----------------------------------------------
// Fixed ceiling at -0.5 dBFS with the knee 6 dB below it. Below the knee the response is
// exactly unity; above it a tanh bends the signal asymptotically onto the ceiling, so it
// is progressive (the harder you drive, the more the peaks flatten, which is part of the
// tone) and the ceiling is a mathematical guarantee rather than a hope. The slope at the
// knee is 1, so there is no corner to hear.
constexpr float kCeiling = 0.9440609f;   // dbToGain(-0.5)
constexpr float kKnee    = 0.4731513f;   // kCeiling * dbToGain(-6.0)

inline float safetyClip(float x)
{
    const float a = std::fabs(x);
    if (a <= kKnee) return x;
    const float y = kKnee + (kCeiling - kKnee) * fastTanh((a - kKnee) / (kCeiling - kKnee));
    return clamp(x < 0.0f ? -y : y, -kCeiling, kCeiling);
}

// --- auto-makeup bounds -----------------------------------------------------
constexpr float kMakeupMin   = 0.2511886f;  // -12 dB
constexpr float kMakeupMax   = 3.9810717f;  // +12 dB
constexpr float kMakeupRmsMs = 300.0f;      // "slow": averages over the loop, not the hit
constexpr float kMakeupGateMs = 1.0e-6f;    // -60 dBFS mean square: below this, hold

} // namespace

class DrumInfurno : public mpc::Plugin
{
public:
    // Parameter order is permanent: saved-state order, Program-mode Q-Link order, and what
    // the MPC lists. Never reorder or rename an id; append only.
    enum Params
    {
        kOutput = 0,   // dB  - parameter 0 on purpose (smoothed gain, tolerates the nudge)
        kMix,          // %   - global dry/wet for the whole unit
        kDamageType,   // Tube / Tape / Overdrive
        kDrive,        // %   - 0 = clean, Damage effectively bypassed
        kShape,        // +-  - Focus / Reel / Slope, depending on type
        kGrain,        // %   - Heat / Squeeze / Bite, depending on type
        kCutLo,        // dB  - low shelf
        kCutMid,       // dB  - bell
        kCutHi,        // dB  - high shelf
        kChamber,      // A-E - modal set
        kBore,         // x   - body size
        kRing,         // %   - resonance intensity
        kBloom,        // %   - envelope depth onto Ring
        kRise,         // ms  - Bloom attack
        kFall,         // ms  - Bloom release
        kMakeup,       // on/off
        // --- appended in 1.1; never inserted above, that would move every id ---
        kKeepLo,       // Hz  - crossover below which the signal bypasses the chain
        kTame,         // Hz  - ladder lowpass before makeup and the clipper
        kNumParams
    };

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name        = "Drum Infurno";                    // == "name" in plugin.json
        i.vendor      = "INFUR";                           // == "manufacturer" in plugin.json
        i.uniqueId    = MPC_FOURCC('I', 'C', 'P', 'O');    // == "uid" in plugin.json ("ICPO")
        i.version     = 1000;
        i.isInstrument = false;    // insert effect: the MPC sends it no MIDI
        i.numInputs   = 2;
        i.numOutputs  = 2;
        i.wantsMidi   = false;
        i.sendsMidi   = false;
        i.latencySamples = di::kFlutterNominal;   // constant across algorithms
        i.tailSamples = 88200;                    // ~2 s: the longest ring the Body leaves
        return i;
    }

    DrumInfurno() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;

        addParam(floatParam ("out",   "Output",   -24.0f, 12.0f,   0.0f, "dB", 1));
        addParam(floatParam ("mix",   "Mix",        0.0f, 100.0f, 60.0f, "%", 0));
        addParam(choiceParam("dtype", "Damage",   { "Tube", "Tape", "Overdrive" }, 0));
        addParam(floatParam ("drive", "Drive",      0.0f, 100.0f,  0.0f, "%", 0));
        addParam(floatParam ("shape", "Shape",   -100.0f, 100.0f,  0.0f, "", 0));
        addParam(floatParam ("grain", "Grain",      0.0f, 100.0f,  0.0f, "%", 0));
        addParam(floatParam ("cutlo", "Cut Lo",   -12.0f, 12.0f,   0.0f, "dB", 1));
        addParam(floatParam ("cutmid","Cut Mid",  -12.0f, 12.0f,   0.0f, "dB", 1));
        addParam(floatParam ("cuthi", "Cut Hi",   -12.0f, 12.0f,   0.0f, "dB", 1));
        addParam(choiceParam("chamber", "Chamber",
                             { di::kChamberNames[0], di::kChamberNames[1], di::kChamberNames[2],
                               di::kChamberNames[3], di::kChamberNames[4] }, 1));
        addParam(logParam   ("bore",  "Bore",       0.25f, 4.0f,   1.0f, "x", 2));
        addParam(floatParam ("ring",  "Ring",       0.0f, 100.0f, 45.0f, "%", 0));
        addParam(floatParam ("bloom", "Bloom",      0.0f, 100.0f, 35.0f, "%", 0));
        addParam(logParam   ("rise",  "Rise",       0.3f, 100.0f,  3.0f, "ms", 1));
        addParam(logParam   ("fall",  "Fall",      10.0f, 1000.0f,150.0f, "ms", 0));
        addParam(boolParam  ("makeup","Makeup",    true));
        addParam(logParam   ("keeplo","Keep Lo",   20.0f, 250.0f,   20.0f, "Hz", 0));
        addParam(logParam   ("tame",  "Tame",     200.0f, 20000.0f, 20000.0f, "Hz", 0));

        if (numParams() != kNumParams)
            mpc::log("DrumInfurno: parameter count mismatch (%d vs %d)", numParams(), (int)kNumParams);

        // ----- factory programs: plain values, in parameter order -------------
        //                        out    mix   type drive shape grain  lo    mid    hi  cham  bore  ring  bloom rise   fall  mkp  keepLo   tame
        addProgram("Init",          {  0.0f, 60.0f, 0.0f,  0.0f,  0.0f,  0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.00f, 45.0f, 35.0f,  3.0f, 150.0f, 1.0f , 20.0f, 20000.0f});
        addProgram("Tube Weight",   {  0.0f, 70.0f, 0.0f, 45.0f,-25.0f, 40.0f, 2.0f,-1.0f, 0.0f, 0.0f, 0.85f, 55.0f, 35.0f,  5.0f, 220.0f, 1.0f , 20.0f, 20000.0f});
        addProgram("Tape Glue",     {  0.0f, 65.0f, 1.0f, 35.0f,-40.0f, 55.0f, 1.0f, 0.0f,-2.0f, 1.0f, 1.00f, 40.0f, 45.0f,  6.0f, 250.0f, 1.0f , 20.0f,  9000.0f});
        addProgram("Overdrive Snap",{  0.0f, 70.0f, 2.0f, 55.0f, 35.0f, 45.0f,-1.0f, 2.0f, 3.0f, 2.0f, 1.35f, 55.0f, 60.0f,  2.0f, 120.0f, 1.0f , 20.0f, 20000.0f});
        addProgram("Room Bloom",    { -1.0f, 60.0f, 0.0f, 25.0f,  0.0f, 25.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.00f, 40.0f, 80.0f,  8.0f, 400.0f, 1.0f , 20.0f, 20000.0f});
        addProgram("Metal Ring",    { -2.0f, 75.0f, 2.0f, 65.0f, 50.0f, 70.0f,-2.0f, 1.0f, 4.0f, 3.0f, 1.80f, 85.0f, 45.0f,  1.5f, 300.0f, 1.0f , 20.0f,  7000.0f});
        addProgram("808 Furnace",   {  0.0f, 65.0f, 0.0f, 60.0f,-50.0f, 70.0f, 4.0f,-2.0f,-3.0f, 0.0f, 0.50f, 65.0f, 25.0f, 10.0f, 320.0f, 1.0f , 70.0f,  3000.0f});
        addProgram("Hat Grind",     { -2.0f, 55.0f, 2.0f, 50.0f, 60.0f, 60.0f,-3.0f, 0.0f, 5.0f, 4.0f, 2.60f, 50.0f, 40.0f,  1.0f,  60.0f, 1.0f , 20.0f, 20000.0f});
    }

    // The MPC shows the parameter name and this text and does NOT append the unit label
    // (docs/host-contract.md), so units live in the value text. SHAPE and GRAIN also carry
    // the name of whatever they currently do, so the screen never says just a number.
    // UI thread only.
    bool formatValue(int index, float plain, std::string& out) override
    {
        char buf[48];
        const int type = clamp(getInt(kDamageType), 0, di::DamageStage::kNumTypes - 1);
        switch (index)
        {
            case kOutput:
            case kCutLo:
            case kCutMid:
            case kCutHi:    std::snprintf(buf, sizeof(buf), "%+.1f dB", (double)plain); break;
            case kMix:
            case kDrive:
            case kRing:
            case kBloom:
            case kGrain:
            case kShape:
                if (index == kShape)
                    std::snprintf(buf, sizeof(buf), "%s %+.0f", di::DamageStage::shapeName(type), (double)plain);
                else if (index == kGrain)
                    std::snprintf(buf, sizeof(buf), "%s %.0f", di::DamageStage::grainName(type), (double)plain);
                else
                    std::snprintf(buf, sizeof(buf), "%.0f %%", (double)plain);
                break;
            case kBore:     std::snprintf(buf, sizeof(buf), "%.2fx", (double)plain); break;
            case kKeepLo:   if (plain <= 20.5f) std::snprintf(buf, sizeof(buf), "Off");
                            else std::snprintf(buf, sizeof(buf), "%.0f Hz", (double)plain);
                            break;
            case kTame:     if (plain >= 19000.0f) std::snprintf(buf, sizeof(buf), "Open");
                            else if (plain >= 1000.0f) std::snprintf(buf, sizeof(buf), "%.1f kHz", (double)plain * 0.001);
                            else std::snprintf(buf, sizeof(buf), "%.0f Hz", (double)plain);
                            break;
            case kRise:     std::snprintf(buf, sizeof(buf), plain < 10.0f ? "%.1f ms" : "%.0f ms", (double)plain); break;
            case kFall:     std::snprintf(buf, sizeof(buf), "%.0f ms", (double)plain); break;
            default: return false;   // Damage, Chamber, Makeup: the choice label is right
        }
        out = buf;
        return true;
    }

    // ----- lifecycle --------------------------------------------------------
    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        sr_ = sampleRate;
        damage_.prepare(sampleRate);
        body_.prepare(sampleRate);

        for (int c = 0; c < 2; ++c)
        {
            dryDelay_[c].prepare((int)(sampleRate * 0.01) + 4);
            splitRaw_[c].setSampleRate(sampleRate);
            splitDry_[c].setSampleRate(sampleRate);
            tame_[c].setSampleRate(sampleRate);
            lowDc_[c].prepare(sampleRate, 8.0f);
        }

        cutLo_.setTime(20.0f, sampleRate);
        cutMid_.setTime(20.0f, sampleRate);
        cutHi_.setTime(20.0f, sampleRate);
        mix_.setTime(20.0f, sampleRate);
        outGain_.setTime(20.0f, sampleRate);
        makeup_.setTime(120.0f, sampleRate);

        rmsCoeff_ = clamp(onePoleCoeff(kMakeupRmsMs, sampleRate), 0.0f, 1.0f);
        snapAll();
        updateCutCoefficients();
    }

    void reset() override
    {
        damage_.reset();
        body_.reset();
        for (int c = 0; c < 2; ++c)
        {
            dryDelay_[c].reset();
            eqLo_[c].reset(); eqMid_[c].reset(); eqHi_[c].reset();
            splitRaw_[c].reset(); splitDry_[c].reset(); tame_[c].reset(); lowDc_[c].reset();
        }
        inMs_ = wetMs_ = 0.0f;
        ctrl_ = 0;
        snapAll();
    }

    // ----- audio ------------------------------------------------------------
    void process(mpc::ProcessContext& ctx) override
    {
        if (ctx.inputs == nullptr || ctx.outputs == nullptr || ctx.numOutputs < 1) return;

        // ----- once per block: snapshot parameters (they are atomics) ---------
        damage_.setBlockParams(getInt(kDamageType), get(kDrive) * 0.01f,
                               clamp(get(kShape) * 0.01f, -1.0f, 1.0f), get(kGrain) * 0.01f);
        body_.setBlockParams(getInt(kChamber), get(kBore), get(kRing) * 0.01f,
                             get(kBloom) * 0.01f, get(kRise), get(kFall));

        cutLo_.setTarget(clamp(get(kCutLo), -12.0f, 12.0f));
        cutMid_.setTarget(clamp(get(kCutMid), -12.0f, 12.0f));
        cutHi_.setTarget(clamp(get(kCutHi), -12.0f, 12.0f));
        updateCutCoefficients();          // biquads: per block, never per sample

        // KEEP LO: the crossover below which the signal bypasses the whole chain.
        const float keepHz = clamp(get(kKeepLo), 20.0f, 250.0f);
        // At the minimum the crossover is bypassed outright, so "Off" really is off: with
        // it engaged the dry side of MIX is the dry HIGH band, which is magnitude-flat but
        // phase-shifted, and at Mix 0 that is not bit-identical to the input. Switching it
        // out at the bottom of the range restores an exact bypass for the default setting.
        // The filters keep running either way, so re-engaging it cannot click.
        keepLoBypass_ = keepHz <= 20.5f;
        for (int c = 0; c < 2; ++c)
        {
            splitRaw_[c].setCutoff(keepHz); splitDry_[c].setCutoff(keepHz);
        }

        // TAME: the ladder. Fully open bypasses it outright so "off" is exactly transparent.
        const float tameHz = clamp(get(kTame), 200.0f, 20000.0f);
        tameBypass_ = tameHz >= 19000.0f;
        if (!tameBypass_) for (int c = 0; c < 2; ++c) tame_[c].setCutoff(tameHz);

        mix_.setTarget(clamp(get(kMix) * 0.01f, 0.0f, 1.0f));
        outGain_.setTarget(dbToGain(clamp(get(kOutput), -24.0f, 12.0f)));
        makeupOn_ = getBool(kMakeup);
        if (!makeupOn_) makeup_.setTarget(1.0f);

        const float* inL = ctx.inputs[0];
        const float* inR = ctx.numInputs > 1 && ctx.inputs[1] != nullptr ? ctx.inputs[1] : inL;
        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : ctx.outputs[0];
        if (inL == nullptr || outL == nullptr) return;

        bool sawBadValue = false;
        const float align = di::kFlutterRead;   // N+1 read offset == N samples of delay

        // ----- per sample ----------------------------------------------------
        for (int i = 0; i < ctx.numFrames; ++i)
        {
            // In place: the input and output buffers are the same memory, so read this
            // frame's inputs into locals before writing this frame's outputs.
            const float rawL = inL[i];
            const float rawR = inR[i];

            // Dry path, delayed to match the wet path's constant latency.
            dryDelay_[0].write(rawL);
            dryDelay_[1].write(rawR);
            const float dryL = dryDelay_[0].readLinear(align);
            const float dryR = dryDelay_[1].readLinear(align);

            // --- KEEP LO: subtractive crossover ---------------------------------
            // The chain gets (x - low), and (low) is added back after the makeup, so the
            // two halves reconstruct the input exactly: no phase cancellation to design
            // around, and the sub stays at its original level whatever the chain does.
            // The clean low is taken from the DELAYED dry, so it needs no delay line of
            // its own - a lowpass and a delay commute, so this is exactly the delayed
            // version of what was removed from the chain's input.
            // DC-blocked: this band bypasses the chain, so it also bypasses Damage's DC
            // blocker and the Body's bandpass. Without this the input's DC lands straight
            // on the output - and at the minimum Keep Lo setting the "low band" is very
            // nearly just DC. At 8 Hz an 808's fundamental is untouched.
            float lowRawL, chainInL, lowRawR, chainInR, lowDryL, lowDryR, dryHighL, dryHighR;
            splitRaw_[0].process(rawL, lowRawL, chainInL);
            splitRaw_[1].process(rawR, lowRawR, chainInR);
            splitDry_[0].process(dryL, lowDryL, dryHighL);
            splitDry_[1].process(dryR, lowDryR, dryHighR);
            (void)lowRawL; (void)lowRawR;
            float lowL = lowDc_[0].process(lowDryL);
            float lowR = lowDc_[1].process(lowDryR);
            if (keepLoBypass_)
            {
                chainInL = rawL;  chainInR = rawR;      // whole signal into the chain
                dryHighL = dryL;  dryHighR = dryR;      // whole signal as the MIX dry side
                lowL = lowR = 0.0f;                     // nothing kept out
            }

            // --- DAMAGE (its own flutter line applies the same nominal delay) ---
            float dL, dR;
            if (!damage_.processFrame(chainInL, chainInR, dL, dR)) sawBadValue = true;

            // --- TAME: ladder lowpass, right after the saturation -----------------
            // Placed here, not after the Body, because the Body is a bandpass bank: by the
            // time the signal leaves it there is often no high end left to remove, so a
            // ladder after it would be inaudible at most settings. Here it catches the
            // fizz where the fizz is made, which is the classic amp order (drive -> tone
            // -> cabinet), and it is still ahead of the makeup and the clipper as asked.
            if (!tameBypass_)
            {
                dL = tame_[0].process(dL);
                dR = tame_[1].process(dR);
            }

            // --- CUT: low shelf -> bell -> high shelf ---
            dL = eqHi_[0].process(eqMid_[0].process(eqLo_[0].process(dL)));
            dR = eqHi_[1].process(eqMid_[1].process(eqLo_[1].process(dR)));

            // --- BODY: 100 % wet, in series ---
            float bL, bR;
            if (!body_.processFrame(dL, dR, bL, bR)) { sawBadValue = true; }

            // --- AUTO-MAKEUP: slow RMS, high band in vs chain out, pre-makeup -----
            // Measured against the HIGH band, not the whole input: the chain never sees
            // the kept low end, so comparing it to the full input would ask the makeup to
            // replace energy that was deliberately routed around it.
            const float monoIn  = 0.5f * (dryHighL + dryHighR);
            const float monoWet = 0.5f * (bL + bR);
            inMs_  += rmsCoeff_ * (monoIn * monoIn - inMs_);
            wetMs_ += rmsCoeff_ * (monoWet * monoWet - wetMs_);
            inMs_  = flushDenormal(inMs_);
            wetMs_ = flushDenormal(wetMs_);

            if ((ctrl_ & (di::kCtrlInterval - 1)) == 0 && makeupOn_)
            {
                // Hold the gain through silence instead of winding up to +12 dB on noise.
                if (inMs_ > kMakeupGateMs && wetMs_ > 1.0e-12f && std::isfinite(inMs_) && std::isfinite(wetMs_))
                    makeup_.setTarget(clamp(std::sqrt(inMs_ / wetMs_), kMakeupMin, kMakeupMax));
            }
            ++ctrl_;

            const float mk = makeup_.process();
            const float wetL = bL * mk;
            const float wetR = bR * mk;

            // --- global MIX, then the kept low end, OUTPUT trim, safety clipper ---
            // MIX crossfades only the band the chain actually processes, between the
            // DRY HIGH band and the wet. The kept low end is added afterwards at unity.
            //
            // This matters: LR4's two bands sum to an allpass, which at the crossover is
            // 180 degrees from the unfiltered input. Blending the clean low against a
            // full-range dry therefore SUBTRACTS at the crossover - measured at -22.5 dB
            // at 150 Hz with Mix 60. Crossfading band-for-band instead keeps both sides
            // of the fade in the same phase, and the low end is independent of MIX, which
            // is what "untouched" should mean.
            const float mix = mix_.process();
            const float g   = outGain_.process();

            float yL = (dryHighL + (wetL - dryHighL) * mix + lowL) * g;
            float yR = (dryHighR + (wetR - dryHighR) * mix + lowR) * g;

            if (!std::isfinite(yL) || !std::isfinite(yR)) { yL = yR = 0.0f; sawBadValue = true; }

            outL[i] = safetyClipOn_ ? safetyClip(yL) : yL;
            outR[i] = safetyClipOn_ ? safetyClip(yR) : yR;
        }

        // Only reached if something non-finite appeared. Clearing float state is realtime
        // safe (no allocation), unlike reset() on the UI thread.
        if (sawBadValue)
        {
            damage_.panic();
            body_.panic();
            for (int c = 0; c < 2; ++c)
            {
                eqLo_[c].reset(); eqMid_[c].reset(); eqHi_[c].reset();
                splitRaw_[c].reset(); splitDry_[c].reset(); tame_[c].reset(); lowDc_[c].reset();
            }
            inMs_ = wetMs_ = 0.0f;
        }
    }

    // Test hook only: the safety clipper is always on in normal use. The offline harness
    // switches it off to measure what the chain really produces before the ceiling.
    void setSafetyClipEnabled(bool on) { safetyClipOn_ = on; }

private:
    void snapAll()
    {
        cutLo_.snap(clamp(get(kCutLo), -12.0f, 12.0f));
        cutMid_.snap(clamp(get(kCutMid), -12.0f, 12.0f));
        cutHi_.snap(clamp(get(kCutHi), -12.0f, 12.0f));
        mix_.snap(clamp(get(kMix) * 0.01f, 0.0f, 1.0f));
        outGain_.snap(dbToGain(clamp(get(kOutput), -24.0f, 12.0f)));
        makeup_.snap(1.0f);
        body_.snap(get(kBore), get(kRing) * 0.01f);
    }

    // CUT: three RBJ sections. At 0 dB each is exactly an identity, so centre is
    // transparent rather than nearly transparent. Clamped to the Nyquist the SDK allows.
    void updateCutCoefficients()
    {
        const float nyq = 0.49f * (float)sr_;
        const BiquadCoeffs lo  = BiquadCoeffs::lowShelf (sr_, clamp(100.0f,  20.0f, nyq), 0.707f, cutLo_.value());
        const BiquadCoeffs mid = BiquadCoeffs::peak     (sr_, clamp(900.0f,  20.0f, nyq), 0.90f,  cutMid_.value());
        const BiquadCoeffs hi  = BiquadCoeffs::highShelf(sr_, clamp(4500.0f, 20.0f, nyq), 0.707f, cutHi_.value());
        for (int c = 0; c < 2; ++c) { eqLo_[c].setCoeffs(lo); eqMid_[c].setCoeffs(mid); eqHi_[c].setCoeffs(hi); }
    }

    di::DamageStage damage_;
    di::BodyStage   body_;

    DelayLine    dryDelay_[2];
    Biquad       eqLo_[2], eqMid_[2], eqHi_[2];
    di::LinkwitzRiley4 splitRaw_[2], splitDry_[2];   // same cutoff: one on raw, one on delayed dry
    di::LadderLP tame_[2];
    DCBlocker    lowDc_[2];
    Smoother  cutLo_, cutMid_, cutHi_, mix_, outGain_, makeup_;

    double sr_ = 44100.0;
    float  rmsCoeff_ = 0.0f;
    float  inMs_ = 0.0f, wetMs_ = 0.0f;
    bool   makeupOn_ = true;
    bool   tameBypass_ = true;
    bool   keepLoBypass_ = true;
    bool   safetyClipOn_ = true;
    unsigned ctrl_ = 0;
};

MPC_PLUGIN_ENTRY(DrumInfurno)
