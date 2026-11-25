#pragma once

#include "PluginProcessor.h"

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

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name);

    AbleAecProcessor& proc;
    Knob filterLen, step, refDelay, resAmount, resFloor;

    juce::ToggleButton freezeButton { "Freeze" }, offButton { "AEC Off" };
    juce::ComboBox modeBox;
    juce::TextButton resetButton { "Reset Filter" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAtt, offAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt;

    // cached meter values for painting
    float erle = 0, delayMs = 0, refDb = -120, micDb = -120, outDb = -120;
    bool converged = false, sidechain = false;

    juce::Rectangle<int> meterArea, statusArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AbleAecEditor)
};
