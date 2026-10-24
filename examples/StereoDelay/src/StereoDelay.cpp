// StereoDelay.cpp - tempo-synced / free stereo delay with filtered feedback and ping-pong.
//
// SPDX-License-Identifier: MIT
//
// What this example demonstrates:
//   [1] an EFFECT (kind "effect", 2 in / 2 out, no MIDI)
//   [2] the Sync On/Off + Division + free Time pattern, using ctx.transport.bpm
//   [3] allocating delay memory ONLY in prepare() (DelayLine::prepare)
//   [4] smoothing the delay time so changes glide instead of click (tape-style pitch wobble)
//   [5] filters and a soft clipper inside the feedback loop so it can never blow up
//   [6] ping-pong routing (L feeds R, R feeds L)
//
// Signal path:
//   in -> [+ feedback] -> delay -> lowcut -> highcut -> softclip -> (wet, and back to feedback)
//   out = dry * (1 - mix) + wet * mix

#include "mpc_plugin.h"
#include "dsp/math.h"
#include "dsp/delay_line.h"
#include "dsp/onepole.h"
#include "dsp/smoother.h"
#include "dsp/tempo.h"

using namespace mpc::dsp;

class StereoDelay : public mpc::Plugin
{
public:
    enum Params { kSync, kDivision, kTime, kFeedback, kLowCut, kHighCut, kPingPong, kMix, kNumParams };

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "Stereo Delay";
        i.vendor = "MPC SDK Examples";
        i.uniqueId = MPC_FOURCC('M', 'x', 'S', 'd');   // == "uid" in plugin.json
        i.version = 1000;
        i.isInstrument = false;   // [1] effect
        i.numInputs = 2;
        i.numOutputs = 2;
        i.tailSamples = 0;        // unknown (depends on feedback)
        return i;
    }

    StereoDelay() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;
        addParam(choiceParam("sync",  "Sync", {"Off", "On"}, 1));                              // [2]
        addParam(choiceParam("div",   "Division", divisionNames(), kDivisionEighth));
        addParam(logParam  ("time",   "Time", 1.0f, 2000.0f, 350.0f, "ms", 0));
        addParam(floatParam("fb",     "Feedback", 0.0f, 100.0f, 40.0f, "%", 0));
        addParam(logParam  ("lowcut", "Low Cut", 20.0f, 2000.0f, 120.0f, "Hz", 0));
        addParam(logParam  ("highcut","High Cut", 500.0f, 18000.0f, 6000.0f, "Hz", 0));
        addParam(boolParam ("pingpong","Ping Pong", true));
        addParam(floatParam("mix",    "Mix", 0.0f, 100.0f, 35.0f, "%", 0));
        if (numParams() != kNumParams) mpc::log("StereoDelay: parameter count mismatch");
    }

    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        // [3] The only allocation in the plugin: room for kMaxDelaySeconds at this sample rate.
        const int maxSamples = (int)(sampleRate * kMaxDelaySeconds) + 1;
        delayL_.prepare(maxSamples);
        delayR_.prepare(maxSamples);
        time_.setTime(80.0f, sampleRate);      // [4] ~80 ms glide between delay times
        mix_.setTime(20.0f, sampleRate);
        feedback_.setTime(20.0f, sampleRate);
    }

    void reset() override
    {
        delayL_.reset(); delayR_.reset();
        lowCutL_.reset(); lowCutR_.reset(); highCutL_.reset(); highCutR_.reset();
        time_.snap(time_.target());
        mix_.snap(get(kMix) * 0.01f);
        feedback_.snap(get(kFeedback) * 0.01f);
    }

    void process(mpc::ProcessContext& ctx) override
    {
        // ----- per block: read parameters, compute targets ------------------
        float ms;
        if (getInt(kSync) == 1)   // [2] tempo-synced: beats -> seconds using the host tempo
            ms = beatsToSeconds(divisionBeats(getInt(kDivision)), ctx.transport.bpm) * 1000.0f;
        else
            ms = get(kTime);
        const float maxDelay = (float)delayL_.maxDelay();
        time_.setTarget(clamp(msToSamples(ms, ctx.sampleRate), 1.0f, maxDelay));
        mix_.setTarget(get(kMix) * 0.01f);
        feedback_.setTarget(get(kFeedback) * 0.01f);
        lowCutL_.setCutoff(get(kLowCut), ctx.sampleRate);   lowCutR_.setCutoff(get(kLowCut), ctx.sampleRate);
        highCutL_.setCutoff(get(kHighCut), ctx.sampleRate); highCutR_.setCutoff(get(kHighCut), ctx.sampleRate);
        const bool pingPong = getBool(kPingPong);

        const float* inL = ctx.inputs[0];
        const float* inR = ctx.numInputs > 1 ? ctx.inputs[1] : ctx.inputs[0];
        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : ctx.outputs[0];

        // ----- per sample -----------------------------------------------------
        for (int i = 0; i < ctx.numFrames; ++i)
        {
            const float d = time_.process();
            const float fb = feedback_.process();
            const float mix = mix_.process();
            const float dryL = inL[i], dryR = inR[i];

            // Read the delayed signal first (reading before writing keeps a 1-sample minimum delay).
            float wetL = delayL_.readLinear(d);
            float wetR = delayR_.readLinear(d);

            // [5] Tame the loop: filter, then soft clip so feedback >= 100% cannot explode.
            wetL = softClip(highCutL_.process(lowCutL_.process(wetL)));
            wetR = softClip(highCutR_.process(lowCutR_.process(wetR)));

            if (pingPong)   // [6] mono input into L, L's output feeds R, R's output feeds L
            {
                const float mono = 0.5f * (dryL + dryR);
                delayL_.write(mono + wetR * fb);
                delayR_.write(wetL * fb);
            }
            else            // two independent delays
            {
                delayL_.write(dryL + wetL * fb);
                delayR_.write(dryR + wetR * fb);
            }

            outL[i] = dryL + (wetL - dryL) * mix;
            outR[i] = dryR + (wetR - dryR) * mix;
        }
    }

private:
    static constexpr float kMaxDelaySeconds = 4.0f;   // covers 1/1 at 60 bpm, or 2000 ms free

    DelayLine delayL_, delayR_;
    OnePoleHP lowCutL_, lowCutR_;
    OnePoleLP highCutL_, highCutR_;
    Smoother time_, mix_, feedback_;
};

MPC_PLUGIN_ENTRY(StereoDelay)
