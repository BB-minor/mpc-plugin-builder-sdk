// damage.h - the DAMAGE stage: three drum-voiced saturation algorithms.
//
// SPDX-License-Identifier: MIT
//
// Three algorithms share two character controls whose LABELS are fixed (SHAPE and GRAIN)
// but whose FUNCTION changes with TYPE. The sublabel shown on screen comes from the
// plugin's formatValue(), so the screen always says what the knob is currently doing.
//
//   TYPE        SHAPE                                  GRAIN
//   ---------   ------------------------------------   --------------------------------
//   Tube        "Focus": a mid contour applied BEFORE   "Heat": DC bias / asymmetry into
//               the waveshaper and inverted after it,   the shaper, which generates even
//               so it sets which band drives the        harmonics - the "fat" kind of
//               shaper hardest rather than acting as    distortion rather than the harsh
//               a tone control. Centre = flat.          kind.
//   Tape        "Reel": tape-speed macro. Lower = more  "Squeeze": level-dependent
//               HF rolloff, more low-mid head bump,     compression that rounds the
//               more flutter. Higher = flatter,         transient before the shaper.
//               brighter, tighter.
//   Overdrive   "Slope": broad brightness tilt of the   "Bite": narrow upper-mid / HF
//               clipped signal (post-clip shelf pair).  emphasis around 3.2 kHz, after
//                                                       the clipper.
//
// DRIVE is a parallel blend with the clean signal as well as an input gain, so at DRIVE 0
// the output is bit-identical to the input and the whole stage is effectively bypassed -
// which is what makes "Body alone" work.
//
// Waveshapers are the SDK's own: fastTanh (Pade tanh, soft, for Tube and Tape) and
// softClip (cubic, harder knee, for Overdrive), both from dsp/math.h. Filters are RBJ
// biquads from dsp/biquad.h with coefficients computed per block (they cost sin/cos/pow),
// never per sample.
//
// Every coefficient is clamped before use and the output is NaN-checked: this runs inside
// the MPC's own process with no sandbox.

#ifndef DI_DAMAGE_H
#define DI_DAMAGE_H

#include "dsp/math.h"
#include "dsp/biquad.h"
#include "dsp/onepole.h"
#include "dsp/delay_line.h"
#include "dsp/lfo.h"
#include "dsp/smoother.h"

#include "env_follower.h"

#include <cmath>

namespace di {

using namespace mpc::dsp;

// The wet path always runs through the flutter delay line so that switching TYPE never
// changes the plugin's latency. The nominal read offset is a fixed sample count; the
// plugin reports it as latency and delays its own dry path to match, so the global MIX
// stays phase-coherent. Only Tape modulates around it.
//
// NOTE on the +1: DelayLine::readLinear(d) returns the sample written d-1 calls ago
// (read offset 1 is the sample just written), so a delay of exactly N samples needs a
// read offset of N+1. Both this stage and the plugin's dry path use kFlutterRead, so the
// real delay equals kFlutterNominal and matches the latency reported to the host.
constexpr int   kFlutterNominal = 64;     // samples of actual delay (1.45 ms at 44.1 kHz)
constexpr float kFlutterRead    = 65.0f;  // = kFlutterNominal + 1, the read offset
constexpr float kFlutterMaxDepth = 11.0f; // samples (~0.25 ms) at the slowest reel

class DamageStage
{
public:
    enum Type { kTube = 0, kTape, kOverdrive, kNumTypes };
    static const char* typeName(int t)
    {
        static const char* const n[kNumTypes] = { "Tube", "Tape", "Overdrive" };
        return n[clamp(t, 0, kNumTypes - 1)];
    }
    // The sublabel under SHAPE / GRAIN for the current type (shown on the MPC screen).
    static const char* shapeName(int t)
    {
        static const char* const n[kNumTypes] = { "Focus", "Reel", "Slope" };
        return n[clamp(t, 0, kNumTypes - 1)];
    }
    static const char* grainName(int t)
    {
        static const char* const n[kNumTypes] = { "Heat", "Squeeze", "Bite" };
        return n[clamp(t, 0, kNumTypes - 1)];
    }

