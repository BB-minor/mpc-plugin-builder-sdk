// body.h - the BODY stage: a dynamic modal resonator.
//
// SPDX-License-Identifier: MIT
//
// THIS IS THE RESOBODY ENGINE, PORTED UNCHANGED.
// The DSP comes from plugins/ResoBody/src/ResoBody.cpp, which was built, validated and
// tested (host_sim 20/20, 23/23 offline scenarios including a self-oscillation proof).
// The mode table, the Q law, every clamp and the envelope follower are byte-for-byte the
// same numbers. Exactly two things were removed, both because Drum Infurno handles them
// at unit level instead:
//
//   1. the internal dry/wet MIX - the Body now runs 100 % wet in series, and the unit's
//      single global MIX does the parallel blend;
//   2. the wet-path tanh soft clip - there is now one safety clipper at the very end of
//      the chain, and clips must not be stacked.
//
// What is deliberately KEPT, because it is the stability design and not a clip:
//   * Q hard-clamped to kQCeil (20). The TPT state-variable filter only self-oscillates
//     at k = 1/Q = 0, so a finite Q ceiling makes self-oscillation arithmetically
//     impossible. The filter's own 0..1 resonance input is clamped again at kResoCeil.
//   * unity-peak band normalisation (bp * k) plus a sqrt(Q) makeup limited to +9.5 dB,
//     so no knob combination has unbounded gain.
//   * the input-only envelope follower. A follower on the resonator output would be a
//     feedback loop; driving it from the input keeps the modulation feed-forward.
//   * every coefficient range- and NaN-checked before it reaches a filter.
//
// Control names in Drum Infurno (the DSP is identical, only the labels changed):
//   Type -> Chamber, Scale -> Bore, Reso -> Ring, Auto -> Bloom,
//   env Attack -> Rise, env Release -> Fall.

#ifndef DI_BODY_H
#define DI_BODY_H

#include "dsp/math.h"
#include "dsp/svf.h"
#include "dsp/smoother.h"
#include "dsp/onepole.h"

#include "env_follower.h"

#include <cmath>

