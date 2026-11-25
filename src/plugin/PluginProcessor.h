#pragma once

#include "AecCore.h"
#include "Controls.h"
#include "UiStrings.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <vector>

class AbleAecProcessor final : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kNumLatencyModes = 2; // 0 = quality (~21 ms), 1 = low latency (~11 ms)
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
    bool acceptsMidi() const override { return true; } // MIDI control (CC, Program Change)
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // host programs = all presets (built-in first, then the user's)
    int getNumPrograms() override { return juce::jmax (1, getNumPresets()); }
    int getCurrentProgram() override { return juce::jmax (0, currentPreset.load()); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override { return getPresetName (index, getUiLanguage()); }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // UI -> audio
    void requestReset() noexcept { resetRequested.store (true); }

    // Interface language, saved with the project (not automatable). Defaults to the system language.
    ui::Lang getUiLanguage() const;
    void setUiLanguage (ui::Lang l);

    // ---- presets (message thread). Index: built-in presets first, then the user's (sorted by name).
    int getNumPresets() const { return (int) ctl::factoryPresets().size() + userPresets.size(); }
    juce::String getPresetName (int index, ui::Lang lang) const;
    bool isFactoryPreset (int index) const { return index >= 0 && index < (int) ctl::factoryPresets().size(); }
    void applyPreset (int index);
    bool saveUserPreset (const juce::String& name); // same name overwrites
    bool deleteUserPreset (int index);
    int getCurrentPresetIndex() const { return currentPreset.load(); }
    static juce::File getUserPresetFolder();
    void reloadUserPresets();

    // ---- MIDI control (CC numbers per target, -1 = none; learn assigns the next CC that arrives)
    int getMidiCc (ctl::Target t) const { return midiCc[(size_t) t].load(); }
    void setMidiCc (ctl::Target t, int cc) { midiCc[(size_t) t].store (cc); }
    void resetMidiMap();
    void startMidiLearn (ctl::Target t) { midiLearn.store ((int) t); }
    int getMidiLearnTarget() const { return midiLearn.load(); }

    // ---- app only: which input channels carry the mic and the speaker reference
    enum class InputRouting { mic12Ref34 = 0, mic1Ref2 = 1 };
    InputRouting getInputRouting() const { return (InputRouting) inputRouting.load(); }
    void setInputRouting (InputRouting r);

    // ---- actions shared by MIDI and keyboard shortcuts (message thread or audio thread)
    void setParamReal (const char* id, float value); // in the parameter's own units
    void nudgeParam (const char* id, float delta);
    void toggleParam (const char* id);
    void cycleChoice (const char* id);
    void stepPreset (int delta);

    // audio -> UI meters
    struct Meters
    {
        std::atomic<float> erleDb { 0.0f }, echoDelayMs { 0.0f }, leakage { 1.0f };
        std::atomic<float> refLevelDb { -120.0f }, micLevelDb { -120.0f }, outLevelDb { -120.0f };
        std::atomic<bool> converged { false }, sidechainConnected { false };
        std::atomic<int> refChannels { 0 }, latencySamples { 0 };
        // values in effect (what Auto chose, or the knobs)
        std::atomic<float> effFilterMs { 250.0f }, effResAmount { 0.2f }, effResFloorDb { -18.0f };
        std::atomic<bool> echoPresent { false };
    } meters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void handleAsyncUpdate() override;
    void handleMidi (const juce::MidiBuffer& midi) noexcept;
    void applyInputRouting (juce::AudioBuffer<float>& buffer) noexcept;
    ableaec::AecSettings readSettings() const noexcept;
    void switchLatencyMode (int mode) noexcept;
    void runBlock (int numMic) noexcept;

    // [latency mode][mic channel], all prepared up front so switching never allocates
    std::array<std::array<ableaec::AecCore, kMaxChannels>, kNumLatencyModes> cores;
    int activeMode = 0;
    int B = 512;               // block size of the active mode
    int activeRefs = 2;
    bool channelsLinked = true; // mic L == R: process once, copy

    // block FIFOs (sized for the largest block)
    std::array<std::vector<float>, kMaxChannels> micIn, outFifo;
    std::array<std::vector<float>, ableaec::AecCore::kMaxRefs> refIn;
    int fifoPos = 0;

    // reference delay lines
    std::array<std::vector<float>, ableaec::AecCore::kMaxRefs> refDelayLine;
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
    std::atomic<float>* pAuto = nullptr;
    std::atomic<float>* pLatency = nullptr;
    std::atomic<float>* pRefMode = nullptr;

    struct UserPreset { juce::String name; juce::File file; };
    juce::Array<UserPreset> userPresets;
    std::atomic<int> currentPreset { 0 };
    std::atomic<int> pendingPreset { -1 }, pendingPresetStep { 0 }; // from the audio thread

    std::array<std::atomic<int>, (size_t) ctl::numTargets> midiCc {};
    std::array<int, (size_t) ctl::numTargets> lastCcValue {};
    std::atomic<int> midiLearn { -1 };
    std::atomic<int> inputRouting { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AbleAecProcessor)
};