    void prepare(double sampleRate)
    {
        sr_ = sampleRate;
        const int maxDelay = (int)(sampleRate * 0.01) + 4;   // 10 ms is ample for flutter
        for (int c = 0; c < 2; ++c)
        {
            flutter_[c].prepare(maxDelay);
            dc_[c].prepare(sampleRate, 12.0f);
            squeeze_[c].setTimes(3.0f, 90.0f, sampleRate);
        }
        wow_.setSampleRate(sampleRate);
        flut_.setSampleRate(sampleRate);
        wow_.setShape(LFO::Shape::Sine);
        flut_.setShape(LFO::Shape::Triangle);
        wow_.setFrequency(0.6f);
        flut_.setFrequency(6.5f);
        drive_.setTime(20.0f, sampleRate);
        bias_.setTime(25.0f, sampleRate);
        trim_.setTime(20.0f, sampleRate);
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            flutter_[c].reset();
            fA_[c].reset(); fB_[c].reset(); fC_[c].reset();
            lp_[c].reset(); dc_[c].reset();
            squeeze_[c].reset();
        }
        wow_.reset(0.0f);
        flut_.reset(0.25f);
        drive_.snap(drive_.target());
        bias_.snap(bias_.target());
        trim_.snap(trim_.target());
    }

    // Once per block. shape is -1..1 (centre = flat), grain and drive are 0..1.
    void setBlockParams(int type, float drive01, float shape, float grain01)
    {
        type = clamp(type, 0, kNumTypes - 1);
        drive01 = clamp(drive01, 0.0f, 1.0f);
        shape = clamp(shape, -1.0f, 1.0f);
        grain01 = clamp(grain01, 0.0f, 1.0f);

        if (type != type_)
        {
            // A different algorithm uses the filter slots for different jobs, so stale
            // state would ring out as a pop. Clearing floats is realtime safe. A discrete
            // algorithm switch is audible by nature; this keeps it to one click.
            for (int c = 0; c < 2; ++c) { fA_[c].reset(); fB_[c].reset(); fC_[c].reset(); lp_[c].reset(); }
            type_ = type;
        }

        amount_ = drive01;                       // parallel blend: 0 = clean bypass
        grain_ = grain01;

        // Input gain into the shaper, per algorithm. Tube is gentlest, Overdrive hardest.
        static const float kDriveGain[kNumTypes] = { 22.0f, 14.0f, 30.0f };
        static const float kTrimDb[kNumTypes]    = {  8.0f,  6.0f, 10.0f };
        const float g = 1.0f + drive01 * kDriveGain[type_];
        drive_.setTarget(std::isfinite(g) ? g : 1.0f);
        // Post-trim keeps headroom going into the resonators: a hot distorted signal into
        // high-Q bands is exactly what spikes. Auto-makeup restores the level later.
        trim_.setTarget(dbToGain(-drive01 * kTrimDb[type_]));

        const float nyq = 0.49f * (float)sr_;

        switch (type_)
        {
            case kTube:
            {
                // Focus: +g into the shaper at 700 Hz, exactly -g after it. An RBJ peaking
                // cut is the reciprocal of the boost, so the pair is magnitude-flat and
                // only the distribution of drive across the spectrum changes.
                const float focusDb = shape * 9.0f;
                const BiquadCoeffs pre  = BiquadCoeffs::peak(sr_, clamp(700.0f, 20.0f, nyq), 0.8f,  focusDb);
                const BiquadCoeffs post = BiquadCoeffs::peak(sr_, clamp(700.0f, 20.0f, nyq), 0.8f, -focusDb);
                for (int c = 0; c < 2; ++c) { fA_[c].setCoeffs(pre); fB_[c].setCoeffs(post); }
                bias_.setTarget(grain01 * 0.8f);          // Heat: asymmetry -> even harmonics
                flutterDepth_ = 0.0f;
                for (int c = 0; c < 2; ++c) lp_[c].setCutoff(nyq, sr_);
                break;
            }
            case kTape:
            {
                const float speed = 0.5f * (shape + 1.0f);          // 0 = slow reel, 1 = fast
                // Head bump: a low-mid lift that grows as the reel slows down.
                const BiquadCoeffs bump = BiquadCoeffs::peak(sr_, clamp(95.0f, 20.0f, nyq), 0.9f,
                                                             (1.0f - speed) * 4.5f);
                for (int c = 0; c < 2; ++c)
                {
                    fA_[c].setCoeffs(bump);
                    fB_[c].setCoeffs(BiquadCoeffs::identity());
                    // HF loss after the saturation, so the harmonics get rolled off too.
                    lp_[c].setCutoff(clamp(2800.0f * std::pow(6.43f, speed), 500.0f, nyq), sr_);
                }
                bias_.setTarget(0.0f);
                // Scaled by drive01: flutter is part of DAMAGE, so DRIVE 0 must leave the
                // signal untouched. Without this the delay is still being modulated at
                // DRIVE 0 and Tape differs from Tube, which is not a bypass.
                flutterDepth_ = (1.0f - speed) * kFlutterMaxDepth * drive01;
                break;
            }
            case kOverdrive:
            default:
            {
                // Slope: a shelf pair at 800 Hz tilts the clipped signal dark <-> bright.
                const float tiltDb = shape * 6.0f;
                const BiquadCoeffs lo = BiquadCoeffs::lowShelf (sr_, clamp(800.0f, 20.0f, nyq), 0.707f, -tiltDb);
                const BiquadCoeffs hi = BiquadCoeffs::highShelf(sr_, clamp(800.0f, 20.0f, nyq), 0.707f,  tiltDb);
                const BiquadCoeffs bite = BiquadCoeffs::peak(sr_, clamp(3200.0f, 20.0f, nyq), 1.3f, grain01 * 9.0f);
                for (int c = 0; c < 2; ++c) { fA_[c].setCoeffs(lo); fB_[c].setCoeffs(hi); fC_[c].setCoeffs(bite); }
                bias_.setTarget(0.0f);
                flutterDepth_ = 0.0f;
                for (int c = 0; c < 2; ++c) lp_[c].setCutoff(nyq, sr_);
                break;
            }
        }
    }

