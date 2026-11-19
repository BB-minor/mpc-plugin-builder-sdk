// Engine.cpp - the Drum Infurno DSP core.
//
// SPDX-License-Identifier: MIT
//
// The chain was lifted verbatim out of the MPC plugin when the core was extracted; the
// per-sample code below is the same code that passed 41 offline checks and host_sim at
// 32, 128 and 512 frames, with the only change being that parameters now come from a
// plain array instead of the MPC framework's atomics. The extraction is covered by a
// bit-exactness test: the refactored build must render the test suite's WAVs identically
// to the pre-refactor build.
//
// Every comment explaining WHY a stage is the way it is lives with that stage, in
// damage.h, body.h and ladder.h.

#include "../include/infurno/Engine.h"

#include "dsp/math.h"
#include "dsp/biquad.h"
#include "dsp/delay_line.h"
#include "dsp/smoother.h"
#include "dsp/onepole.h"

#include "damage.h"
#include "body.h"
#include "ladder.h"

#include <cmath>
#include <cstdio>

namespace infurno {

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
constexpr float kMakeupMin    = 0.2511886f;  // -12 dB
constexpr float kMakeupMax    = 3.9810717f;  // +12 dB
constexpr float kMakeupRmsMs  = 300.0f;      // "slow": averages over the loop, not the hit
constexpr float kMakeupGateMs = 1.0e-6f;     // -60 dBFS mean square: below this, hold

const char* const kDamageChoices[] = { "Tube", "Tape", "Overdrive" };

const ParamInfo kParams[kNumParams] = {
    // id        name       min      max       def      unit  dec  scale                 choices            n
    { "out",    "Output",  -24.0f,   12.0f,     0.0f,   "dB", 1, Scale::Linear,      nullptr,            0 },
    { "mix",    "Mix",       0.0f,  100.0f,    60.0f,   "%",  0, Scale::Linear,      nullptr,            0 },
    { "dtype",  "Damage",    0.0f,    2.0f,     0.0f,   "",   0, Scale::Linear,      kDamageChoices,     3 },
    { "drive",  "Drive",     0.0f,  100.0f,     0.0f,   "%",  0, Scale::Linear,      nullptr,            0 },
    { "shape",  "Shape",  -100.0f,  100.0f,     0.0f,   "",   0, Scale::Linear,      nullptr,            0 },
    { "grain",  "Grain",     0.0f,  100.0f,     0.0f,   "%",  0, Scale::Linear,      nullptr,            0 },
    { "cutlo",  "Cut Lo",  -12.0f,   12.0f,     0.0f,   "dB", 1, Scale::Linear,      nullptr,            0 },
    { "cutmid", "Cut Mid", -12.0f,   12.0f,     0.0f,   "dB", 1, Scale::Linear,      nullptr,            0 },
    { "cuthi",  "Cut Hi",  -12.0f,   12.0f,     0.0f,   "dB", 1, Scale::Linear,      nullptr,            0 },
    { "chamber","Chamber",   0.0f,    4.0f,     1.0f,   "",   0, Scale::Linear,      kChamberNames,      5 },
    { "bore",   "Bore",      0.25f,   4.0f,     1.0f,   "x",  2, Scale::Logarithmic, nullptr,            0 },
    { "ring",   "Ring",      0.0f,  100.0f,    45.0f,   "%",  0, Scale::Linear,      nullptr,            0 },
    { "bloom",  "Bloom",     0.0f,  100.0f,    35.0f,   "%",  0, Scale::Linear,      nullptr,            0 },
    { "rise",   "Rise",      0.3f,  100.0f,     3.0f,   "ms", 1, Scale::Logarithmic, nullptr,            0 },
    { "fall",   "Fall",     10.0f, 1000.0f,   150.0f,   "ms", 0, Scale::Logarithmic, nullptr,            0 },
    { "makeup", "Makeup",    0.0f,    1.0f,     1.0f,   "",   0, Scale::Linear,      nullptr,            2 },
    { "keeplo", "Keep Lo",  20.0f,  250.0f,    20.0f,   "Hz", 0, Scale::Logarithmic, nullptr,            0 },
    { "tame",   "Tame",    200.0f, 20000.0f, 20000.0f,  "Hz", 0, Scale::Logarithmic, nullptr,            0 },
    { "core",   "Core",      0.0f,  100.0f,     0.0f,   "%",  0, Scale::Linear,      nullptr,            0 },
};

struct Program { const char* name; float v[kNumParams]; };

//                       out    mix   type drive shape grain   lo    mid    hi  cham  bore   ring  bloom rise   fall  mkp  keepLo   tame   core
const Program kPrograms[] = {
 { "Init",           {  0.0f, 60.0f, 0.0f,  0.0f,  0.0f,  0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.00f, 45.0f, 35.0f,  3.0f, 150.0f, 1.0f, 20.0f, 20000.0f, 0.0f } },
 { "Tube Weight",    {  0.0f, 70.0f, 0.0f, 45.0f,-25.0f, 40.0f, 2.0f,-1.0f, 0.0f, 0.0f, 0.85f, 55.0f, 35.0f,  5.0f, 220.0f, 1.0f, 20.0f, 20000.0f, 0.0f } },
 { "Tape Glue",      {  0.0f, 65.0f, 1.0f, 35.0f,-40.0f, 55.0f, 1.0f, 0.0f,-2.0f, 1.0f, 1.00f, 40.0f, 45.0f,  6.0f, 250.0f, 1.0f, 20.0f,  9000.0f, 0.0f } },
 { "Overdrive Snap", {  0.0f, 70.0f, 2.0f, 55.0f, 35.0f, 45.0f,-1.0f, 2.0f, 3.0f, 2.0f, 1.35f, 55.0f, 60.0f,  2.0f, 120.0f, 1.0f, 20.0f, 20000.0f, 0.0f } },
 { "Room Bloom",     { -1.0f, 60.0f, 0.0f, 25.0f,  0.0f, 25.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.00f, 40.0f, 80.0f,  8.0f, 400.0f, 1.0f, 20.0f, 20000.0f, 0.0f } },
 { "Metal Ring",     { -2.0f, 75.0f, 2.0f, 65.0f, 50.0f, 70.0f,-2.0f, 1.0f, 4.0f, 3.0f, 1.80f, 85.0f, 45.0f,  1.5f, 300.0f, 1.0f, 20.0f,  7000.0f, 0.0f } },
 { "808 Furnace",    {  0.0f, 65.0f, 0.0f, 60.0f,-50.0f, 70.0f, 4.0f,-2.0f,-3.0f, 0.0f, 0.50f, 65.0f, 25.0f, 10.0f, 320.0f, 1.0f, 70.0f,  3000.0f, 0.0f } },
 { "Hat Grind",      { -2.0f, 55.0f, 2.0f, 50.0f, 60.0f, 60.0f,-3.0f, 0.0f, 5.0f, 4.0f, 2.60f, 50.0f, 40.0f,  1.0f,  60.0f, 1.0f, 20.0f, 20000.0f, 0.0f } },
};
constexpr int kNumProgramsConst = (int)(sizeof(kPrograms) / sizeof(kPrograms[0]));

} // namespace

