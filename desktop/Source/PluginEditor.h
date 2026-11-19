// PluginEditor.h - functional editor. Correct binding first, polish later.
//
// SPDX-License-Identifier: MIT

#pragma once

#include "PluginProcessor.h"

class DrumInfurnoEditor : public juce::AudioProcessorEditor,
                          private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit DrumInfurnoEditor(DrumInfurnoProcessor&);
    ~DrumInfurnoEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // One control plus its labels. Exactly one of the three widgets is used.
    struct Control
    {
        int paramId = 0;
        juce::Label  name;
        juce::Label  sublabel;        // only Shape and Grain use this
        std::unique_ptr<juce::Slider>      slider;
        std::unique_ptr<juce::ComboBox>    combo;
        std::unique_ptr<juce::ToggleButton> toggle;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   sliderAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   buttonAtt;
    };

    // No juce::Label member here: Label is non-copyable, so a vector of these could
    // not be brace-initialised. The headers live in their own vector of unique_ptr.
    struct Section { juce::String title; std::vector<int> paramIds; };

    void parameterChanged(const juce::String& paramID, float newValue) override;
    void refreshDamageLabels();
    void buildControl(Control&, int paramId);

    DrumInfurnoProcessor& proc;
    std::vector<std::unique_ptr<Control>> controls;   // indexed by ParamId
    std::vector<Section> sections;
    std::vector<std::unique_ptr<juce::Label>> sectionHeaders;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumInfurnoEditor)
};
