#pragma once

#include "AecCore.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

class AbleAecProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr float kMaxFilterMs = 500.0f;
    static constexpr float kMaxRefDelayMs = 500.0f;

    AbleAecProcessor();
    ~AbleAecProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // UI -> audio
    void requestReset() noexcept { resetRequested.store (true); }

    // audio -> UI meters
    struct Meters
    {
        std::atomic<float> erleDb { 0.0f }, echoDelayMs { 0.0f }, leakage { 1.0f };
        std::atomic<float> refLevelDb { -120.0f }, micLevelDb { -120.0f }, outLevelDb { -120.0f };
        std::atomic<bool> converged { false }, sidechainConnected { false };
    } meters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    ableaec::AecSettings readSettings() const noexcept;
    void runBlock (int numMicChannels) noexcept;

    static constexpr int B = ableaec::AecCore::B;

    std::array<ableaec::AecCore, kMaxChannels> cores;
    bool channelsLinked = true; // mic L == R: process once, copy

    // block FIFOs (B samples)
    std::array<std::array<float, B>, kMaxChannels> micIn {}, outFifo {};
    std::array<float, B> refIn {};
    int fifoPos = 0;

    // reference delay line
    std::vector<float> refDelayLine;
    int refDelayWrite = 0;

    double currentSampleRate = 48000.0;
    std::atomic<bool> resetRequested { false };

    // level meter accumulators (audio thread only)
    float refPeakAcc = 0.0f, micPeakAcc = 0.0f, outPeakAcc = 0.0f;

    std::atomic<float>* pFilterLen = nullptr;
    std::atomic<float>* pStep = nullptr;
    std::atomic<float>* pRefDelay = nullptr;
    std::atomic<float>* pResAmount = nullptr;
    std::atomic<float>* pResFloor = nullptr;
    std::atomic<float>* pFreeze = nullptr;
    std::atomic<float>* pMode = nullptr;
    std::atomic<float>* pBypass = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AbleAecProcessor)
};