// ---------------------------------------------------------------------------
// Shared metadata
// ---------------------------------------------------------------------------
const ParamInfo& paramInfo(int id)
{
    return kParams[clamp(id, 0, (int)kNumParams - 1)];
}

const char* shapeLabel(int damageType) { return DamageStage::shapeName(damageType); }
const char* grainLabel(int damageType) { return DamageStage::grainName(damageType); }

int          numPrograms()            { return kNumProgramsConst; }
const char*  programName(int i)       { return kPrograms[clamp(i, 0, kNumProgramsConst - 1)].name; }
const float* programValues(int i)     { return kPrograms[clamp(i, 0, kNumProgramsConst - 1)].v; }
int          latencySamples()         { return kFlutterNominal; }

void formatValue(int id, float plain, int damageType, char* out, std::size_t size)
{
    if (out == nullptr || size == 0) return;
    const int t = clamp(damageType, 0, DamageStage::kNumTypes - 1);
    switch (id)
    {
        case kOutput:
        case kCutLo:
        case kCutMid:
        case kCutHi:    std::snprintf(out, size, "%+.1f dB", (double)plain); break;
        case kShape:    std::snprintf(out, size, "%s %+.0f", shapeLabel(t), (double)plain); break;
        case kGrain:    std::snprintf(out, size, "%s %.0f",  grainLabel(t), (double)plain); break;
        case kMix:
        case kDrive:
        case kRing:
        case kBloom:
        case kCore:     std::snprintf(out, size, "%.0f %%", (double)plain); break;
        case kBore:     std::snprintf(out, size, "%.2fx", (double)plain); break;
        case kKeepLo:   if (plain <= 20.5f) std::snprintf(out, size, "Off");
                        else std::snprintf(out, size, "%.0f Hz", (double)plain);
                        break;
        case kTame:     if (plain >= 19000.0f) std::snprintf(out, size, "Open");
                        else if (plain >= 1000.0f) std::snprintf(out, size, "%.1f kHz", (double)plain * 0.001);
                        else std::snprintf(out, size, "%.0f Hz", (double)plain);
                        break;
        case kRise:     std::snprintf(out, size, plain < 10.0f ? "%.1f ms" : "%.0f ms", (double)plain); break;
        case kFall:     std::snprintf(out, size, "%.0f ms", (double)plain); break;
        case kDamageType:
        case kChamber:
        {
            const ParamInfo& p = paramInfo(id);
            const int n = clamp((int)std::lround(plain), 0, p.numChoices - 1);
            std::snprintf(out, size, "%s", p.choices[n]);
            break;
        }
        case kMakeup:   std::snprintf(out, size, plain >= 0.5f ? "On" : "Off"); break;
        default:        std::snprintf(out, size, "%.2f", (double)plain); break;
    }
}

