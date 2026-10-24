// SimpleSynth.cpp - an 8-voice subtractive synthesizer for the MPC.
//
// SPDX-License-Identifier: MIT
//
// What this example demonstrates (search for the [N] markers below):
//   [1] declaring parameters in an enum + constructor so indices never drift
//   [2] built-in programs with addProgram()
//   [3] a Voice struct built from framework/dsp pieces and a VoiceAllocator
//   [4] sample-accurate MIDI: render up to each event's offset, then handle it
//   [5] "control rate" updates: expensive maths (tan, pow) every 16 samples
//   [6] tempo-synced LFO via ctx.transport
//   [7] no allocation in process(): fixed-size scratch arrays, chunked rendering
//
// Signal path per voice:
//   osc1 + osc2 (PolyBLEP, detune, octave) -> SVF (cutoff + env + LFO + key track, drive)
//   -> amp ADSR * velocity  -> summed -> master volume -> both outputs (mono-to-stereo).

#include "mpc_plugin.h"
#include "dsp/math.h"
#include "dsp/oscillator.h"
#include "dsp/svf.h"
#include "dsp/adsr.h"
#include "dsp/lfo.h"
#include "dsp/smoother.h"
#include "dsp/voice.h"
#include "dsp/tempo.h"
#include "dsp/midi_cc.h"

using namespace mpc::dsp;

