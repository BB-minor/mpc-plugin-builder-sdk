// DrumSynth.cpp - an 8-voice analogue-style drum synthesizer with multiple outputs.
//
// SPDX-License-Identifier: MIT
//
// What this example demonstrates:
//   [1] a MULTI-OUT instrument: numOutputs = 4 (Main L/R = 0,1 and Aux L/R = 2,3) with a
//       "Routing" choice that decides which drums go to which stereo pair
//   [2] one-shot drum voices mapped to fixed MIDI notes (36..43), with choke groups
//   [3] drum synthesis recipes: sine + pitch envelope (kick/toms), filtered noise (snare/clap),
//       stacked squares through a highpass (hats/cowbell)
//   [4] building many similar parameters in a loop and finding them again by formula
//   [5] a smoothed master volume
//
// MIDI note map: 36 Kick, 37 Snare, 38 Clap, 39 Closed Hat, 40 Open Hat, 41 Low Tom, 42 High Tom, 43 Perc
// (other notes wrap onto the same eight: 44 = Kick again, 35 = Perc).

#include "mpc_plugin.h"
#include "dsp/math.h"
#include "dsp/oscillator.h"
#include "dsp/noise.h"
#include "dsp/svf.h"
#include "dsp/onepole.h"
#include "dsp/smoother.h"

using namespace mpc::dsp;

class DrumSynth : public mpc::Plugin
{
public:
    enum DrumType { kKick = 0, kSnare, kClap, kClosedHat, kOpenHat, kLowTom, kHighTom, kPerc, kNumDrums };
    static constexpr int kFirstNote = 36;
    enum VoiceParam { kTune = 0, kDecay, kLevel, kParamsPerVoice };
    // [4] Parameter index = drum * kParamsPerVoice + which. The global ones come after.
    static int paramIndex(int drum, VoiceParam which) { return drum * kParamsPerVoice + which; }
    enum GlobalParams { kRouting = kNumDrums * kParamsPerVoice, kMaster, kNumParams };

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "Drum Synth";
        i.vendor = "MPC SDK Examples";
        i.uniqueId = MPC_FOURCC('M', 'x', 'D', 'r');   // == "uid" in plugin.json
        i.version = 1000;
        i.isInstrument = true;
        i.numInputs = 0;
        i.numOutputs = 4;          // [1] two stereo pairs
        return i;
    }

    DrumSynth() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;
        static const char* const shortNames[kNumDrums] = {"Kick", "Snare", "Clap", "CHat", "OHat", "LTom", "HTom", "Perc"};
        static const char* const ids[kNumDrums]        = {"kick", "snare", "clap", "chat", "ohat", "ltom", "htom", "perc"};
        for (int d = 0; d < kNumDrums; ++d)
        {
            // Names must be short (<= 12 chars) to fit the MPC screen: "Snare Decay" is 11.
            std::string tuneId = std::string(ids[d]) + "_tune", decId = std::string(ids[d]) + "_decay", lvlId = std::string(ids[d]) + "_level";
            std::string tuneName = std::string(shortNames[d]) + " Tune", decName = std::string(shortNames[d]) + " Decay", lvlName = std::string(shortNames[d]) + " Level";
            addParam(floatParam(tuneId.c_str(), tuneName.c_str(), -12.0f, 12.0f, 0.0f, "st", 1));
            addParam(floatParam(decId.c_str(),  decName.c_str(), 0.0f, 100.0f, 50.0f, "%", 0));
            addParam(floatParam(lvlId.c_str(),  lvlName.c_str(), 0.0f, 100.0f, 80.0f, "%", 0));
        }
        addParam(choiceParam("routing", "Routing", {"All Main", "Hats>Aux", "Split"}, 0));
        addParam(floatParam("master", "Master", -40.0f, 6.0f, -6.0f, "dB", 1));
        if (numParams() != kNumParams) mpc::log("DrumSynth: parameter count mismatch");

        // Two kits as built-in programs.
        std::vector<float> v((size_t)numParams());
        for (int i = 0; i < numParams(); ++i) v[(size_t)i] = param(i).def;
        addProgram("Clean Kit", v);
        v[(size_t)paramIndex(kKick, kDecay)] = 85; v[(size_t)paramIndex(kKick, kTune)] = -3;
        v[(size_t)paramIndex(kSnare, kDecay)] = 65; v[(size_t)paramIndex(kSnare, kTune)] = -2;
        v[(size_t)paramIndex(kClosedHat, kDecay)] = 30; v[(size_t)paramIndex(kOpenHat, kDecay)] = 70;
        v[(size_t)paramIndex(kPerc, kLevel)] = 60;
        addProgram("Boom Kit", v);
    }

    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        sr_ = sampleRate;
        for (int d = 0; d < kNumDrums; ++d) voices_[d].prepare((DrumType)d, sampleRate, (uint32_t)(d + 1) * 7919u);
        master_.setTime(10.0f, sampleRate);
        master_.snap(dbToGain(get(kMaster)));
    }

    void reset() override
    {
        for (auto& v : voices_) v.kill();
        master_.snap(dbToGain(get(kMaster)));
    }

    void process(mpc::ProcessContext& ctx) override
    {
        routing_ = getInt(kRouting);
        master_.setTarget(dbToGain(get(kMaster)));

        int pos = 0;
        for (int e = 0; e < ctx.numMidiEvents; ++e)   // sample-accurate triggering
        {
            const mpc::MidiEvent& ev = ctx.midi[e];
            if (ev.sampleOffset > pos) { render(ctx, pos, ev.sampleOffset); pos = ev.sampleOffset; }
            handleMidi(ev);
        }
        render(ctx, pos, ctx.numFrames);

        // [5] master volume on every output
        for (int i = 0; i < ctx.numFrames; ++i)
        {
            const float g = master_.process();
            for (int c = 0; c < ctx.numOutputs; ++c) ctx.outputs[c][i] *= g;
        }
    }

