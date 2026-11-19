// PluginProcessor.cpp - JUCE wrapper around infurno_core.
//
// SPDX-License-Identifier: MIT

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {

// The MPC maps a logarithmic parameter as plain = min * (max/min)^norm. Mirroring that
// exactly here means a knob at the same position gives the same value in both builds.
juce::NormalisableRange<float> logRange(float min, float max)
{
    return {
        min, max,
        [](float lo, float hi, float norm)  { return lo * std::pow(hi / lo, norm); },
        [](float lo, float hi, float plain) { return std::log(plain / lo) / std::log(hi / lo); },
        [](float lo, float hi, float v)     { return juce::jlimit(lo, hi, v); }
    };
}

} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout DrumInfurnoProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int id = 0; id < infurno::kNumParams; ++id)
    {
        const infurno::ParamInfo& p = infurno::paramInfo(id);
        const juce::ParameterID pid { p.id, 1 };

        if (p.choices != nullptr)                       // Damage type, Chamber
        {
            juce::StringArray choices;
            for (int c = 0; c < p.numChoices; ++c) choices.add(p.choices[c]);
            layout.add(std::make_unique<juce::AudioParameterChoice>(
                pid, p.name, choices, (int)p.def));
        }
        else if (p.numChoices == 2)                     // Makeup
        {
            layout.add(std::make_unique<juce::AudioParameterBool>(
                pid, p.name, p.def >= 0.5f));
        }
        else
        {
            auto range = p.scale == infurno::Scale::Logarithmic
                       ? logRange(p.min, p.max)
                       : juce::NormalisableRange<float>(p.min, p.max);

            // Shape and Grain deliberately show only a number here: what they DO depends
            // on the Damage type, and a parameter's text function has no reliable way to
            // see another parameter's current value. The editor shows the live function
            // ("Focus", "Squeeze", ...) in a label under the knob instead.
            const bool dependsOnType = (id == infurno::kShape || id == infurno::kGrain);

            auto attrs = juce::AudioParameterFloatAttributes()
                .withLabel(p.unit)
                .withStringFromValueFunction(
                    [id, dependsOnType, decimals = p.decimals](float v, int) -> juce::String
                    {
                        if (dependsOnType) return juce::String(v, decimals);
                        char buf[48];
                        infurno::formatValue(id, v, 0, buf, sizeof(buf));
                        return { buf };
                    });

            layout.add(std::make_unique<juce::AudioParameterFloat>(pid, p.name, range, p.def, attrs));
        }
    }

    return layout;
}

DrumInfurnoProcessor::DrumInfurnoProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createLayout())
{
    for (int id = 0; id < infurno::kNumParams; ++id)
        paramPtrs[(size_t)id] = apvts.getRawParameterValue(infurno::paramInfo(id).id);

    setLatencySamples(infurno::latencySamples());

    // Prepare once up front so the engine's delay lines exist even if a host (or
    // pluginval) calls reset() or processBlock() in an unexpected order.
    engine.prepare(44100.0, 512);
}

void DrumInfurnoProcessor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
    engine.prepare(sampleRate, maximumExpectedSamplesPerBlock);
    engine.reset();
    monoScratch.setSize(1, juce::jmax(1, maximumExpectedSamplesPerBlock), false, true, true);
    setLatencySamples(infurno::latencySamples());
}

void DrumInfurnoProcessor::reset()
{
    engine.reset();
}

bool DrumInfurnoProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in  = layouts.getMainInputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return in == out;
}

void DrumInfurnoProcessor::setCurrentProgram(int index)
{
    if (index < 0 || index >= infurno::numPrograms()) return;
    currentProgram = index;

    const float* v = infurno::programValues(index);
    for (int id = 0; id < infurno::kNumParams; ++id)
    {
        if (auto* p = apvts.getParameter(infurno::paramInfo(id).id))
        {
            // Programs store plain values; parameters take normalised ones.
            p->setValueNotifyingHost(p->convertTo0to1(v[id]));
        }
    }
}

void DrumInfurnoProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;        // FTZ / DAZ for the whole block

    const int numSamples = buffer.getNumSamples();
    const int numIn      = getTotalNumInputChannels();
    const int numOut     = getTotalNumOutputChannels();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear(ch, 0, numSamples);

    if (numSamples <= 0 || numOut <= 0) return;

    // One snapshot per block, pushed into the engine, exactly as the MPC wrapper does.
    for (int id = 0; id < infurno::kNumParams; ++id)
        if (auto* a = paramPtrs[(size_t)id])
            engine.setParam(id, a->load(std::memory_order_relaxed));

    if (numOut >= 2)
    {
        float* io[2] = { buffer.getWritePointer(0), buffer.getWritePointer(1) };
        engine.process(io, numSamples);
    }
    else
    {
        // Mono: the engine is stereo, so give it a scratch right channel rather than the
        // same pointer twice, which would have the two channels writing over each other.
        if (monoScratch.getNumSamples() < numSamples)
            monoScratch.setSize(1, numSamples, false, true, true);   // only if a host lied about its block size

        float* left  = buffer.getWritePointer(0);
        float* right = monoScratch.getWritePointer(0);
        juce::FloatVectorOperations::copy(right, left, numSamples);

        float* io[2] = { left, right };
        engine.process(io, numSamples);
    }
}

juce::AudioProcessorEditor* DrumInfurnoProcessor::createEditor()
{
    return new DrumInfurnoEditor(*this);
}

void DrumInfurnoProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void DrumInfurnoProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DrumInfurnoProcessor();
}