namespace di {

using namespace mpc::dsp;

// ---------------------------------------------------------------------------
// The modal fingerprints (Chamber A..E). Ratios are relative to kBaseHz at Bore
// 1.00x. Deliberately inharmonic: real boxes and shells do not resonate in
// integer multiples, and integer ratios sound like a chord, not an object.
// ---------------------------------------------------------------------------
struct Mode
{
    float ratio;    // multiplied by kBaseHz and by Bore
    float gainDb;   // relative level of this mode within the body
    float qWeight;  // this mode's share of the global Ring setting
};

constexpr int kNumModes = 4;
constexpr int kNumChambers = 5;

const Mode kBodies[kNumChambers][kNumModes] = {
    // A - Tight    72 / 148 / 242 / 405 Hz   small sealed cabinet
    { { 0.72f,  0.0f, 1.15f }, { 1.48f,  -3.5f, 1.00f }, { 2.42f,  -7.0f, 0.85f }, { 4.05f, -11.0f, 0.70f } },
    // B - Cab     100 / 205 / 342 / 585 Hz   bigger cabinet, general purpose
    { { 1.00f,  0.0f, 1.10f }, { 2.05f,  -2.5f, 1.00f }, { 3.42f,  -5.5f, 0.90f }, { 5.85f,  -9.0f, 0.75f } },
    // C - Wood    128 / 272 / 510 / 860 Hz   hollow wooden shell
    { { 1.28f,  0.0f, 1.00f }, { 2.72f,  -2.0f, 1.05f }, { 5.10f,  -4.5f, 0.95f }, { 8.60f,  -7.5f, 0.80f } },
    // D - Bright  165 / 395 / 740 / 1260 Hz  metallic, toms and rims
    { { 1.65f, -1.0f, 0.90f }, { 3.95f,   0.0f, 1.05f }, { 7.40f,  -3.0f, 1.00f }, { 12.60f, -6.0f, 0.90f } },
    // E - Wide    210 / 530 / 1120 / 2150 Hz open and wide, hats and top end
    { { 2.10f, -2.0f, 0.85f }, { 5.30f,   0.0f, 0.95f }, { 11.20f, -2.5f, 1.05f }, { 21.50f, -5.0f, 1.00f } },
};

const char* const kChamberNames[kNumChambers] = { "A Tight", "B Cab", "C Wood", "D Bright", "E Wide" };

// --- tuning constants (identical to ResoBody) -------------------------------
constexpr float kBaseHz   = 100.0f;
constexpr float kMinHz    = 20.0f;
constexpr float kMaxHz    = 16000.0f;

constexpr float kQMin     = 0.70f;     // Ring 0 %   -> gentle, wide colouration
constexpr float kQMax     = 14.0f;     // Ring 100 % -> a clear ring, before Bloom
constexpr float kQOctaves = 4.321928f; // log2(kQMax / kQMin)
constexpr float kQFloor   = 0.50f;
constexpr float kQCeil    = 20.0f;     // HARD ceiling, Bloom included
constexpr float kResoCeil = 0.985f;    // second clamp on the SVF's resonance input

constexpr float kEnvDrive       = 3.0f;
constexpr float kBloomRingSpan  = 0.55f;  // Bloom 100 % adds this much normalised Ring
constexpr float kBloomBoreSpan  = 0.18f;  // ... and this much Bore
constexpr float kQMakeupMax     = 3.0f;   // +9.5 dB ceiling on the resonance makeup
constexpr int   kCtrlInterval   = 16;     // samples between coefficient updates

// ---------------------------------------------------------------------------
// The stage itself: four resonant bands per channel, in parallel, 100 % wet.
// ---------------------------------------------------------------------------
class BodyStage
{
public:
    void prepare(double sampleRate)
    {
        sr_ = sampleRate;
        for (int c = 0; c < 2; ++c)
            for (int m = 0; m < kNumModes; ++m) svf_[c][m].setSampleRate(sampleRate);
        for (int c = 0; c < 2; ++c) coreDc_[c].prepare(sampleRate, 8.0f);
        boreOct_.setTime(30.0f, sampleRate);   // smoothed in log2 space: even in pitch
        ring_.setTime(30.0f, sampleRate);
        for (int t = 0; t < kNumChambers; ++t)
            for (int m = 0; m < kNumModes; ++m) gainLin_[t][m] = dbToGain(kBodies[t][m].gainDb);
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            follower_[c].reset();
            coreDc_[c].reset();
            for (int m = 0; m < kNumModes; ++m) svf_[c][m].reset();
        }
        ctrl_ = 0;
    }

    // CORE: how much of the Body's input passes straight through, under the resonant
    // bands. An RBJ peaking filter is exactly a direct path plus a scaled bandpass
    // (H_peak = 1 + (A-1) * H_bandpass), so a bank of bandpasses WITHOUT a through-path
    // is a 4-band filter, while the same bank WITH one is the broadband-with-resonances
    // response a cabinet or body actually has. At 0 this is the pure resonator the plugin
    // shipped with; turn it up and the body keeps the signal's full bandwidth, with the
    // distortion intact, instead of relying on the unit's MIX to blend clean signal back.
    void setCore(float core01) { core_ = clamp(core01, 0.0f, 1.0f); }

    // Called once per block. bore is a multiplier (0.25..4), ring and bloom are 0..1.
    void setBlockParams(int chamber, float bore, float ring01, float bloom01,
                        float riseMs, float fallMs)
    {
        chamber_ = clamp(chamber, 0, kNumChambers - 1);
        bloom_   = clamp(bloom01, 0.0f, 1.0f);
        boreOct_.setTarget(std::log2(clamp(bore, 0.25f, 4.0f)));
        ring_.setTarget(clamp(ring01, 0.0f, 1.0f));
        for (int c = 0; c < 2; ++c) follower_[c].setTimes(riseMs, fallMs, sr_);
    }

    void snap(float bore, float ring01)
    {
        boreOct_.snap(std::log2(clamp(bore, 0.25f, 4.0f)));
        ring_.snap(clamp(ring01, 0.0f, 1.0f));
    }

