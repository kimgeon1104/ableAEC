#include "PluginEditor.h"

#include "BinaryData.h"

namespace
{
const juce::Colour kBg { 0xff1e2126 };
const juce::Colour kPanel { 0xff2a2e35 };
const juce::Colour kAccent { 0xffffa31a };
const juce::Colour kGood { 0xff4cd17a };
const juce::Colour kWarn { 0xffe05a4f };
const juce::Colour kText { 0xffe6e6e6 };
} // namespace

//==============================================================================
AbleAecEditor::AbleAecEditor (AbleAecProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    // Embedded UI font (Latin + the Korean characters the interface uses), so Korean renders the
    // same on every system. It becomes this plugin's default sans-serif face.
    typeRegular = juce::Typeface::createSystemTypefaceFor (BinaryData::AbleAECUiKRRegular_ttf,
                                                           (size_t) BinaryData::AbleAECUiKRRegular_ttfSize);
    typeBold = juce::Typeface::createSystemTypefaceFor (BinaryData::AbleAECUiKRBold_ttf,
                                                        (size_t) BinaryData::AbleAECUiKRBold_ttfSize);
    if (typeRegular != nullptr)
        juce::LookAndFeel::getDefaultLookAndFeel().setDefaultSansSerifTypeface (typeRegular);

    lang = proc.getUiLanguage();

    setupKnob (filterLen, "filterLen");
    setupKnob (step, "step");
    setupKnob (refDelay, "refDelay");
    setupKnob (resAmount, "resAmount");
    setupKnob (resFloor, "resFloor");

    for (auto* b : { &freezeButton, &offButton })
    {
        b->setColour (juce::ToggleButton::textColourId, kText);
        b->setColour (juce::ToggleButton::tickColourId, kAccent);
        addAndMakeVisible (*b);
    }
    freezeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "freeze", freezeButton);
    offAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "aecOff", offButton);

    auto setupBox = [this] (juce::ComboBox& box, const juce::String& paramId,
                            std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>& att)
    {
        box.addItemList (proc.apvts.getParameter (paramId)->getAllValueStrings(), 1);
        addAndMakeVisible (box);
        att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, paramId, box);
    };
    setupBox (modeBox, "mode", modeAtt);
    setupBox (latencyBox, "latency", latencyAtt);
    setupBox (refModeBox, "refMode", refModeAtt);

    langBox.addItem ("English", 1);
    langBox.addItem (juce::String::fromUTF8 ("한국어"), 2);
    langBox.setSelectedId (lang == ui::Lang::ko ? 2 : 1, juce::dontSendNotification);
    langBox.onChange = [this]
    {
        lang = langBox.getSelectedId() == 2 ? ui::Lang::ko : ui::Lang::en;
        proc.setUiLanguage (lang);
        applyLanguage();
    };
    addAndMakeVisible (langBox);

    resetButton.onClick = [this] { proc.requestReset(); };
    addAndMakeVisible (resetButton);

    guideButton.onClick = [this]
    {
        guidePanel.setVisible (true);
        guidePanel.toFront (true);
    };
    addAndMakeVisible (guideButton);

    addChildComponent (guidePanel);

    applyLanguage();
    setSize (640, 410);
    startTimerHz (15);
}

AbleAecEditor::~AbleAecEditor() { stopTimer(); }

juce::FontOptions AbleAecEditor::font (float height, bool bold) const
{
    if (auto tf = bold ? typeBold : typeRegular)
        return juce::FontOptions (tf).withHeight (height);
    return juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain);
}

void AbleAecEditor::setupKnob (Knob& k, const juce::String& paramId)
{
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, kAccent);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    addAndMakeVisible (k.slider);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, kText);
    addAndMakeVisible (k.label);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramId, k.slider);
}

