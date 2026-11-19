// PluginEditor.cpp - functional editor. Correct binding first, polish later.
//
// SPDX-License-Identifier: MIT
//
// Laid out by signal order, which is also the order the sections appear in the chain:
// Keep Lo -> Damage -> Tame -> Cut -> Body -> Output. SHAPE and GRAIN carry a sublabel
// that follows the Damage type (Focus/Heat, Reel/Squeeze, Slope/Bite), because their
// names are fixed but their function is not.

#include "PluginEditor.h"

namespace {
constexpr int kCellW = 104, kCellH = 86, kGapX = 10, kGapY = 14;
constexpr int kHeaderH = 20, kMargin = 14, kTitleH = 34;
}

DrumInfurnoEditor::DrumInfurnoEditor(DrumInfurnoProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    using namespace infurno;

    sections = {
        { "DAMAGE",  { kDamageType, kDrive, kShape, kGrain } },
        { "TAME",    { kTame } },
        { "CUT",     { kCutLo, kCutMid, kCutHi, kKeepLo } },
        { "BODY",    { kChamber, kBore, kRing, kBloom, kRise, kFall, kCore } },
        { "OUTPUT",  { kMix, kOutput, kMakeup } },
    };

    controls.resize((size_t)kNumParams);

    for (auto& s : sections)
    {
        auto header = std::make_unique<juce::Label>();
        header->setText(s.title, juce::dontSendNotification);
        header->setFont(juce::FontOptions(13.0f, juce::Font::bold));
        header->setColour(juce::Label::textColourId, juce::Colour(0xffe8641e));
        addAndMakeVisible(*header);
        sectionHeaders.push_back(std::move(header));

        for (int id : s.paramIds)
        {
            controls[(size_t)id] = std::make_unique<Control>();
            buildControl(*controls[(size_t)id], id);
        }
    }

    proc.apvts.addParameterListener(paramInfo(kDamageType).id, this);
    refreshDamageLabels();

    int widest = 0;
    for (auto& s : sections) widest = juce::jmax(widest, (int)s.paramIds.size());
    setSize(kMargin * 2 + widest * (kCellW + kGapX) - kGapX,
            kTitleH + kMargin + (int)sections.size() * (kHeaderH + kCellH + kGapY));
}

DrumInfurnoEditor::~DrumInfurnoEditor()
{
    proc.apvts.removeParameterListener(infurno::paramInfo(infurno::kDamageType).id, this);
}

void DrumInfurnoEditor::buildControl(Control& c, int id)
{
    const infurno::ParamInfo& p = infurno::paramInfo(id);
    c.paramId = id;

    c.name.setText(p.name, juce::dontSendNotification);
    c.name.setFont(juce::FontOptions(12.0f));
    c.name.setJustificationType(juce::Justification::centred);
    c.name.setColour(juce::Label::textColourId, juce::Colour(0xffa0836f));
    addAndMakeVisible(c.name);

    c.sublabel.setFont(juce::FontOptions(11.0f));
    c.sublabel.setJustificationType(juce::Justification::centred);
    c.sublabel.setColour(juce::Label::textColourId, juce::Colour(0xffe8641e));
    addAndMakeVisible(c.sublabel);

    if (p.choices != nullptr)
    {
        c.combo = std::make_unique<juce::ComboBox>();
        for (int i = 0; i < p.numChoices; ++i) c.combo->addItem(p.choices[i], i + 1);
        addAndMakeVisible(*c.combo);
        c.comboAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            proc.apvts, p.id, *c.combo);
    }
    else if (p.numChoices == 2)
    {
        c.toggle = std::make_unique<juce::ToggleButton>("On");
        addAndMakeVisible(*c.toggle);
        c.buttonAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            proc.apvts, p.id, *c.toggle);
    }
    else
    {
        c.slider = std::make_unique<juce::Slider>(juce::Slider::RotaryVerticalDrag,
                                                  juce::Slider::TextBoxBelow);
        c.slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, kCellW, 16);
        c.slider->setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xffe8641e));
        addAndMakeVisible(*c.slider);
        c.sliderAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            proc.apvts, p.id, *c.slider);
    }
}

void DrumInfurnoEditor::parameterChanged(const juce::String&, float)
{
    // Called from whatever thread moved the parameter, so bounce to the message thread.
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<DrumInfurnoEditor>(this)]
    {
        if (safe != nullptr) safe->refreshDamageLabels();
    });
}

void DrumInfurnoEditor::refreshDamageLabels()
{
    const int type = (int)proc.apvts.getRawParameterValue(
                         infurno::paramInfo(infurno::kDamageType).id)->load();

    auto setSub = [this](int id, const char* text)
    {
        if (auto& c = controls[(size_t)id])
            c->sublabel.setText(text, juce::dontSendNotification);
    };
    setSub(infurno::kShape, infurno::shapeLabel(type));
    setSub(infurno::kGrain, infurno::grainLabel(type));
}

void DrumInfurnoEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1f1a18));

    g.setColour(juce::Colour(0xfff2e6dc));
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawText("DRUM INFURNO", kMargin, 8, getWidth() - kMargin * 2, 24,
               juce::Justification::left);

    g.setColour(juce::Colour(0xffa0836f));
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("INFUR   -   Damage > Tame > Cut > Body", kMargin, 8,
               getWidth() - kMargin * 2, 24, juce::Justification::right);
}

void DrumInfurnoEditor::resized()
{
    int y = kTitleH;

    for (size_t si = 0; si < sections.size(); ++si)
    {
        const Section& s = sections[si];
        sectionHeaders[si]->setBounds(kMargin, y, getWidth() - kMargin * 2, kHeaderH);
        int x = kMargin;

        for (int id : s.paramIds)
        {
            auto& c = *controls[(size_t)id];
            const juce::Rectangle<int> cell(x, y + kHeaderH, kCellW, kCellH);

            c.name.setBounds(cell.getX(), cell.getY(), kCellW, 14);
            c.sublabel.setBounds(cell.getX(), cell.getBottom() - 13, kCellW, 13);

            const juce::Rectangle<int> body = cell.withTrimmedTop(14).withTrimmedBottom(13);
            if (c.slider) c.slider->setBounds(body);
            if (c.combo)  c.combo->setBounds(body.withSizeKeepingCentre(kCellW - 8, 24));
            if (c.toggle) c.toggle->setBounds(body.withSizeKeepingCentre(kCellW - 8, 24));

            x += kCellW + kGapX;
        }
        y += kHeaderH + kCellH + kGapY;
    }
}
