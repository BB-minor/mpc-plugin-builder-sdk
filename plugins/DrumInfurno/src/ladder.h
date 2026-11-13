// ladder.h - TAME: a zero-delay-feedback Moog-style ladder lowpass.
//
// SPDX-License-Identifier: MIT
//
// WHY THIS IS HAND-WRITTEN
//   The SDK has a state-variable filter (dsp/svf.h) and that is what the BODY uses, but
//   an SVF is not a ladder. The point of a ladder here is that the resonance and the
//   saturation interact: the feedback path is saturated, so as the signal gets hotter the
//   effective resonance softens and the filter leans into the sound instead of ringing on
//   top of it. That is the character a clean 12 dB lowpass cannot give, and it is exactly
//   what takes the glare off a hard-driven 808 without just dulling it.
//
// TOPOLOGY
//   Four one-pole TPT (topology-preserving transform) stages in series with a global
//   feedback of k, solved for zero delay. Per stage, with G = g / (1 + g):
//       y = G * x + (1 - G) * s          (s is the stage state)
//   Cascading four and substituting u = x - k * y4 gives a closed form for y4:
//       y4 = (G^4 * x + S) / (1 + k * G^4)
//   where S collects the state contributions. No unit delay in the feedback loop, so the
//   filter stays stable and in tune when the cutoff is modulated, unlike a naive cascade.
//
// STABILITY
//   * A classic ladder self-oscillates at k = 4. kResonance is a compile-time constant far
//     below that, and it is not exposed as a parameter, so no knob combination can reach
//     self-oscillation.
//   * fastTanh() in the feedback bounds the loop regardless: even if the input were
//     enormous, what returns is at most +-1 * k.
//   * The cutoff is clamped to [20 Hz, 0.45 * sampleRate] before tan() is called, and the
//     coefficients are NaN-checked. process() checks its own output.
//   * Feedback reduces the passband gain to 1 / (1 + k), so the output is multiplied by
//     (1 + k) to restore unity at DC. That factor is a constant, not a knob.
//
// COST
//   One tan() per coefficient update (control rate, not per sample). Per sample: about a
//   dozen multiply-adds and one fastTanh per channel.

#ifndef DI_LADDER_H
#define DI_LADDER_H

#include "dsp/math.h"
#include "dsp/onepole.h"
#include "dsp/biquad.h"

#include <cmath>

namespace di {

using namespace mpc::dsp;

// "a little bit of fixed resonance": 0.9 of the 4.0 that would self-oscillate. Enough to
// give the cutoff some character, not enough to honk when it lands in the presence region.
constexpr float kLadderResonance = 0.9f;

class LadderLP
{
public:
    void setSampleRate(double sr)
    {
        sr_ = (float)sr;
        nyq_ = 0.45f * sr_;
        setCutoff(cutoffHz_);
    }

    void setCutoff(float hz)
    {
        cutoffHz_ = clamp(hz, 20.0f, nyq_);
        const float g = std::tan(kPi * cutoffHz_ / sr_);
        if (!std::isfinite(g) || g <= 0.0f) { G_ = 0.5f; }
        else                                { G_ = g / (1.0f + g); }
        G_ = clamp(G_, 0.0f, 0.9999f);
        const float g4 = G_ * G_ * G_ * G_;
        g4_ = g4;
        const float denom = 1.0f + kLadderResonance * g4;
        invDenom_ = std::isfinite(denom) && denom > 1.0e-6f ? 1.0f / denom : 1.0f;
        oneMinusG_ = 1.0f - G_;
        makeup_ = 1.0f + kLadderResonance;      // restore the passband the feedback costs
    }

    void reset() { s1_ = s2_ = s3_ = s4_ = 0.0f; }

