// PluginProcessor.h - JUCE wrapper around infurno_core.
//
// SPDX-License-Identifier: MIT
//
// No DSP lives here. The parameter layout is generated from the core's shared table, so
// the desktop build and the MPC build cannot drift apart on ranges, defaults or names.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <infurno/Engine.h>

class DrumInfurnoProcessor : public juce::AudioProcessor
{
public:
    DrumInfurnoProcessor();
    ~DrumInfurnoProcessor() override = default;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }   // the Body's longest ring

    // Factory programs come from the core, so both builds offer the same ones.
    int getNumPrograms() override                   { return infurno::numPrograms(); }
    int getCurrentProgram() override                { return currentProgram; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override { return infurno::programName(index); }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    infurno::Engine engine;
    std::array<std::atomic<float>*, infurno::kNumParams> paramPtrs {};
    juce::AudioBuffer<float> monoScratch;    // second channel when the host is mono
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumInfurnoProcessor)
};
