#pragma once

#include "PluginProcessor.h"
#include "UiStrings.h"

#include <juce_audio_processors/juce_audio_processors.h>

class AbleAecEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AbleAecEditor (AbleAecProcessor&);
    ~AbleAecEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override; // app only: keyboard shortcuts

    juce::FontOptions font (float height, bool bold = false) const;

private:
    void timerCallback() override;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    // Scrollable text panel over the controls (user guide, keyboard shortcuts).
    class InfoPanel final : public juce::Component
    {
    public:
        explicit InfoPanel (AbleAecEditor& owner);
        void setContent (const juce::String& title, const juce::String& text, const juce::String& closeText);
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        AbleAecEditor& owner;
        juce::TextEditor text;
        juce::TextButton closeButton;
        juce::String title;
    };

    // MIDI mapping: which CC drives what, with MIDI learn.
    class MidiPanel final : public juce::Component
    {
    public:
        explicit MidiPanel (AbleAecEditor& owner);
        void refresh();
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        struct Row
        {
            juce::Label name, cc;
            juce::TextButton learn;
        };
        AbleAecEditor& owner;
        juce::OwnedArray<Row> rows;
        juce::TextButton closeButton, defaultsButton;
        juce::Label note;
    };

    void setupKnob (Knob&, const juce::String& paramId);
    juce::String knobParamId (const Knob&) const;
    void applyLanguage();
    void refreshPresetList();
    void savePresetDialog();
    void showPanel (juce::Component& panel);
    juce::String t (ui::Str s) const { return ui::tr (s, lang); }

    AbleAecProcessor& proc;
    const bool isApp;
    ui::Lang lang = ui::Lang::en;
    juce::Typeface::Ptr typeRegular, typeBold;

    Knob filterLen, step, refDelay, resAmount, resFloor;

    juce::ToggleButton freezeButton, offButton, autoButton;
    juce::ComboBox modeBox, latencyBox, refModeBox, langBox, presetBox;
    juce::TextButton resetButton, guideButton, savePresetButton, deletePresetButton, midiButton, keysButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAtt, offAtt, autoAtt;
    bool autoShown = false;
    void setAutoKnobs (bool on);
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt, latencyAtt, refModeAtt;
    int shownPreset = -2, shownPresetCount = -1;

    InfoPanel guidePanel { *this }, keysPanel { *this };
    MidiPanel midiPanel { *this };
    juce::TooltipWindow tooltipWindow { this, 600 };

    // cached meter values for painting
    float erle = 0, delayMs = 0, refDb = -120, micDb = -120, outDb = -120;
    bool converged = false, sidechain = false, echoPresent = false;
    int refChannels = 0, latencySamples = 0;

    juce::Rectangle<int> meterArea, statusArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AbleAecEditor)
};