private:
    // ----- one drum voice ----------------------------------------------------
    struct DrumVoice
    {
        DrumType type = kKick;
        double sr = 44100.0;
        bool active = false;
        float gain = 0.0f, pan = 0.0f;
        float amp = 0.0f, ampCoef = 0.0f;         // exponential amplitude decay
        float pitchEnv = 0.0f, pitchCoef = 0.0f;  // exponential pitch decay (0..1)
        float noiseEnv = 0.0f, noiseCoef = 0.0f;  // separate noise decay
        float baseHz = 50.0f;
        WavetableSine sine, sine2;
        WhiteNoise noise;
        SVF filter;
        OnePoleHP hp;
        float phases[6] = {0, 0, 0, 0, 0, 0}, incs[6] = {0, 0, 0, 0, 0, 0};
        int clapBurstsLeft = 0, clapTimer = 0;

        void prepare(DrumType t, double sampleRate, uint32_t seed)
        {
            type = t; sr = sampleRate;
            sine.setSampleRate(sr); sine2.setSampleRate(sr);
            filter.setSampleRate(sr);
            noise.reseed(seed);
            static const float pans[kNumDrums] = {0.0f, 0.0f, 0.2f, -0.3f, 0.3f, -0.35f, 0.35f, 0.45f};
            pan = pans[t];
            kill();
        }
        void kill() { active = false; amp = 0.0f; filter.reset(); hp.reset(); }

        // Coefficient for an exponential decay that falls 60 dB in `ms` (what players expect from a "decay" knob).
        float decayCoef(float ms) const { return std::exp(-6.9078f / (0.001f * ms * (float)sr)); }

        // tune in semitones, decay 0..1, velocity 0..1
        void trigger(float tune, float decay, float velocity, float level)
        {
            active = true;
            amp = 1.0f; pitchEnv = 1.0f;
            noiseEnv = (type == kKick || type == kSnare || type == kClap || type == kLowTom || type == kHighTom) ? 1.0f : 0.0f;
            gain = level * (0.2f + 0.8f * velocity);
            const float ratio = semitonesToRatio(tune);
            switch (type)
            {
                case kKick:
                    baseHz = 48.0f * ratio;
                    ampCoef = decayCoef(lerp(150.0f, 1500.0f, decay));
                    pitchCoef = decayCoef(90.0f);
                    noiseCoef = decayCoef(8.0f);        // the click
                    sine.setFrequency(baseHz); sine.resetPhase();
                    break;
                case kSnare:
                    baseHz = 185.0f * ratio;
                    ampCoef = decayCoef(lerp(120.0f, 500.0f, decay));
                    noiseCoef = decayCoef(lerp(150.0f, 900.0f, decay));
                    pitchCoef = decayCoef(50.0f);
                    sine.setFrequency(baseHz); sine2.setFrequency(baseHz * 1.6f);
                    sine.resetPhase(); sine2.resetPhase();
                    hp.setCutoff(1800.0f, sr);
                    break;
                case kClap:
                    ampCoef = decayCoef(20.0f);                       // each burst
                    noiseCoef = decayCoef(lerp(150.0f, 800.0f, decay)); // final tail
                    clapBurstsLeft = 3; clapTimer = (int)(0.011f * (float)sr);
                    filter.set(1100.0f * ratio, 0.55f);
                    break;
                case kClosedHat:
                case kOpenHat:
                    ampCoef = decayCoef(type == kClosedHat ? lerp(40.0f, 350.0f, decay) : lerp(250.0f, 2000.0f, decay));
                    setMetal(40.0f * ratio);
                    filter.set(7500.0f, 0.3f);
                    hp.setCutoff(9000.0f, sr);
                    break;
                case kLowTom:
                case kHighTom:
                    baseHz = (type == kLowTom ? 95.0f : 160.0f) * ratio;
                    ampCoef = decayCoef(lerp(200.0f, 1200.0f, decay));
                    pitchCoef = decayCoef(150.0f);
                    noiseCoef = decayCoef(40.0f);
                    sine.setFrequency(baseHz); sine.resetPhase();
                    break;
                case kPerc:
                default:
                    ampCoef = decayCoef(lerp(80.0f, 800.0f, decay));
                    incs[0] = 587.0f * ratio / (float)sr; incs[1] = 845.0f * ratio / (float)sr;
                    phases[0] = phases[1] = 0.0f;
                    filter.set(1200.0f * ratio, 0.35f);
                    break;
            }
        }

        void setMetal(float base)   // six inharmonic squares, classic hat recipe
        {
            static const float ratios[6] = {2.0f, 3.0f, 4.16f, 5.43f, 6.79f, 8.21f};
            for (int k = 0; k < 6; ++k) { incs[k] = base * ratios[k] * 2.5f / (float)sr; }
        }
        float metal(int n)
        {
            float s = 0.0f;
            for (int k = 0; k < n; ++k)
            {
                s += phases[k] < 0.5f ? 1.0f : -1.0f;
                phases[k] += incs[k]; if (phases[k] >= 1.0f) phases[k] -= 1.0f;
            }
            return s / (float)n;
        }

        // Returns one mono sample; sets active = false when the sound has died away.
        float process()
        {
            float y = 0.0f;
            switch (type)
            {
                case kKick:
                {
                    sine.setFrequency(baseHz * (1.0f + 6.0f * pitchEnv));  // [3] pitch sweep
                    y = fastTanh(sine.process() * 1.8f) * amp + noise.process() * noiseEnv * 0.4f;
                    pitchEnv *= pitchCoef; noiseEnv *= noiseCoef; amp *= ampCoef;
                    break;
                }
                case kSnare:
                {
                    sine.setFrequency(baseHz * (1.0f + 0.8f * pitchEnv));
                    const float body = (sine.process() * 0.7f + sine2.process() * 0.3f) * amp;
                    const float n = hp.process(noise.process()) * noiseEnv;
                    y = body * 0.6f + n * 0.9f;
                    pitchEnv *= pitchCoef; noiseEnv *= noiseCoef; amp *= ampCoef;
                    break;
                }
                case kClap:
                {
                    const float n = filter.process(noise.process(), SVF::Mode::Bandpass) * 2.5f;
                    if (clapBurstsLeft > 0)
                    {
                        y = n * amp; amp *= ampCoef;
                        if (--clapTimer <= 0) { amp = 1.0f; clapTimer = (int)(0.011f * (float)sr); --clapBurstsLeft; }
                    }
                    else { y = n * noiseEnv; noiseEnv *= noiseCoef; amp = noiseEnv; }
                    break;
                }
                case kClosedHat:
                case kOpenHat:
                {
                    const float m = metal(6);
                    y = hp.process(filter.process(m, SVF::Mode::Highpass)) * amp * 1.5f;
                    amp *= ampCoef;
                    break;
                }
                case kLowTom:
                case kHighTom:
                {
                    sine.setFrequency(baseHz * (1.0f + 1.2f * pitchEnv));
                    y = fastTanh(sine.process() * 1.3f) * amp + noise.process() * noiseEnv * 0.15f;
                    pitchEnv *= pitchCoef; noiseEnv *= noiseCoef; amp *= ampCoef;
                    break;
                }
                case kPerc:
                default:
                {
                    const float m = metal(2);
                    y = fastTanh(filter.process(m, SVF::Mode::Bandpass) * 3.0f) * amp;
                    amp *= ampCoef;
                    break;
                }
            }
            if (amp < 0.001f && noiseEnv < 0.001f) active = false;   // -60 dB: done, free the CPU
            return y * gain;
        }
    };

    void handleMidi(const mpc::MidiEvent& ev)
    {
        if (ev.isNoteOn())
        {
            // [2] 36..43 is the canonical map; every other note wraps onto it (so pad bank B, or a
            //     keyboard anywhere, still plays the kit instead of silence).
            const int d = (((ev.note() - kFirstNote) % kNumDrums) + kNumDrums) % kNumDrums;
            if (d == kClosedHat) voices_[kOpenHat].kill();            // choke group
            voices_[d].trigger(get(paramIndex(d, kTune)), get(paramIndex(d, kDecay)) * 0.01f,
                               (float)ev.velocity() / 127.0f, get(paramIndex(d, kLevel)) * 0.01f);
        }
        else if (ev.isController() && ev.controller() == 120)        // all sound off
        {
            for (auto& v : voices_) v.kill();
        }
        // note offs are ignored: drums are one-shots
    }

    // Which stereo pair (0 = main, 1 = aux) does a drum go to under the current routing?
    int busFor(int drum) const
    {
        if (routing_ == 1) return (drum == kClosedHat || drum == kOpenHat) ? 1 : 0;
        if (routing_ == 2) return (drum >= kClosedHat) ? 1 : 0;
        return 0;
    }

    void render(mpc::ProcessContext& ctx, int from, int to)
    {
        const bool haveAux = ctx.numOutputs >= 4;
        for (int d = 0; d < kNumDrums; ++d)
        {
            DrumVoice& v = voices_[d];
            if (!v.active) continue;
            const int bus = haveAux ? busFor(d) : 0;     // [1] fall back to main if the host gave us 2 outs
            float* L = ctx.outputs[bus * 2];
            float* R = ctx.outputs[bus * 2 + 1];
            float gl, gr; equalPowerPan(v.pan, gl, gr);
            for (int i = from; i < to && v.active; ++i)
            {
                const float y = v.process();
                L[i] += y * gl;
                R[i] += y * gr;
            }
        }
    }

    double sr_ = 44100.0;
    int routing_ = 0;
    DrumVoice voices_[kNumDrums];
    Smoother master_;
};

MPC_PLUGIN_ENTRY(DrumSynth)
