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
AbleAecEditor::AbleAecEditor (AbleAecProcessor& p)
    : AudioProcessorEditor (&p), proc (p), isApp (p.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
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

    for (auto* b : { &freezeButton, &offButton, &autoButton })
    {
        b->setColour (juce::ToggleButton::textColourId, kText);
        b->setColour (juce::ToggleButton::tickColourId, kAccent);
        addAndMakeVisible (*b);
    }
    freezeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "freeze", freezeButton);
    offAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "aecOff", offButton);
    autoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "auto", autoButton);
    autoButton.setColour (juce::ToggleButton::tickColourId, kGood);

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

    guideButton.onClick = [this] { showPanel (guidePanel); };
    addAndMakeVisible (guideButton);

    // presets
    presetBox.onChange = [this]
    {
        const int index = presetBox.getSelectedId() - 1;
        if (index >= 0 && index != proc.getCurrentPresetIndex())
            proc.applyPreset (index);
        deletePresetButton.setEnabled (! proc.isFactoryPreset (index));
    };
    savePresetButton.onClick = [this] { savePresetDialog(); };
    deletePresetButton.onClick = [this]
    {
        proc.deleteUserPreset (presetBox.getSelectedId() - 1);
        refreshPresetList();
    };
    midiButton.onClick = [this] { midiPanel.refresh(); showPanel (midiPanel); };
    keysButton.onClick = [this] { showPanel (keysPanel); };
    for (auto* c : std::initializer_list<juce::Component*> { &presetBox, &savePresetButton, &deletePresetButton, &midiButton })
        addAndMakeVisible (c);
    if (isApp)
        addAndMakeVisible (keysButton);

    addChildComponent (guidePanel);
    addChildComponent (keysPanel);
    addChildComponent (midiPanel);

    applyLanguage();
    setAutoKnobs (autoButton.getToggleState());
    refreshPresetList();
    setWantsKeyboardFocus (isApp);
    setSize (640, 470);
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
        const bool autoDriven = autoShown && (k == &filterLen || k == &resAmount || k == &resFloor);
        k->label.setText (t (s.first) + (autoDriven ? t (Str::autoSuffix) : juce::String()), juce::dontSendNotification);
        k->label.setColour (juce::Label::textColourId, autoDriven ? kGood : kText);
        k->label.setFont (font (14.0f));
        k->label.setTooltip (t (s.second));
        k->slider.setTooltip (t (s.second));
    }

    freezeButton.setButtonText (t (Str::freeze));
    freezeButton.setTooltip (t (Str::tipFreeze));
    offButton.setButtonText (t (Str::aecOff));
    offButton.setTooltip (t (Str::tipAecOff));
    autoButton.setButtonText (t (Str::autoMode));
    autoButton.setTooltip (t (Str::tipAuto));
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

    guidePanel.setContent (t (Str::guideTitle), ui::guide (lang), t (Str::close));
    keysPanel.setContent (t (Str::keysTitle), ui::shortcuts (lang), t (Str::close));
    midiPanel.refresh();
    savePresetButton.setButtonText (t (Str::presetSave));
    deletePresetButton.setButtonText (t (Str::presetDelete));
    midiButton.setButtonText (t (Str::midiButton));
    keysButton.setButtonText (t (Str::keysButton));
    presetBox.setTooltip (t (Str::tipPreset));
    savePresetButton.setTooltip (t (Str::tipPresetSave));
    deletePresetButton.setTooltip (t (Str::tipPresetDelete));
    midiButton.setTooltip (t (Str::tipMidi));
    keysButton.setTooltip (t (Str::tipKeys));
    shownPresetCount = -1; // rebuild the list with the new language
    refreshPresetList();
    repaint();
}

juce::String AbleAecEditor::knobParamId (const Knob& k) const
{
    if (&k == &filterLen) return "filterLen";
    if (&k == &resAmount) return "resAmount";
    if (&k == &resFloor) return "resFloor";
    if (&k == &step) return "step";
    return "refDelay";
}