    float process(float x)
    {
        // Zero-delay feedback: solve for the fourth stage's output before running the
        // stages, so there is no unit delay inside the resonance loop.
        const float S = G_ * G_ * G_ * oneMinusG_ * s1_
                      + G_ * G_ * oneMinusG_ * s2_
                      + G_ * oneMinusG_ * s3_
                      + oneMinusG_ * s4_;
        float y4 = (g4_ * x + S) * invDenom_;
        if (!std::isfinite(y4)) { reset(); return 0.0f; }

        // Saturated feedback: this is the Moog part, and it also bounds the loop.
        const float u = x - kLadderResonance * fastTanh(y4);

        float v, in = u;
        v = G_ * (in - s1_); in = v + s1_; s1_ = in + v;
        v = G_ * (in - s2_); in = v + s2_; s2_ = in + v;
        v = G_ * (in - s3_); in = v + s3_; s3_ = in + v;
        v = G_ * (in - s4_); in = v + s4_; s4_ = in + v;

        s1_ = flushDenormal(s1_); s2_ = flushDenormal(s2_);
        s3_ = flushDenormal(s3_); s4_ = flushDenormal(s4_);

        const float y = in * makeup_;
        if (!std::isfinite(y)) { reset(); return 0.0f; }
        return y;
    }

private:
    float sr_ = 44100.0f, nyq_ = 19845.0f, cutoffHz_ = 20000.0f;
    float G_ = 0.5f, oneMinusG_ = 0.5f, g4_ = 0.0625f, invDenom_ = 1.0f, makeup_ = 1.9f;
    float s1_ = 0.0f, s2_ = 0.0f, s3_ = 0.0f, s4_ = 0.0f;
};

// ---------------------------------------------------------------------------
// KEEP LO: a Linkwitz-Riley 4th-order crossover (24 dB/oct).
//
// WHY NOT THE OBVIOUS SUBTRACTIVE SPLIT
//   low = LP(x); high = x - low  reconstructs x exactly, which is tempting. But the high
//   band it produces is only a 6 dB/oct highpass NO MATTER how steep the lowpass is: near
//   and below the crossover its magnitude is roughly N * f / fc. Measured on this plugin,
//   a 2-pole version at a 150 Hz crossover still passed 55 Hz at -3.7 dB, so most of the
//   "removed" sub went through the distortion anyway and the kept low end was a blend of
//   clean and processed. That is not "untouched".
//
// WHAT THIS DOES INSTEAD
//   Two cascaded Butterworth (Q = 0.7071) sections per band, which is the textbook
//   Linkwitz-Riley 4th order. It gives 24 dB/oct on both sides - the same 55 Hz leak is
//   now about -35 dB - and LR4's two bands sum to an allpass, so the magnitude response
//   of low + high is flat and no band needs its polarity inverted (which LR2 would).
//   Phase is not preserved, magnitude is; for a crossover feeding a distortion stage that
//   is the right trade.
//
//   Coefficients cost sin/cos: computed per block, never per sample.
// ---------------------------------------------------------------------------
class LinkwitzRiley4
{
public:
    void setSampleRate(double sr) { sr_ = sr; setCutoff(hz_); }
    void setCutoff(float hz)
    {
        hz_ = clamp(hz, 10.0f, 0.45f * (float)sr_);
        const BiquadCoeffs lp = BiquadCoeffs::lowpass (sr_, hz_, 0.7071068f);
        const BiquadCoeffs hp = BiquadCoeffs::highpass(sr_, hz_, 0.7071068f);
        lp1_.setCoeffs(lp); lp2_.setCoeffs(lp);
        hp1_.setCoeffs(hp); hp2_.setCoeffs(hp);
    }
    void reset() { lp1_.reset(); lp2_.reset(); hp1_.reset(); hp2_.reset(); }

    void process(float x, float& low, float& high)
    {
        low  = lp2_.process(lp1_.process(x));
        high = hp2_.process(hp1_.process(x));
        if (!std::isfinite(low))  { low = 0.0f;  lp1_.reset(); lp2_.reset(); }
        if (!std::isfinite(high)) { high = 0.0f; hp1_.reset(); hp2_.reset(); }
    }

private:
    Biquad lp1_, lp2_, hp1_, hp2_;
    double sr_ = 44100.0;
    float hz_ = 20.0f;
};

} // namespace di

#endif // DI_LADDER_H