    // One stereo frame, 100 % wet. inL/inR drive the followers (input only).
    // Returns false if a non-finite value had to be suppressed.
    bool processFrame(float inL, float inR, float& outL, float& outR)
    {
        const float envL = follower_[0].process(inL);
        const float envR = follower_[1].process(inR);

        boreOct_.process();
        ring_.process();

        if ((ctrl_ & (kCtrlInterval - 1)) == 0) updateCoefficients(0.5f * (envL + envR));
        ++ctrl_;

        float wetL = 0.0f, wetR = 0.0f;
        for (int m = 0; m < kNumModes; ++m)
        {
            float lp, bp, hp;
            svf_[0][m].processAll(inL, lp, bp, hp);
            wetL += bp * norm_[m];
            svf_[1][m].processAll(inR, lp, bp, hp);
            wetR += bp * norm_[m];
        }

        wetL = flushDenormal(wetL);
        wetR = flushDenormal(wetR);

        if (!std::isfinite(wetL) || !std::isfinite(wetR))
        {
            outL = outR = 0.0f;
            return false;
        }
        // The through-path is DC-blocked: the resonant bands are inherently DC-free, so
        // before CORE existed no DC could leave this stage. Damage only DC-blocks in its
        // Tube branch, so without this the Tape and Overdrive algorithms would push the
        // input's DC straight to the output as soon as CORE came up.
        outL = wetL + coreDc_[0].process(core_ * inL);
        outR = wetR + coreDc_[1].process(core_ * inR);
        return true;
    }

    // Realtime safe: clears float state only, no allocation.
    void panic()
    {
        for (int c = 0; c < 2; ++c)
            for (int m = 0; m < kNumModes; ++m) svf_[c][m].reset();
    }

private:
    void updateCoefficients(float env)
    {
        // Bound the envelope into 0..1 with a soft curve, so Bloom behaves the same on a
        // quiet loop and a hot one, and can never exceed its budget.
        const float envN = clamp(fastTanh(clamp(env, 0.0f, 8.0f) * kEnvDrive), 0.0f, 1.0f);

        const float ringEff = clamp(ring_.value() + bloom_ * envN * kBloomRingSpan, 0.0f, 1.0f);
        const float boreEff = std::exp2(boreOct_.value()) * (1.0f + bloom_ * envN * kBloomBoreSpan);
        const float qGlobal = kQMin * std::exp2(ringEff * kQOctaves);

        for (int m = 0; m < kNumModes; ++m)
        {
            float hz = kBaseHz * kBodies[chamber_][m].ratio * boreEff;
            float q  = qGlobal * kBodies[chamber_][m].qWeight;

            hz = clamp(hz, kMinHz, kMaxHz);
            q  = clamp(q, kQFloor, kQCeil);

            float resonance = clamp((2.0f - 1.0f / q) / 1.98f, 0.0f, kResoCeil);

            if (!std::isfinite(hz) || !std::isfinite(resonance))
            {
                hz = 1000.0f;            // inaudible fallback rather than an unknown state
                resonance = 0.0f;
                norm_[m] = 0.0f;
                svf_[0][m].set(hz, resonance);
                svf_[1][m].set(hz, resonance);
                continue;
            }

            // bp peaks at 1/k = Q, so bp * k is unity peak gain; sqrt(Q) adds back what a
            // narrow band loses of a drum's broadband energy, limited to kQMakeupMax.
            const float k = 2.0f - 1.98f * resonance;
            const float makeup = clamp(std::sqrt(q), 1.0f, kQMakeupMax);
            norm_[m] = k * makeup * gainLin_[chamber_][m];

            svf_[0][m].set(hz, resonance);
            svf_[1][m].set(hz, resonance);
        }
    }

    SVF         svf_[2][kNumModes];
    EnvFollower follower_[2];
    Smoother    boreOct_, ring_;
    DCBlocker   coreDc_[2];

    double sr_ = 44100.0;
    float gainLin_[kNumChambers][kNumModes] = {{0}};
    float norm_[kNumModes] = {0};
    int   chamber_ = 1;
    float bloom_ = 0.0f;
    float core_ = 0.0f;
    unsigned ctrl_ = 0;
};

} // namespace di

#endif // DI_BODY_H