// In Auto the three driven knobs are detached from their parameters (so the user's values stay as
// they were) and show what the plugin chose; switching Auto off re-attaches them.
void AbleAecEditor::setAutoKnobs (bool on)
{
    if (on == autoShown) return;
    autoShown = on;
    for (auto* k : { &filterLen, &resAmount, &resFloor })
    {
        if (on)
            k->attachment.reset();
        else
            k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, knobParamId (*k), k->slider);
        k->slider.setInterceptsMouseClicks (! on, ! on); // Auto drives it; the user cannot turn it
        k->slider.setColour (juce::Slider::rotarySliderFillColourId, on ? kGood : kAccent);
        k->slider.setColour (juce::Slider::textBoxTextColourId, on ? kGood : kText);
    }
    applyLanguage();
}

void AbleAecEditor::timerCallback()
{
    auto& m = proc.meters;
    setAutoKnobs (autoButton.getToggleState());
    if (autoShown)
    {
        filterLen.slider.setValue (m.effFilterMs.load(), juce::dontSendNotification);
        resAmount.slider.setValue (m.effResAmount.load(), juce::dontSendNotification);
        resFloor.slider.setValue (m.effResFloorDb.load(), juce::dontSendNotification);
    }
    echoPresent = m.echoPresent.load();
    if (proc.getCurrentPresetIndex() != shownPreset || proc.getNumPresets() != shownPresetCount)
        refreshPresetList();
    if (midiPanel.isVisible())
        midiPanel.refresh();
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
    if (! autoShown)
        line (t (Str::stAuto), t (Str::autoOff), kText.withAlpha (0.7f));
    else if (! converged && ! echoPresent)
        line (t (Str::stAuto), t (refPresent ? Str::autoLearning : Str::autoOnNoEcho), kText);
    else
        line (t (Str::stAuto), t (echoPresent ? Str::autoOnEcho : Str::autoOnNoEcho), kGood);
}

void AbleAecEditor::resized()
{
    auto r = getLocalBounds().reduced (12);

    auto header = r.removeFromTop (44);
    auto headerRight = header.removeFromRight (230).withSizeKeepingCentre (230, 28);
    guideButton.setBounds (headerRight.removeFromRight (90));
    headerRight.removeFromRight (10);
    langBox.setBounds (headerRight);
    r.removeFromTop (4);

    auto presetRow = r.removeFromTop (28);
    presetBox.setBounds (presetRow.removeFromLeft (250));
    presetRow.removeFromLeft (8);
    savePresetButton.setBounds (presetRow.removeFromLeft (70));
    presetRow.removeFromLeft (6);
    deletePresetButton.setBounds (presetRow.removeFromLeft (70));
    midiButton.setBounds (presetRow.removeFromRight (70));
    if (isApp)
    {
        presetRow.removeFromRight (6);
        keysButton.setBounds (presetRow.removeFromRight (90));
    }
    r.removeFromTop (8);

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
    autoButton.setBounds (controls2.removeFromLeft (100));
    resetButton.setBounds (controls2.removeFromRight (120));

    r.removeFromTop (10);
    meterArea = r.removeFromLeft (r.getWidth() / 2 - 5);
    r.removeFromLeft (10);
    statusArea = r;

    for (auto* p : std::initializer_list<juce::Component*> { &guidePanel, &keysPanel, &midiPanel })
        p->setBounds (getLocalBounds().reduced (10));
}

void AbleAecEditor::showPanel (juce::Component& panel)
{
    for (auto* p : std::initializer_list<juce::Component*> { &guidePanel, &keysPanel, &midiPanel })
        p->setVisible (p == &panel);
    panel.toFront (true);
}