// ---------------------------------------------------------------------------
// The engine
// ---------------------------------------------------------------------------
struct Engine::Impl
{
    float p[kNumParams];

    DamageStage     damage;
    BodyStage       body;
    DelayLine       dryDelay[2];
    Biquad          eqLo[2], eqMid[2], eqHi[2];
    LinkwitzRiley4  splitRaw[2], splitDry[2];
    LadderLP        tame[2];
    DCBlocker       lowDc[2], wetDc[2];
    Smoother        cutLo, cutMid, cutHi, mix, outGain, makeup;

    double sr = 44100.0;
    float  rmsCoeff = 0.0f;
    float  inMs = 0.0f, wetMs = 0.0f;
    bool   makeupOn = true;
    bool   tameBypass = true;
    bool   keepLoBypass = true;
    bool   safetyClipOn = true;
    unsigned ctrl = 0;

    Impl() { for (int i = 0; i < kNumParams; ++i) p[i] = kParams[i].def; }

    float getInt(int id) const { return (float)std::lround(p[id]); }

    void snapAll()
    {
        cutLo.snap(clamp(p[kCutLo], -12.0f, 12.0f));
        cutMid.snap(clamp(p[kCutMid], -12.0f, 12.0f));
        cutHi.snap(clamp(p[kCutHi], -12.0f, 12.0f));
        mix.snap(clamp(p[kMix] * 0.01f, 0.0f, 1.0f));
        outGain.snap(dbToGain(clamp(p[kOutput], -24.0f, 12.0f)));
        makeup.snap(1.0f);
        body.snap(p[kBore], p[kRing] * 0.01f);
    }

    // CUT: three RBJ sections. At 0 dB each is exactly an identity, so centre is
    // transparent rather than nearly transparent.
    void updateCutCoefficients()
    {
        const float nyq = 0.49f * (float)sr;
        const BiquadCoeffs lo  = BiquadCoeffs::lowShelf (sr, clamp(100.0f,  20.0f, nyq), 0.707f, cutLo.value());
        const BiquadCoeffs mid = BiquadCoeffs::peak     (sr, clamp(900.0f,  20.0f, nyq), 0.90f,  cutMid.value());
        const BiquadCoeffs hi  = BiquadCoeffs::highShelf(sr, clamp(4500.0f, 20.0f, nyq), 0.707f, cutHi.value());
        for (int c = 0; c < 2; ++c) { eqLo[c].setCoeffs(lo); eqMid[c].setCoeffs(mid); eqHi[c].setCoeffs(hi); }
    }
};

Engine::Engine() : impl_(new Impl) {}
Engine::~Engine() = default;

void Engine::prepare(double sampleRate, int /*maxBlockSize*/)
{
    Impl& s = *impl_;
    s.sr = sampleRate;
    s.damage.prepare(sampleRate);
    s.body.prepare(sampleRate);

    for (int c = 0; c < 2; ++c)
    {
        s.dryDelay[c].prepare((int)(sampleRate * 0.01) + 4);
        s.splitRaw[c].setSampleRate(sampleRate);
        s.splitDry[c].setSampleRate(sampleRate);
        s.tame[c].setSampleRate(sampleRate);
        s.lowDc[c].prepare(sampleRate, 8.0f);
        s.wetDc[c].prepare(sampleRate, 8.0f);
    }

    s.cutLo.setTime(20.0f, sampleRate);
    s.cutMid.setTime(20.0f, sampleRate);
    s.cutHi.setTime(20.0f, sampleRate);
    s.mix.setTime(20.0f, sampleRate);
    s.outGain.setTime(20.0f, sampleRate);
    s.makeup.setTime(120.0f, sampleRate);

    s.rmsCoeff = clamp(onePoleCoeff(kMakeupRmsMs, sampleRate), 0.0f, 1.0f);
    s.snapAll();
    s.updateCutCoefficients();
}

