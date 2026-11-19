// DrumInfurno.cpp - the MPC wrapper around infurno_core.
//
// SPDX-License-Identifier: MIT
//
// There is no DSP in this file. The whole signal chain - Keep Lo, Damage, Tame, Cut,
// Body, auto-makeup, Mix, Output, safety clip - lives in core/, which has no dependency
// on the MPC SDK or the VST2 ABI, and which the desktop VST3/AU build links as well.
// This file only does the things that are specific to being an MPC plugin:
//
//   * declaring the plugin to the MPC (name, vendor, uid, channel counts, latency, tail)
//   * turning the core's shared parameter table into MPC parameters
//   * turning the core's shared program table into MPC programs
//   * asking the core for value text, so the MPC screen and the desktop editor agree
//   * forwarding prepare / reset / process
//
// Parameter order comes from the core's ParamId enum and is permanent: it is the saved
// state order and the Q-Link order. Append there, never insert.
//
// See core/README.md for the architecture and the bit-exactness test that guards it.

#include "mpc_plugin.h"
// Relative on purpose: tools/build.py compiles src/*.cpp with a fixed include list
// (-I<sdk>/framework -I<project>/src) and offers no way to add -I core/include. The
// CMake desktop build gets the same header through target_include_directories.
#include "../../../core/include/infurno/Engine.h"

#include <cstring>
#include <string>
#include <vector>

class DrumInfurno : public mpc::Plugin
{
public:
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
        i.latencySamples = infurno::latencySamples();   // constant across algorithms
        i.tailSamples = 88200;                          // ~2 s: the Body's longest ring
        return i;
    }

    DrumInfurno() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;

        // Parameters, built from the core's table so the two wrappers cannot disagree
        // about a range, a default or a name.
        for (int id = 0; id < infurno::kNumParams; ++id)
        {
            const infurno::ParamInfo& p = infurno::paramInfo(id);

            if (p.choices != nullptr)
            {
                std::vector<std::string> choices;
                choices.reserve((size_t)p.numChoices);
                for (int c = 0; c < p.numChoices; ++c) choices.push_back(p.choices[c]);
                addParam(choiceParam(p.id, p.name, std::move(choices), (int)p.def));
            }
            else if (p.numChoices == 2)                       // a bool: Off / On
            {
                addParam(boolParam(p.id, p.name, p.def >= 0.5f));
            }
            else if (p.scale == infurno::Scale::Logarithmic)
            {
                addParam(logParam(p.id, p.name, p.min, p.max, p.def, p.unit, p.decimals));
            }
            else
            {
                addParam(floatParam(p.id, p.name, p.min, p.max, p.def, p.unit, p.decimals));
            }
        }

        // Factory programs, also from the core's table.
        for (int i = 0; i < infurno::numPrograms(); ++i)
        {
            const float* v = infurno::programValues(i);
            addProgram(infurno::programName(i), std::vector<float>(v, v + infurno::kNumParams));
        }
    }

    // The MPC shows the parameter name and this text, and does NOT append the unit label
    // (docs/host-contract.md), so units live in the value text. The core formats it so
    // the desktop editor shows exactly the same strings, including the SHAPE / GRAIN
    // sublabels that depend on the Damage type. UI thread only.
    bool formatValue(int index, float plain, std::string& out) override
    {
        const infurno::ParamInfo& p = infurno::paramInfo(index);
        if (p.choices != nullptr || p.numChoices == 2)
            return false;            // the framework's own choice / On-Off text is right

        char buf[48];
        infurno::formatValue(index, plain, getInt(infurno::kDamageType), buf, sizeof(buf));
        out = buf;
        return true;
    }

    // ----- lifecycle --------------------------------------------------------
    void prepare(double sampleRate, int maxBlockSize) override
    {
        pushParams();
        engine_.prepare(sampleRate, maxBlockSize);
    }

    void reset() override
    {
        pushParams();
        engine_.reset();
    }

    // ----- audio ------------------------------------------------------------
    void process(mpc::ProcessContext& ctx) override
    {
        if (ctx.inputs == nullptr || ctx.outputs == nullptr || ctx.numOutputs < 1) return;

        const float* inL = ctx.inputs[0];
        const float* inR = ctx.numInputs > 1 && ctx.inputs[1] != nullptr ? ctx.inputs[1] : inL;
        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : ctx.outputs[0];
        if (inL == nullptr || outL == nullptr) return;

        // The core works in place. The MPC normally hands us the same buffers for input
        // and output, in which case these copies do not happen at all; a host that passes
        // separate buffers gets the input moved across first. memcpy is realtime safe.
        if (outL != inL) std::memcpy(outL, inL, sizeof(float) * (size_t)ctx.numFrames);
        if (outR != inR) std::memcpy(outR, inR, sizeof(float) * (size_t)ctx.numFrames);

        pushParams();
        float* io[2] = { outL, outR };
        engine_.process(io, ctx.numFrames);
    }

    // Test hook only; the safety clipper is always on in normal use.
    void setSafetyClipEnabled(bool on) { engine_.setSafetyClipEnabled(on); }

private:
    // The framework stores parameters as atomics written by the UI thread; the core holds
    // plain floats and expects its owner to snapshot them. One pass per block does that.
    void pushParams()
    {
        for (int id = 0; id < infurno::kNumParams; ++id) engine_.setParam(id, get(id));
    }

    infurno::Engine engine_;
};

MPC_PLUGIN_ENTRY(DrumInfurno)