void AbleAecEditor::refreshPresetList()
{
    using ui::Str;
    const int n = proc.getNumPresets();
    const int current = proc.getCurrentPresetIndex();
    if (n != shownPresetCount)
    {
        presetBox.clear (juce::dontSendNotification);
        const int nf = (int) ctl::factoryPresets().size();
        presetBox.addSectionHeading (t (Str::presetFactory));
        for (int i = 0; i < nf; ++i)
            presetBox.addItem ((isApp && i < 9 ? juce::String (i + 1) + ".  " : juce::String()) + proc.getPresetName (i, lang), i + 1);
        if (n > nf)
        {
            presetBox.addSeparator();
            presetBox.addSectionHeading (t (Str::presetUser));
            for (int i = nf; i < n; ++i)
                presetBox.addItem ((isApp && i < 9 ? juce::String (i + 1) + ".  " : juce::String()) + proc.getPresetName (i, lang), i + 1);
        }
        shownPresetCount = n;
    }
    presetBox.setSelectedId (current + 1, juce::dontSendNotification);
    deletePresetButton.setEnabled (! proc.isFactoryPreset (current));
    shownPreset = current;
}

void AbleAecEditor::savePresetDialog()
{
    using ui::Str;
    auto* w = new juce::AlertWindow (t (Str::presetSaveTitle), {}, juce::MessageBoxIconType::NoIcon, this);
    const int current = proc.getCurrentPresetIndex();
    w->addTextEditor ("name", proc.isFactoryPreset (current) ? juce::String() : proc.getPresetName (current, lang), t (Str::presetName));
    w->addButton (t (Str::presetSave), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton (t (Str::cancel), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<AbleAecEditor> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
    {
        if (result == 1 && safe != nullptr)
        {
            safe->proc.saveUserPreset (w->getTextEditorContents ("name"));
            safe->shownPresetCount = -1;
            safe->refreshPresetList();
        }
    }), true);
}

bool AbleAecEditor::keyPressed (const juce::KeyPress& key)
{
    if (! isApp)
        return false;

    const auto c = key.getTextCharacter();
    const auto code = key.getKeyCode();
    if (code == juce::KeyPress::escapeKey)
    {
        for (auto* p : std::initializer_list<juce::Component*> { &guidePanel, &keysPanel, &midiPanel })
            p->setVisible (false);
        return true;
    }
    if (code == juce::KeyPress::F1Key || c == 'g' || c == 'G') { showPanel (guidePanel); return true; }
    if (c == 'k' || c == 'K' || c == '?') { showPanel (keysPanel); return true; }
    if (key.getModifiers().isCommandDown() && (code == 'S' || code == 's')) { savePresetDialog(); return true; }
    if (key.getModifiers().isCommandDown() || key.getModifiers().isAltDown())
        return false;
    if (c >= '1' && c <= '9') { proc.applyPreset (c - '1'); return true; }

    switch (juce::CharacterFunctions::toLowerCase (c))
    {
        case 'a': proc.toggleParam ("auto"); return true;
        case 'f': proc.toggleParam ("freeze"); return true;
        case 'b': proc.toggleParam ("aecOff"); return true;
        case 'r': proc.requestReset(); return true;
        case 'm': proc.cycleChoice ("mode"); return true;
        case 'l': proc.cycleChoice ("latency"); return true;
        case ',': proc.nudgeParam ("filterLen", -25.0f); return true;
        case '.': proc.nudgeParam ("filterLen", 25.0f); return true;
        case '[': proc.nudgeParam ("resAmount", -0.05f); return true;
        case ']': proc.nudgeParam ("resAmount", 0.05f); return true;
        case '-': proc.nudgeParam ("step", -0.05f); return true;
        case '=': proc.nudgeParam ("step", 0.05f); return true;
        default: return false;
    }
}

//==============================================================================
AbleAecEditor::InfoPanel::InfoPanel (AbleAecEditor& o) : owner (o)
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

    // Lay the text out at a real width from the start: filling a 0-px-wide TextEditor wraps every
    // character onto its own line and takes seconds.
    setSize (620, 450);
}