void Engine::reset()
{
    Impl& s = *impl_;
    s.damage.reset();
    s.body.reset();
    for (int c = 0; c < 2; ++c)
    {
        s.dryDelay[c].reset();
        s.eqLo[c].reset(); s.eqMid[c].reset(); s.eqHi[c].reset();
        s.splitRaw[c].reset(); s.splitDry[c].reset(); s.tame[c].reset();
        s.lowDc[c].reset(); s.wetDc[c].reset();
    }
    s.inMs = s.wetMs = 0.0f;
    s.ctrl = 0;
    s.snapAll();
}

void Engine::setParam(int id, float plainValue)
{
    if (id >= 0 && id < kNumParams) impl_->p[id] = plainValue;
}

float Engine::getParam(int id) const
{
    return (id >= 0 && id < kNumParams) ? impl_->p[id] : 0.0f;
}

void Engine::setSafetyClipEnabled(bool on) { impl_->safetyClipOn = on; }

void Engine::process(float* const* io, int numSamples)
{
    if (io == nullptr || io[0] == nullptr || numSamples <= 0) return;
    Impl& s = *impl_;

    float* chL = io[0];
    float* chR = io[1] != nullptr ? io[1] : io[0];

    // ----- once per block: push the parameter snapshot into the stages -------
    s.damage.setBlockParams((int)s.getInt(kDamageType), s.p[kDrive] * 0.01f,
                            clamp(s.p[kShape] * 0.01f, -1.0f, 1.0f), s.p[kGrain] * 0.01f);
    s.body.setBlockParams((int)s.getInt(kChamber), s.p[kBore], s.p[kRing] * 0.01f,
                          s.p[kBloom] * 0.01f, s.p[kRise], s.p[kFall]);
    s.body.setCore(s.p[kCore] * 0.01f);

    s.cutLo.setTarget(clamp(s.p[kCutLo], -12.0f, 12.0f));
    s.cutMid.setTarget(clamp(s.p[kCutMid], -12.0f, 12.0f));
    s.cutHi.setTarget(clamp(s.p[kCutHi], -12.0f, 12.0f));
    s.updateCutCoefficients();          // biquads: per block, never per sample

    // KEEP LO: the crossover below which the signal bypasses the whole chain. At the
    // minimum it is bypassed outright, so "Off" really is off and Mix 0 stays bit-exact.
    // The filters keep running either way, so re-engaging it cannot click.
    const float keepHz = clamp(s.p[kKeepLo], 20.0f, 250.0f);
    s.keepLoBypass = keepHz <= 20.5f;
    for (int c = 0; c < 2; ++c) { s.splitRaw[c].setCutoff(keepHz); s.splitDry[c].setCutoff(keepHz); }

    // TAME: the ladder. Fully open bypasses it outright so "off" is exactly transparent.
    const float tameHz = clamp(s.p[kTame], 200.0f, 20000.0f);
    s.tameBypass = tameHz >= 19000.0f;
    if (!s.tameBypass) for (int c = 0; c < 2; ++c) s.tame[c].setCutoff(tameHz);

    s.mix.setTarget(clamp(s.p[kMix] * 0.01f, 0.0f, 1.0f));
    s.outGain.setTarget(dbToGain(clamp(s.p[kOutput], -24.0f, 12.0f)));
    s.makeupOn = s.p[kMakeup] >= 0.5f;
    if (!s.makeupOn) s.makeup.setTarget(1.0f);

    bool sawBadValue = false;
    const float align = kFlutterRead;   // N+1 read offset == N samples of delay

    // ----- per sample --------------------------------------------------------
    for (int i = 0; i < numSamples; ++i)
    {
        // In place: read this frame's inputs into locals before writing its outputs.
        const float rawL = chL[i];
        const float rawR = chR[i];

        // Dry path, delayed to match the wet path's constant latency.
        s.dryDelay[0].write(rawL);
        s.dryDelay[1].write(rawR);
        const float dryL = s.dryDelay[0].readLinear(align);
        const float dryR = s.dryDelay[1].readLinear(align);

        // --- KEEP LO: Linkwitz-Riley 4 crossover ---------------------------
        // The clean low is taken from the DELAYED dry, so it needs no delay line of its
        // own: a filter and a delay commute, so this is exactly the delayed version of
        // the band the chain never sees. It is DC-blocked because bypassing the chain
        // also bypasses Damage's DC blocker and the Body's bandpass.
        float lowRawL, chainInL, lowRawR, chainInR, lowDryL, lowDryR, dryHighL, dryHighR;
        s.splitRaw[0].process(rawL, lowRawL, chainInL);
        s.splitRaw[1].process(rawR, lowRawR, chainInR);
        s.splitDry[0].process(dryL, lowDryL, dryHighL);
        s.splitDry[1].process(dryR, lowDryR, dryHighR);
        (void)lowRawL; (void)lowRawR;
        float lowL = s.lowDc[0].process(lowDryL);
        float lowR = s.lowDc[1].process(lowDryR);
        if (s.keepLoBypass)
        {
            chainInL = rawL;  chainInR = rawR;      // whole signal into the chain
            dryHighL = dryL;  dryHighR = dryR;      // whole signal as the MIX dry side
            lowL = lowR = 0.0f;                     // nothing kept out
        }

        // --- DAMAGE (its own flutter line applies the nominal delay) ---
        float dL, dR;
        if (!s.damage.processFrame(chainInL, chainInR, dL, dR)) sawBadValue = true;

        // --- TAME: ladder lowpass, right after the saturation ---
        if (!s.tameBypass)
        {
            dL = s.tame[0].process(dL);
            dR = s.tame[1].process(dR);
        }

        // --- CUT: low shelf -> bell -> high shelf ---
        dL = s.eqHi[0].process(s.eqMid[0].process(s.eqLo[0].process(dL)));
        dR = s.eqHi[1].process(s.eqMid[1].process(s.eqLo[1].process(dR)));

        // --- BODY: 100 % wet, in series ---
        float bL, bR;
        if (!s.body.processFrame(dL, dR, bL, bR)) sawBadValue = true;

        // --- AUTO-MAKEUP: slow RMS, high band in vs chain out, pre-makeup ---
        // Measured against the HIGH band: the chain never sees the kept low end, so
        // comparing against the full input would ask the makeup to replace energy that
        // was deliberately routed around it.
        const float monoIn  = 0.5f * (dryHighL + dryHighR);
        const float monoWet = 0.5f * (bL + bR);
        s.inMs  += s.rmsCoeff * (monoIn * monoIn - s.inMs);
        s.wetMs += s.rmsCoeff * (monoWet * monoWet - s.wetMs);
        s.inMs  = flushDenormal(s.inMs);
        s.wetMs = flushDenormal(s.wetMs);

        if ((s.ctrl & (kCtrlInterval - 1)) == 0 && s.makeupOn)
        {
            // Hold the gain through silence instead of winding up to +12 dB on noise.
            if (s.inMs > kMakeupGateMs && s.wetMs > 1.0e-12f
                && std::isfinite(s.inMs) && std::isfinite(s.wetMs))
                s.makeup.setTarget(clamp(std::sqrt(s.inMs / s.wetMs), kMakeupMin, kMakeupMax));
        }
        ++s.ctrl;

        // One DC blocker for the whole wet path: Damage only blocks DC in its Tube
        // branch, the ladder and the Cut shelves pass DC, and CORE opens a route straight
        // through the Body, which used to remove it as a side effect of being a bandpass
        // bank. Before the mix, so the dry side stays bit-exact at Mix 0.
        const float mk = s.makeup.process();
        const float wetL = s.wetDc[0].process(bL * mk);
        const float wetR = s.wetDc[1].process(bR * mk);

        // --- MIX band-for-band, then the kept low end, OUTPUT, safety clipper ---
        const float mixv = s.mix.process();
        const float g    = s.outGain.process();

        float yL = (dryHighL + (wetL - dryHighL) * mixv + lowL) * g;
        float yR = (dryHighR + (wetR - dryHighR) * mixv + lowR) * g;

        if (!std::isfinite(yL) || !std::isfinite(yR)) { yL = yR = 0.0f; sawBadValue = true; }

        chL[i] = s.safetyClipOn ? safetyClip(yL) : yL;
        chR[i] = s.safetyClipOn ? safetyClip(yR) : yR;
    }

    // Only reached if something non-finite appeared. Clearing float state is realtime
    // safe (no allocation).
    if (sawBadValue)
    {
        s.damage.panic();
        s.body.panic();
        for (int c = 0; c < 2; ++c)
        {
            s.eqLo[c].reset(); s.eqMid[c].reset(); s.eqHi[c].reset();
            s.splitRaw[c].reset(); s.splitDry[c].reset(); s.tame[c].reset();
            s.lowDc[c].reset(); s.wetDc[c].reset();
        }
        s.inMs = s.wetMs = 0.0f;
    }
}

} // namespace infurno