    // One stereo frame. Returns false if a non-finite value had to be suppressed.
    bool processFrame(float inL, float inR, float& outL, float& outR)
    {
        // Flutter / latency alignment. Modulation is shared by both channels so a nearly
        // mono drum bus does not smear into a phasey stereo image.
        const float wow = wow_.process();
        const float flt = flut_.process();
        const float offset = kFlutterRead + flutterDepth_ * (0.7f * wow + 0.3f * flt);
        const float d = clamp(offset, 1.0f, (float)flutter_[0].maxDelay() - 1.0f);

        flutter_[0].write(inL);
        flutter_[1].write(inR);
        const float dlyL = flutter_[0].readLinear(d);
        const float dlyR = flutter_[1].readLinear(d);

        const float g    = drive_.process();
        const float bias = bias_.process();
        const float trim = trim_.process();

        float wetL = shape(0, dlyL, g, bias);
        float wetR = shape(1, dlyR, g, bias);
        wetL *= trim;
        wetR *= trim;

        // DRIVE as a parallel blend: at 0 the output is exactly the (delayed) input.
        outL = dlyL + (wetL - dlyL) * amount_;
        outR = dlyR + (wetR - dlyR) * amount_;

        if (!std::isfinite(outL) || !std::isfinite(outR))
        {
            outL = outR = 0.0f;
            return false;
        }
        return true;
    }

    void panic()
    {
        for (int c = 0; c < 2; ++c)
        {
            flutter_[c].reset();
            fA_[c].reset(); fB_[c].reset(); fC_[c].reset();
            lp_[c].reset(); dc_[c].reset();
        }
    }

    int type() const { return type_; }

private:
    float shape(int c, float x, float g, float bias)
    {
        switch (type_)
        {
            case kTube:
            {
                const float pre = fA_[c].process(x);                 // Focus: into the shaper
                // Asymmetric tanh. Subtracting tanh(bias) removes the DC the bias adds;
                // the DC blocker catches whatever the subtraction leaves behind.
                float y = fastTanh(pre * g + bias) - fastTanh(bias);
                y = fB_[c].process(y);                               // Focus: inverse, after
                return dc_[c].process(y);
            }
            case kTape:
            {
                float y = fA_[c].process(x);                         // head bump
                // Squeeze: level-dependent gain reduction, rounding the transient.
                const float e = squeeze_[c].process(y);
                const float gr = 1.0f - grain_ * (e / (e + 0.25f)) * 0.65f;
                y = fastTanh(y * g * clamp(gr, 0.2f, 1.0f));
                return lp_[c].process(y);                            // Reel: HF loss, post
            }
            case kOverdrive:
            default:
            {
                float y = softClip(x * g);                           // cubic, harder knee
                y = fA_[c].process(y);                               // Slope: low shelf
                y = fB_[c].process(y);                               // Slope: high shelf
                return fC_[c].process(y);                            // Bite
            }
        }
    }

    DelayLine   flutter_[2];
    Biquad      fA_[2], fB_[2], fC_[2];
    OnePoleLP   lp_[2];
    DCBlocker   dc_[2];
    EnvFollower squeeze_[2];
    LFO         wow_, flut_;
    Smoother    drive_, bias_, trim_;

    double sr_ = 44100.0;
    int   type_ = kTube;
    float amount_ = 0.0f;
    float grain_ = 0.0f;
    float flutterDepth_ = 0.0f;
};

} // namespace di

#endif // DI_DAMAGE_H
