#include "PluginEditor.h"

namespace
{
const juce::Colour kBg { 0xff1e2126 };
const juce::Colour kPanel { 0xff2a2e35 };
const juce::Colour kAccent { 0xffffa31a };
const juce::Colour kGood { 0xff4cd17a };
const juce::Colour kWarn { 0xffe05a4f };
const juce::Colour kText { 0xffe6e6e6 };
} // namespace

AbleAecEditor::AbleAecEditor (AbleAecProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setupKnob (filterLen, "filterLen", "Filter Length");
    setupKnob (step, "step", "Adapt Speed");
    setupKnob (refDelay, "refDelay", "Ref Delay");
    setupKnob (resAmount, "resAmount", "Residual Supp.");
    setupKnob (resFloor, "resFloor", "Supp. Floor");

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

    resetButton.onClick = [this] { proc.requestReset(); };
    addAndMakeVisible (resetButton);

    setSize (640, 390);
    startTimerHz (15);
}

AbleAecEditor::~AbleAecEditor() { stopTimer(); }

void AbleAecEditor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& name)
{
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, kAccent);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    addAndMakeVisible (k.slider);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, kText);
    addAndMakeVisible (k.label);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramId, k.slider);
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
    g.fillAll (kBg);

    auto header = getLocalBounds().removeFromTop (36).reduced (12, 0);
    g.setColour (kAccent);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("AbleAEC", header, juce::Justification::centredLeft);
    g.setColour (kText.withAlpha (0.6f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Acoustic Echo Canceller  |  mic on input, speaker on sidechain", header, juce::Justification::centredRight);

    // meters
    g.setColour (kPanel);
    g.fillRoundedRectangle (meterArea.toFloat(), 6.0f);

    auto area = meterArea.reduced (10);
    auto bar = [&] (const juce::String& name, float db, juce::Colour c)
    {
        auto row = area.removeFromTop (20);
        area.removeFromTop (6);
        g.setColour (kText);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (name, row.removeFromLeft (70), juce::Justification::centredLeft);
        auto valueText = row.removeFromRight (60);
        g.drawText (db <= -119.0f ? juce::String ("-inf") : juce::String (db, 1) + " dB", valueText, juce::Justification::centredRight);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRect (row);
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 80.0f) / 80.0f);
        g.setColour (c);
        g.fillRect (row.withWidth ((int) (row.getWidth() * frac)));
    };
    bar ("Speaker", refDb, kAccent);
    bar ("Mic in", micDb, juce::Colours::skyblue);
    bar ("Output", outDb, kGood);

    // status
    g.setColour (kPanel);
    g.fillRoundedRectangle (statusArea.toFloat(), 6.0f);
    auto s = statusArea.reduced (10);
    g.setFont (juce::FontOptions (13.0f));

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
        line ("Sidechain", "not connected", kWarn);
    else if (! refPresent)
        line ("Sidechain", "connected, no signal", kText);
    else
        line ("Sidechain", refChannels == 2 ? "receiving (stereo)" : "receiving (mono)", kGood);

    line ("Echo reduction", juce::String (erle, 1) + " dB", converged ? kGood : kText);
    line ("Echo delay", delayMs > 0.0f ? juce::String (delayMs, 1) + " ms" : juce::String ("-"), kText);
    line ("State", converged ? "converged" : (refPresent ? "learning..." : "waiting for speaker signal"),
          converged ? kGood : kText);
    const double sr = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 48000.0;
    line ("Latency", juce::String (latencySamples) + " smp (" + juce::String (latencySamples * 1000.0 / sr, 1) + " ms)", kText);
}

void AbleAecEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    r.removeFromTop (30);

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
    modeBox.setBounds (controls.removeFromLeft (170));
    controls.removeFromLeft (10);
    latencyBox.setBounds (controls.removeFromLeft (150));
    controls.removeFromLeft (10);
    refModeBox.setBounds (controls.removeFromLeft (130));
    r.removeFromTop (6);
    auto controls2 = r.removeFromTop (26);
    freezeButton.setBounds (controls2.removeFromLeft (90));
    offButton.setBounds (controls2.removeFromLeft (90));
    resetButton.setBounds (controls2.removeFromRight (110));

    r.removeFromTop (10);
    meterArea = r.removeFromLeft (r.getWidth() / 2 - 5);
    r.removeFromLeft (10);
    statusArea = r;
}