void AbleAecEditor::applyLanguage()
{
    using ui::Str;
    const std::pair<Knob*, std::pair<Str, Str>> knobs[] = {
        { &filterLen, { Str::knobFilterLen, Str::tipFilterLen } },
        { &step, { Str::knobStep, Str::tipStep } },
        { &refDelay, { Str::knobRefDelay, Str::tipRefDelay } },
        { &resAmount, { Str::knobResAmount, Str::tipResAmount } },
        { &resFloor, { Str::knobResFloor, Str::tipResFloor } },
    };
    for (auto& [k, s] : knobs)
    {
        k->label.setText (t (s.first), juce::dontSendNotification);
        k->label.setFont (font (14.0f));
        k->label.setTooltip (t (s.second));
        k->slider.setTooltip (t (s.second));
    }

    freezeButton.setButtonText (t (Str::freeze));
    freezeButton.setTooltip (t (Str::tipFreeze));
    offButton.setButtonText (t (Str::aecOff));
    offButton.setTooltip (t (Str::tipAecOff));
    resetButton.setButtonText (t (Str::resetFilter));
    resetButton.setTooltip (t (Str::tipReset));
    guideButton.setButtonText (t (Str::guide));
    guideButton.setTooltip (t (Str::tipGuide));
    langBox.setTooltip (t (Str::tipLanguage));

    // combo boxes keep their item ids (= parameter choices); only the shown text changes
    auto relabel = [] (juce::ComboBox& box, std::initializer_list<juce::String> texts)
    {
        const int selected = box.getSelectedId(); // read before the texts change
        int id = 1;
        for (auto& s : texts) box.changeItemText (id++, s);
        if (selected != 0)
            box.setSelectedId (selected, juce::dontSendNotification); // refreshes the shown text
    };
    relabel (modeBox, { t (Str::modeClean), t (Str::modeLinear), t (Str::modeEcho), t (Str::modeRaw) });
    relabel (latencyBox, { t (Str::latQuality), t (Str::latLow) });
    relabel (refModeBox, { t (Str::refStereo), t (Str::refMono) });
    modeBox.setTooltip (t (Str::tipMode));
    latencyBox.setTooltip (t (Str::tipLatency));
    refModeBox.setTooltip (t (Str::tipRefMode));

    guidePanel.setContent (lang);
    repaint();
}

void AbleAecEditor::timerCallback()
{
    auto& m = proc.meters;
    erle = m.erleDb.load();
    delayMs = m.echoDelayMs.load();
    refDb = m.refLevelDb.load();
    micDb = m.micLevelDb.load();
    outDb = m.outLevelDb.load();
    converged = m.converged.load();
    sidechain = m.sidechainConnected.load();
    refChannels = m.refChannels.load();
    latencySamples = m.latencySamples.load();
    repaint (meterArea);
    repaint (statusArea);
}

void AbleAecEditor::paint (juce::Graphics& g)
{
    using ui::Str;
    g.fillAll (kBg);

    auto header = getLocalBounds().reduced (12, 0).removeFromTop (54);
    g.setColour (kAccent);
    g.setFont (font (22.0f, true));
    g.drawText ("AbleAEC", header.removeFromTop (34).withTrimmedTop (6), juce::Justification::centredLeft);
    g.setColour (kText.withAlpha (0.6f));
    g.setFont (font (12.0f));
    g.drawText (t (Str::subtitle), header, juce::Justification::centredLeft);

    // meters
    g.setColour (kPanel);
    g.fillRoundedRectangle (meterArea.toFloat(), 6.0f);

    auto area = meterArea.reduced (10);
    auto bar = [&] (const juce::String& name, float db, juce::Colour c)
    {
        auto row = area.removeFromTop (20);
        area.removeFromTop (6);
        g.setColour (kText);
        g.setFont (font (12.0f));
        g.drawText (name, row.removeFromLeft (76), juce::Justification::centredLeft);
        auto valueText = row.removeFromRight (60);
        g.drawText (db <= -119.0f ? juce::String ("-inf") : juce::String (db, 1) + " dB", valueText, juce::Justification::centredRight);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRect (row);
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 80.0f) / 80.0f);
        g.setColour (c);
        g.fillRect (row.withWidth ((int) (row.getWidth() * frac)));
    };
    bar (t (Str::meterSpeaker), refDb, kAccent);
    bar (t (Str::meterMic), micDb, juce::Colours::skyblue);
    bar (t (Str::meterOut), outDb, kGood);

    // status
    g.setColour (kPanel);
    g.fillRoundedRectangle (statusArea.toFloat(), 6.0f);
    auto s = statusArea.reduced (10);
    g.setFont (font (13.0f));

    auto line = [&] (const juce::String& k, const juce::String& v, juce::Colour c)
    {
        auto row = s.removeFromTop (20);
        g.setColour (kText.withAlpha (0.7f));
        g.drawText (k, row.removeFromLeft (110), juce::Justification::centredLeft);
        g.setColour (c);
        g.drawText (v, row, juce::Justification::centredLeft);
    };

    const bool refPresent = refDb > -80.0f;
    if (! sidechain)
        line (t (Str::stSidechain), t (Str::scNotConnected), kWarn);
    else if (! refPresent)
        line (t (Str::stSidechain), t (Str::scNoSignal), kText);
    else
        line (t (Str::stSidechain), t (refChannels == 2 ? Str::scStereo : Str::scMono), kGood);

    line (t (Str::stErle), juce::String (erle, 1) + " dB", converged ? kGood : kText);
    line (t (Str::stDelay), delayMs > 0.0f ? juce::String (delayMs, 1) + " ms" : juce::String ("-"), kText);
    line (t (Str::stState), converged ? t (Str::stateConverged) : t (refPresent ? Str::stateLearning : Str::stateWaiting),
          converged ? kGood : kText);
    const double sr = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 48000.0;
    line (t (Str::stLatency), juce::String (latencySamples) + " " + t (Str::samples) + " ("
                                  + juce::String (latencySamples * 1000.0 / sr, 1) + " ms)", kText);
}