class SimpleSynth : public mpc::Plugin
{
public:
    // [1] One enum entry per parameter, in the order they are added below.
    enum Params
    {
        kOsc1Wave, kOsc1Oct, kOsc1Level, kOsc2Wave, kOsc2Oct, kOsc2Semi, kOsc2Detune, kOsc2Level,
        kPulseWidth, kGlide, kBendRange,
        kCutoff, kReso, kFilterMode, kEnvAmount, kDrive, kKeyTrack, kFAttack, kFDecay, kFSustain, kFRelease,
        kAttack, kDecay, kSustain, kRelease, kVolume, kVelSens,
        kLfoShape, kLfoRate, kLfoSync, kLfoDiv, kLfoPitch, kLfoFilter, kWheelVib,
        kNumParams
    };

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "Simple Synth";
        i.vendor = "MPC SDK Examples";
        i.uniqueId = MPC_FOURCC('M', 'x', 'S', 'y');   // must match "uid" in plugin.json
        i.version = 1000;
        i.isInstrument = true;
        i.numInputs = 0;
        i.numOutputs = 2;
        return i;
    }

    SimpleSynth() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;
        const std::vector<std::string> waves = {"Sine", "Triangle", "Saw", "Square"};   // == dsp::Wave order
        // OSC page
        addParam(choiceParam("osc1wave", "Osc1 Wave", waves, 2));
        addParam(intParam  ("osc1oct",  "Osc1 Octave", -2, 2, 0));
        addParam(floatParam("osc1lvl",  "Osc1 Level", 0.0f, 100.0f, 100.0f, "%", 0));
        addParam(choiceParam("osc2wave", "Osc2 Wave", waves, 2));
        addParam(intParam  ("osc2oct",  "Osc2 Octave", -2, 2, 0));
        addParam(intParam  ("osc2semi", "Osc2 Semi", -12, 12, 0, "st"));
        addParam(floatParam("osc2det",  "Osc2 Detune", -50.0f, 50.0f, 8.0f, "ct", 0));
        addParam(floatParam("osc2lvl",  "Osc2 Level", 0.0f, 100.0f, 60.0f, "%", 0));
        addParam(floatParam("pw",       "Pulse Width", 5.0f, 95.0f, 50.0f, "%", 0));
        addParam(floatParam("glide",    "Glide", 0.0f, 1000.0f, 0.0f, "ms", 0, 0.5f));
        addParam(intParam  ("bend",     "Bend Range", 0, 24, 2, "st"));
        // FILTER page
        addParam(logParam  ("cutoff",   "Cutoff", 20.0f, 18000.0f, 4000.0f, "Hz", 0));
        addParam(floatParam("reso",     "Resonance", 0.0f, 100.0f, 20.0f, "%", 0));
        addParam(choiceParam("fmode",   "Filter Mode", {"LP", "BP", "HP", "Notch"}, 0));
        addParam(floatParam("envamt",   "Env Amount", -100.0f, 100.0f, 40.0f, "%", 0));
        addParam(floatParam("drive",    "Drive", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("keytrk",   "Key Track", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(logParam  ("fatk",     "Filt Attack", 1.0f, 5000.0f, 5.0f, "ms", 0));
        addParam(logParam  ("fdec",     "Filt Decay", 1.0f, 5000.0f, 300.0f, "ms", 0));
        addParam(floatParam("fsus",     "Filt Sustain", 0.0f, 100.0f, 20.0f, "%", 0));
        addParam(logParam  ("frel",     "Filt Release", 1.0f, 5000.0f, 300.0f, "ms", 0));
        // AMP page
        addParam(logParam  ("atk",      "Amp Attack", 1.0f, 5000.0f, 5.0f, "ms", 0));
        addParam(logParam  ("dec",      "Amp Decay", 1.0f, 5000.0f, 200.0f, "ms", 0));
        addParam(floatParam("sus",      "Amp Sustain", 0.0f, 100.0f, 80.0f, "%", 0));
        addParam(logParam  ("rel",      "Amp Release", 1.0f, 5000.0f, 200.0f, "ms", 0));
        addParam(floatParam("vol",      "Volume", -40.0f, 6.0f, -8.0f, "dB", 1));
        addParam(floatParam("velsens",  "Velocity", 0.0f, 100.0f, 60.0f, "%", 0));
        // LFO page
        addParam(choiceParam("lfoshape", "LFO Shape", LFO::shapeNames(), 0));
        addParam(logParam  ("lforate",  "LFO Rate", 0.05f, 30.0f, 5.0f, "Hz", 2));
        addParam(boolParam ("lfosync",  "LFO Sync", false));
        addParam(choiceParam("lfodiv",  "LFO Div", divisionNames(), kDivisionQuarter));
        addParam(floatParam("lfopitch", "LFO>Pitch", 0.0f, 12.0f, 0.0f, "st", 2, 0.5f));
        addParam(floatParam("lfofilt",  "LFO>Filter", -100.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("wheelvib", "Wheel Vib", 0.0f, 12.0f, 0.5f, "st", 2, 0.5f));

        if (numParams() != kNumParams) mpc::log("SimpleSynth: parameter count mismatch (%d vs %d)", numParams(), (int)kNumParams);

        // [2] Built-in programs. Start from the defaults and override a few values.
        auto program = [this](const char* name, std::initializer_list<std::pair<int, float>> overrides)
        {
            std::vector<float> v((size_t)numParams());
            for (int i = 0; i < numParams(); ++i) v[(size_t)i] = param(i).def;
            for (const auto& o : overrides) v[(size_t)o.first] = o.second;
            addProgram(name, std::move(v));
        };
        program("Init Saw", {});
        program("Fat Bass", {{kOsc1Wave, 2}, {kOsc2Wave, 3}, {kOsc2Oct, -1}, {kOsc2Detune, 12}, {kOsc2Level, 80},
                             {kCutoff, 900}, {kReso, 35}, {kEnvAmount, 70}, {kDrive, 30}, {kFDecay, 250}, {kFSustain, 5},
                             {kDecay, 300}, {kSustain, 70}, {kRelease, 120}, {kGlide, 60}});
        program("Soft Pad", {{kOsc1Wave, 2}, {kOsc2Wave, 1}, {kOsc2Detune, -10}, {kOsc2Level, 90},
                             {kCutoff, 1500}, {kReso, 10}, {kEnvAmount, 30}, {kFAttack, 800}, {kFDecay, 1500}, {kFSustain, 50},
                             {kAttack, 700}, {kDecay, 1000}, {kSustain, 90}, {kRelease, 1200},
                             {kLfoShape, 0}, {kLfoRate, 0.4f}, {kLfoFilter, 25}, {kVolume, -10}});
        program("Pluck", {{kOsc1Wave, 3}, {kPulseWidth, 25}, {kOsc2Wave, 2}, {kOsc2Oct, 1}, {kOsc2Level, 40},
                          {kCutoff, 600}, {kReso, 30}, {kEnvAmount, 90}, {kKeyTrack, 100}, {kFDecay, 120}, {kFSustain, 0},
                          {kDecay, 400}, {kSustain, 0}, {kRelease, 150}, {kVelSens, 100}});
    }

    // ----- lifecycle --------------------------------------------------------
    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        for (auto& v : voices_)
        {
            v.osc1.setSampleRate(sampleRate); v.osc2.setSampleRate(sampleRate);
            v.filter.setSampleRate(sampleRate);
            v.ampEnv.setSampleRate(sampleRate); v.filtEnv.setSampleRate(sampleRate);
        }
        lfo_.setSampleRate(sampleRate);
        controllers_.prepare(sampleRate, 10.0f);
        volume_.setTime(10.0f, sampleRate);
        volume_.snap(dbToGain(get(kVolume)));
    }

    void reset() override
    {
        for (auto& v : voices_) { v.ampEnv.reset(); v.filtEnv.reset(); v.filter.reset(); v.note = -1; }
        alloc_.reset();
        lfo_.reset();
        controllers_.reset();
        volume_.snap(dbToGain(get(kVolume)));
    }

    // ----- audio ------------------------------------------------------------
    void process(mpc::ProcessContext& ctx) override
    {
        updateBlockSettings(ctx);

        // [4] Split the block at MIDI event offsets so notes start on the right sample.
        int pos = 0;
        for (int e = 0; e < ctx.numMidiEvents; ++e)
        {
            const mpc::MidiEvent& ev = ctx.midi[e];
            if (ev.sampleOffset > pos) { render(ctx, pos, ev.sampleOffset); pos = ev.sampleOffset; }
            handleMidi(ev);
        }
        render(ctx, pos, ctx.numFrames);

        // Mono voice mix is in outputs[0]; apply master volume and copy to the right channel.
        float* L = ctx.outputs[0];
        float* R = ctx.numOutputs > 1 ? ctx.outputs[1] : nullptr;
        for (int i = 0; i < ctx.numFrames; ++i)
        {
            const float g = volume_.process();
            L[i] *= g;
            if (R) R[i] = L[i];
        }
    }

private:
    static constexpr int kNumVoices = 8;
    static constexpr int kChunk = 64;        // [7] max samples rendered per inner loop (fixed scratch size)
    static constexpr int kCtrlInterval = 16; // [5] samples between coefficient updates

    // [3] Everything one note needs.
    struct Voice
    {
        PolyBlepOsc osc1, osc2;
        SVF filter;
        ADSR ampEnv, filtEnv;
        Smoother glide;          // smooths the MIDI note number (so glide is linear in pitch)
        int note = -1;
        float velGain = 1.0f;
        int ctrl = 0;            // control-rate counter
        float oscGain1 = 1.0f, oscGain2 = 0.0f;
    };

    // Per-block parameter snapshot (read once; parameters are atomics that the UI thread writes).
    struct Settings
    {
        int wave1, wave2, oct1, oct2, semi2, mode;
        float detune2, lvl1, lvl2, pw, glideMs, cutoff, reso, envAmt, drive, keyTrack, velSens, lfoPitch, lfoFilter, wheelVib;
    } s_{};

    void updateBlockSettings(mpc::ProcessContext& ctx)
    {
        s_.wave1 = getInt(kOsc1Wave); s_.wave2 = getInt(kOsc2Wave);
        s_.oct1 = getInt(kOsc1Oct);   s_.oct2 = getInt(kOsc2Oct); s_.semi2 = getInt(kOsc2Semi);
        s_.detune2 = get(kOsc2Detune); s_.lvl1 = get(kOsc1Level) * 0.01f; s_.lvl2 = get(kOsc2Level) * 0.01f;
        s_.pw = get(kPulseWidth) * 0.01f; s_.glideMs = get(kGlide);
        s_.cutoff = get(kCutoff); s_.reso = get(kReso) * 0.01f; s_.mode = getInt(kFilterMode);
        s_.envAmt = get(kEnvAmount) * 0.01f; s_.drive = get(kDrive) * 0.01f; s_.keyTrack = get(kKeyTrack) * 0.01f;
        s_.velSens = get(kVelSens) * 0.01f;
        s_.lfoPitch = get(kLfoPitch); s_.lfoFilter = get(kLfoFilter) * 0.01f; s_.wheelVib = get(kWheelVib);

        alloc_.setBendRange((float)getInt(kBendRange));
        volume_.setTarget(dbToGain(get(kVolume)));

        // [6] LFO: free running in Hz, or locked to the song position when synced and playing.
        lfo_.setShape(getInt(kLfoShape));
        if (getBool(kLfoSync))
        {
            lfo_.setTempoSync(ctx.transport.bpm, divisionBeats(getInt(kLfoDiv)));
            if (ctx.transport.playing && ctx.transport.ppqValid) lfo_.syncToPpq(ctx.transport.ppqPos);
        }
        else lfo_.setFrequency(get(kLfoRate));

        const float fa = get(kFAttack), fd = get(kFDecay), fs = get(kFSustain) * 0.01f, fr = get(kFRelease);
        const float aa = get(kAttack),  ad = get(kDecay),  as = get(kSustain) * 0.01f,  ar = get(kRelease);
        for (auto& v : voices_)
        {
            v.ampEnv.set(aa, ad, as, ar);
            v.filtEnv.set(fa, fd, fs, fr);
            v.glide.setTime(s_.glideMs, ctx.sampleRate);
            v.osc1.setWave(s_.wave1); v.osc2.setWave(s_.wave2);
            v.osc1.setPulseWidth(s_.pw); v.osc2.setPulseWidth(s_.pw);
            v.filter.setDrive(1.0f + s_.drive * 9.0f);
        }
    }

    void handleMidi(const mpc::MidiEvent& ev)
    {
        if (ev.isNoteOn())
        {
            const int lastNote = alloc_.lastNote();
            const int vi = alloc_.noteOn(ev.note(), ev.velocity());
            Voice& v = voices_[vi];
            const bool wasSounding = v.ampEnv.isActive();
            v.note = ev.note();
            v.velGain = lerp(1.0f, ev.velocity() / 127.0f, s_.velSens);
            // Glide from the previous note if glide is on, otherwise jump.
            if (s_.glideMs > 0.0f && lastNote >= 0) { if (!wasSounding) v.glide.snap((float)lastNote); v.glide.setTarget((float)ev.note()); }
            else v.glide.snap((float)ev.note());
            if (!wasSounding) { v.osc1.resetPhase(); v.osc2.resetPhase(0.25f); v.filter.reset(); }
            v.ampEnv.noteOn(); v.filtEnv.noteOn();
            v.ctrl = 0;   // force a coefficient update on the first sample
        }
        else if (ev.isNoteOff())
        {
            const int vi = alloc_.noteOff(ev.note());
            if (vi >= 0) releaseVoice(vi);
        }
        else if (ev.isAllNotesOff())
        {
            alloc_.allNotesOff([this](int vi) { releaseVoice(vi); });
        }
        else if (ev.isController() && ev.controller() == cc::Sustain)
        {
            controllers_.handle(ev);
            alloc_.setSustain(ev.controllerValue() >= 64, [this](int vi) { releaseVoice(vi); });
        }
        else if (ev.isPitchBend())
        {
            alloc_.setPitchBend(ev.pitchBend());
        }
        else controllers_.handle(ev);   // mod wheel, aftertouch, other CCs
    }

    void releaseVoice(int vi) { voices_[vi].ampEnv.noteOff(); voices_[vi].filtEnv.noteOff(); }

    // Render [from, to) of the block for all voices into outputs[0].
    void render(mpc::ProcessContext& ctx, int from, int to)
    {
        float* out = ctx.outputs[0];
        while (from < to)
        {
            const int n = (to - from) < kChunk ? (to - from) : kChunk;
            // Global modulators for this chunk.
            for (int i = 0; i < n; ++i) { controllers_.tick(); lfoBuf_[i] = lfo_.process(); }
            const float vib = s_.lfoPitch + s_.wheelVib * controllers_.modWheel();   // semitones of pitch LFO

            for (int vi = 0; vi < kNumVoices; ++vi)
            {
                Voice& v = voices_[vi];
                if (!v.ampEnv.isActive()) continue;
                renderVoice(v, out + from, n, vib);
                if (!v.ampEnv.isActive()) alloc_.voiceFinished(vi);   // release finished: free the slot
            }
            from += n;
        }
    }

    void renderVoice(Voice& v, float* out, int n, float vibSemitones)
    {
        for (int i = 0; i < n; ++i)
        {
            const float fenv = v.filtEnv.process();
            const float aenv = v.ampEnv.process();

            // [5] Control rate: pitch, cutoff and filter coefficients every kCtrlInterval samples.
            if ((v.ctrl++ % kCtrlInterval) == 0)
            {
                const float note = v.glide.value();
                const float lfo = lfoBuf_[i];
                const float hz = alloc_.noteToHz(note) * semitonesToRatio(lfo * vibSemitones);
                v.osc1.setFrequency(hz * semitonesToRatio(12.0f * (float)s_.oct1));
                v.osc2.setFrequency(hz * semitonesToRatio(12.0f * (float)s_.oct2 + (float)s_.semi2) * centsToRatio(s_.detune2));

                float octaves = s_.envAmt * fenv * 8.0f                 // envelope: up to +-8 octaves
                              + s_.lfoFilter * lfo * 4.0f               // LFO: up to +-4 octaves
                              + s_.keyTrack * (note - 60.0f) / 12.0f;   // key tracking around middle C
                v.filter.set(s_.cutoff * std::pow(2.0f, clamp(octaves, -10.0f, 10.0f)), s_.reso);
            }
            // The glide smoother runs per sample so it is accurate even with short glides.
            v.glide.process();

            const float mix = v.osc1.process() * s_.lvl1 + v.osc2.process() * s_.lvl2;
            const float filtered = v.filter.process(mix, s_.mode);
            out[i] += filtered * aenv * v.velGain * 0.5f;
        }
    }

    Voice voices_[kNumVoices];
    VoiceAllocator<kNumVoices> alloc_;
    LFO lfo_;
    MidiControllers controllers_;
    Smoother volume_;
    float lfoBuf_[kChunk] = {0};
};

MPC_PLUGIN_ENTRY(SimpleSynth)
