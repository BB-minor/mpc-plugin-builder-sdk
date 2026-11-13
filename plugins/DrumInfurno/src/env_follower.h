// env_follower.h - rectify + one-pole with separate attack and release coefficients.
//
// SPDX-License-Identifier: MIT
//
// Lifted unchanged from plugins/ResoBody (where it drives Bloom); it lives in its own
// header here because the Damage stage's "Squeeze" tape compression needs the same thing.
// The SDK has no envelope-follower class, so this is built from onePoleCoeff() and
// flushDenormal() in dsp/math.h rather than hand-rolled filter maths.
//
// Allocation free: two multiplies and a compare per sample.

#ifndef DI_ENV_FOLLOWER_H
#define DI_ENV_FOLLOWER_H

#include "dsp/math.h"

#include <cmath>

namespace di {

using namespace mpc::dsp;

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

} // namespace di

#endif // DI_ENV_FOLLOWER_H