void AbleAecEditor::InfoPanel::setContent (const juce::String& newTitle, const juce::String& body, const juce::String& closeText)
{
    title = newTitle;
    closeButton.setButtonText (closeText);

    text.clear();
    const auto lines = juce::StringArray::fromLines (body);
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

void AbleAecEditor::InfoPanel::paint (juce::Graphics& g)
{
    g.setColour (kBg);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    g.setColour (kAccent.withAlpha (0.6f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.0f, 1.0f);
    g.setColour (kText);
    g.setFont (owner.font (18.0f, true));
    g.drawText (title, getLocalBounds().reduced (16, 0).removeFromTop (44), juce::Justification::centredLeft);
}

void AbleAecEditor::InfoPanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (32);
    closeButton.setBounds (top.removeFromRight (90).withSizeKeepingCentre (90, 28));
    r.removeFromTop (8);
    text.setBounds (r);
}

//==============================================================================
AbleAecEditor::MidiPanel::MidiPanel (AbleAecEditor& o) : owner (o)
{
    for (int t = 0; t < ctl::numTargets; ++t)
    {
        auto* row = rows.add (new Row());
        row->name.setColour (juce::Label::textColourId, kText);
        row->cc.setColour (juce::Label::textColourId, kAccent);
        row->cc.setJustificationType (juce::Justification::centred);
        row->learn.onClick = [this, t]
        {
            owner.proc.startMidiLearn ((ctl::Target) t);
            refresh();
        };
        addAndMakeVisible (row->name);
        addAndMakeVisible (row->cc);
        addAndMakeVisible (row->learn);
    }
    note.setColour (juce::Label::textColourId, kText.withAlpha (0.7f));
    note.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (note);
    closeButton.onClick = [this] { setVisible (false); };
    defaultsButton.onClick = [this] { owner.proc.resetMidiMap(); refresh(); };
    addAndMakeVisible (closeButton);
    addAndMakeVisible (defaultsButton);
}

void AbleAecEditor::MidiPanel::refresh()
{
    using ui::Str;
    const int learning = owner.proc.getMidiLearnTarget();
    for (int t = 0; t < ctl::numTargets; ++t)
    {
        auto* row = rows[t];
        row->name.setText (owner.t (ctl::info ((ctl::Target) t).name), juce::dontSendNotification);
        row->name.setFont (owner.font (13.0f));
        const int cc = owner.proc.getMidiCc ((ctl::Target) t);
        row->cc.setText (learning == t ? owner.t (Str::midiLearning)
                                       : (cc >= 0 ? "CC " + juce::String (cc) : owner.t (Str::midiNone)),
                         juce::dontSendNotification);
        row->cc.setFont (owner.font (13.0f, learning == t));
        row->learn.setButtonText (owner.t (Str::midiLearn));
    }
    note.setText (owner.t (Str::midiNote), juce::dontSendNotification);
    note.setFont (owner.font (12.5f));
    closeButton.setButtonText (owner.t (Str::close));
    defaultsButton.setButtonText (owner.t (Str::midiDefaults));
    repaint();
}

void AbleAecEditor::MidiPanel::paint (juce::Graphics& g)
{
    g.setColour (kBg);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    g.setColour (kAccent.withAlpha (0.6f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.0f, 1.0f);
    g.setColour (kText);
    g.setFont (owner.font (18.0f, true));
    g.drawText (owner.t (ui::Str::midiTitle), getLocalBounds().reduced (16, 0).removeFromTop (44), juce::Justification::centredLeft);
}

void AbleAecEditor::MidiPanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (32);
    closeButton.setBounds (top.removeFromRight (90).withSizeKeepingCentre (90, 28));
    top.removeFromRight (8);
    defaultsButton.setBounds (top.removeFromRight (110).withSizeKeepingCentre (110, 28));
    r.removeFromTop (6);
    note.setBounds (r.removeFromBottom (48));

    // two columns of rows
    const int half = (ctl::numTargets + 1) / 2;
    auto left = r.removeFromLeft (r.getWidth() / 2 - 6);
    r.removeFromLeft (12);
    auto right = r;
    for (int t = 0; t < ctl::numTargets; ++t)
    {
        auto& col = t < half ? left : right;
        auto row = col.removeFromTop (col.getHeight() / juce::jmax (1, (t < half ? half : ctl::numTargets - half) - (t < half ? t : t - half)));
        row = row.withSizeKeepingCentre (row.getWidth(), 26);
        rows[t]->learn.setBounds (row.removeFromRight (64));
        rows[t]->cc.setBounds (row.removeFromRight (96));
        rows[t]->name.setBounds (row);
    }
}
