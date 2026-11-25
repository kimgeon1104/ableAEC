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

private:
    void timerCallback() override;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    // In-plugin user guide, shown over the controls.
    class GuidePanel final : public juce::Component
    {
    public:
        explicit GuidePanel (AbleAecEditor& owner);
        void setContent (ui::Lang lang);
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        AbleAecEditor& owner;
        juce::TextEditor text;
        juce::TextButton closeButton;
        juce::String title;
    };

    void setupKnob (Knob&, const juce::String& paramId);
    void applyLanguage();
    juce::String t (ui::Str s) const { return ui::tr (s, lang); }
    juce::FontOptions font (float height, bool bold = false) const;

    AbleAecProcessor& proc;
    ui::Lang lang = ui::Lang::en;
    juce::Typeface::Ptr typeRegular, typeBold;

    Knob filterLen, step, refDelay, resAmount, resFloor;

    juce::ToggleButton freezeButton, offButton;
    juce::ComboBox modeBox, latencyBox, refModeBox, langBox;
    juce::TextButton resetButton, guideButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAtt, offAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt, latencyAtt, refModeAtt;

    GuidePanel guidePanel { *this };
    juce::TooltipWindow tooltipWindow { this, 600 };

    // cached meter values for painting
    float erle = 0, delayMs = 0, refDb = -120, micDb = -120, outDb = -120;
    bool converged = false, sidechain = false;
    int refChannels = 0, latencySamples = 0;

    juce::Rectangle<int> meterArea, statusArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AbleAecEditor)
};