void AbleAecEditor::resized()
{
    auto r = getLocalBounds().reduced (12);

    auto header = r.removeFromTop (44);
    auto headerRight = header.removeFromRight (230).withSizeKeepingCentre (230, 28);
    guideButton.setBounds (headerRight.removeFromRight (90));
    headerRight.removeFromRight (10);
    langBox.setBounds (headerRight);
    r.removeFromTop (6);

    auto knobs = r.removeFromTop (130);
    const int w = knobs.getWidth() / 5;
    for (auto* k : { &filterLen, &step, &refDelay, &resAmount, &resFloor })
    {
        auto cell = knobs.removeFromLeft (w).reduced (4, 0);
        k->label.setBounds (cell.removeFromTop (18));
        k->slider.setBounds (cell);
    }

    r.removeFromTop (8);
    auto controls = r.removeFromTop (28);
    modeBox.setBounds (controls.removeFromLeft (180));
    controls.removeFromLeft (10);
    latencyBox.setBounds (controls.removeFromLeft (170));
    controls.removeFromLeft (10);
    refModeBox.setBounds (controls.removeFromLeft (140));
    r.removeFromTop (6);
    auto controls2 = r.removeFromTop (26);
    freezeButton.setBounds (controls2.removeFromLeft (110));
    offButton.setBounds (controls2.removeFromLeft (110));
    resetButton.setBounds (controls2.removeFromRight (120));

    r.removeFromTop (10);
    meterArea = r.removeFromLeft (r.getWidth() / 2 - 5);
    r.removeFromLeft (10);
    statusArea = r;

    guidePanel.setBounds (getLocalBounds().reduced (10));
}

//==============================================================================
AbleAecEditor::GuidePanel::GuidePanel (AbleAecEditor& o) : owner (o)
{
    text.setMultiLine (true, true);
    text.setReadOnly (true);
    text.setScrollbarsShown (true);
    text.setCaretVisible (false);
    text.setColour (juce::TextEditor::backgroundColourId, kPanel);
    text.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    text.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    text.setColour (juce::TextEditor::textColourId, kText);
    addAndMakeVisible (text);

    closeButton.onClick = [this] { setVisible (false); };
    addAndMakeVisible (closeButton);
}

void AbleAecEditor::GuidePanel::setContent (ui::Lang lang)
{
    title = ui::tr (ui::Str::guideTitle, lang);
    closeButton.setButtonText (ui::tr (ui::Str::close, lang));

    text.clear();
    const auto lines = juce::StringArray::fromLines (ui::guide (lang));
    for (int i = 0; i < lines.size(); ++i)
    {
        auto lineText = lines[i];
        if (lineText.startsWith ("# "))
        {
            text.setFont (owner.font (16.0f, true));
            text.setColour (juce::TextEditor::textColourId, kAccent);
            text.insertTextAtCaret (lineText.substring (2) + "\n");
        }
        else
        {
            text.setFont (owner.font (14.0f));
            text.setColour (juce::TextEditor::textColourId, kText);
            if (lineText.startsWith ("- "))
                lineText = juce::String::fromUTF8 ("•  ") + lineText.substring (2);
            text.insertTextAtCaret (lineText + (i + 1 < lines.size() ? "\n" : ""));
        }
    }
    text.setCaretPosition (0);
    repaint();
}

void AbleAecEditor::GuidePanel::paint (juce::Graphics& g)
{
    g.setColour (kBg);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    g.setColour (kAccent.withAlpha (0.6f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.0f, 1.0f);
    g.setColour (kText);
    g.setFont (owner.font (18.0f, true));
    g.drawText (title, getLocalBounds().reduced (16, 0).removeFromTop (44), juce::Justification::centredLeft);
}

void AbleAecEditor::GuidePanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (32);
    closeButton.setBounds (top.removeFromRight (90).withSizeKeepingCentre (90, 28));
    r.removeFromTop (8);
    text.setBounds (r);
}
