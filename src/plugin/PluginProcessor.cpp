#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <cstring>

namespace
{
float toDb (float x) { return x > 1.0e-6f ? 20.0f * std::log10 (x) : -120.0f; }
} // namespace

AbleAecProcessor::AbleAecProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AbleAEC", createLayout())
{
    pFilterLen = apvts.getRawParameterValue ("filterLen");
    pStep = apvts.getRawParameterValue ("step");
    pRefDelay = apvts.getRawParameterValue ("refDelay");
    pResAmount = apvts.getRawParameterValue ("resAmount");
    pResFloor = apvts.getRawParameterValue ("resFloor");
    pFreeze = apvts.getRawParameterValue ("freeze");
    pMode = apvts.getRawParameterValue ("mode");
    pBypass = apvts.getRawParameterValue ("aecOff");
    pAuto = apvts.getRawParameterValue ("auto");
    pLatency = apvts.getRawParameterValue ("latency");
    pRefMode = apvts.getRawParameterValue ("refMode");
}

juce::AudioProcessorValueTreeState::ParameterLayout AbleAecProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;

    auto ms = [] (float v, int) { return String (v, 0) + " ms"; };
    auto db = [] (float v, int) { return String (v, 0) + " dB"; };
    auto pct = [] (float v, int) { return String (juce::roundToInt (v * 100.0f)) + " %"; };

    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "filterLen", 1 }, "Filter Length",
                                                  NormalisableRange<float> (20.0f, kMaxFilterMs, 1.0f, 0.6f), 250.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "step", 1 }, "Adapt Speed",
                                                  NormalisableRange<float> (0.05f, 1.0f, 0.01f), 0.5f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "refDelay", 1 }, "Ref Delay",
                                                  NormalisableRange<float> (0.0f, kMaxRefDelayMs, 0.1f, 0.5f), 0.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "resAmount", 1 }, "Residual Suppression",
                                                  NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.2f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "resFloor", 1 }, "Suppression Floor",
                                                  NormalisableRange<float> (-60.0f, 0.0f, 1.0f), -18.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "freeze", 1 }, "Freeze Adaptation", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Output",
                                                   StringArray { "Clean", "Linear AEC only", "Echo estimate", "Raw mic" }, 0));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "aecOff", 1 }, "AEC Off", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "auto", 1 }, "Auto", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "latency", 1 }, "Latency",
                                                   StringArray { "Quality (~21 ms)", "Low (~11 ms)" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "refMode", 1 }, "Reference",
                                                   StringArray { "Stereo (L/R)", "Mono (L+R)" }, 0));
    return l;
}

bool AbleAecProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()) return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void AbleAecProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    int maxB = 0;
    for (int m = 0; m < kNumLatencyModes; ++m)
    {
        const int b = ableaec::AecCore::blockSizeFor (sampleRate, m == 1);
        maxB = juce::jmax (maxB, b);
        for (auto& c : cores[(size_t) m])
        {
            c.prepare (sampleRate, kMaxFilterMs, b, ableaec::AecCore::kMaxRefs,
                       ableaec::AecCore::partitionSizeFor (sampleRate, m == 1));
            c.setSettings (readSettings());
        }
    }
    for (auto& v : micIn) v.assign ((size_t) maxB, 0.0f);
    for (auto& v : outFifo) v.assign ((size_t) maxB, 0.0f);
    for (auto& v : refIn) v.assign ((size_t) maxB, 0.0f);

    const auto delayLen = (size_t) std::ceil (kMaxRefDelayMs * 0.001 * sampleRate) + 2;
    for (auto& v : refDelayLine) v.assign (delayLen, 0.0f);
    refDelayWrite = 0;

    activeRefs = 2;
    channelsLinked = true;
    activeMode = -1;
    switchLatencyMode (juce::jlimit (0, kNumLatencyModes - 1, (int) pLatency->load()));
}

void AbleAecProcessor::switchLatencyMode (int mode) noexcept
{
    activeMode = mode;
    for (auto& c : cores[(size_t) mode])
    {
        c.reset();
        c.setActiveRefs (activeRefs);
    }
    channelsLinked = true;
    B = cores[(size_t) mode][0].getBlockSize();
    for (auto& v : micIn) std::fill (v.begin(), v.end(), 0.0f);
    for (auto& v : outFifo) std::fill (v.begin(), v.end(), 0.0f);
    for (auto& v : refIn) std::fill (v.begin(), v.end(), 0.0f);
    fifoPos = 0;

    // B samples of FIFO + B samples inside AecCore
    setLatencySamples (2 * B);
    meters.latencySamples.store (2 * B);
}

ableaec::AecSettings AbleAecProcessor::readSettings() const noexcept
{
    ableaec::AecSettings s;
    s.filterLengthMs = pFilterLen->load();
    s.stepSize = pStep->load();
    s.resAmount = pResAmount->load();
    s.resFloorDb = pResFloor->load();
    s.freeze = pFreeze->load() > 0.5f;
    s.autoTune = pAuto->load() > 0.5f;
    s.mode = pBypass->load() > 0.5f ? ableaec::OutputMode::RawMic
                                    : (ableaec::OutputMode) juce::jlimit (0, 3, (int) pMode->load());
    return s;
}

void AbleAecProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int wantedMode = juce::jlimit (0, kNumLatencyModes - 1, (int) pLatency->load());
    if (wantedMode != activeMode)
        switchLatencyMode (wantedMode);

    auto& active = cores[(size_t) activeMode];
    if (resetRequested.exchange (false))
        for (auto& c : active) c.resetFilter();

    const auto settings = readSettings();
    for (auto& c : active) c.setSettings (settings);

    auto mainIn = getBusBuffer (buffer, true, 0);
    auto mainOut = getBusBuffer (buffer, false, 0);
    const int numMic = juce::jmin (kMaxChannels, mainIn.getNumChannels());
    const int numSamples = buffer.getNumSamples();

    const float* sc[2] = { nullptr, nullptr };
    int numSc = 0;
    if (getBusCount (true) > 1 && getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled())
    {
        auto scBuf = getBusBuffer (buffer, true, 1);
        numSc = juce::jmin (2, scBuf.getNumChannels());
        for (int c = 0; c < numSc; ++c) sc[c] = scBuf.getReadPointer (c);
    }
    meters.sidechainConnected.store (numSc > 0);

    // stereo reference when the sidechain is stereo and the user did not ask for a mono sum
    const int wantedRefs = (numSc >= 2 && pRefMode->load() < 0.5f) ? 2 : 1;
    if (wantedRefs != activeRefs)
    {
        activeRefs = wantedRefs;
        for (auto& c : active) c.setActiveRefs (activeRefs);
    }
    meters.refChannels.store (numSc > 0 ? activeRefs : 0);

    const int delayLen = (int) refDelayLine[0].size();
    const int refDelaySamples = juce::jlimit (0, delayLen - 1,
                                              (int) std::lround (pRefDelay->load() * 0.001 * currentSampleRate));

    for (int i = 0; i < numSamples; ++i)
    {
        // reference channels (stereo, or mono sum), then the optional delay
        float r[2] = { 0.0f, 0.0f };
        if (activeRefs == 2)
        {
            r[0] = sc[0][i];
            r[1] = sc[1][i];
        }
        else
        {
            for (int c = 0; c < numSc; ++c) r[0] += sc[c][i];
            if (numSc > 1) r[0] *= 1.0f / (float) numSc;
        }

        int readIdx = refDelayWrite - refDelaySamples;
        if (readIdx < 0) readIdx += delayLen;
        for (int c = 0; c < activeRefs; ++c)
        {
            refDelayLine[(size_t) c][(size_t) refDelayWrite] = r[c];
            refIn[(size_t) c][(size_t) fifoPos] = refDelayLine[(size_t) c][(size_t) readIdx];
            refPeakAcc = juce::jmax (refPeakAcc, std::abs (r[c]));
        }
        if (++refDelayWrite >= delayLen) refDelayWrite = 0;

        for (int c = 0; c < numMic; ++c)
        {
            const float m = mainIn.getReadPointer (c)[i];
            micIn[(size_t) c][(size_t) fifoPos] = m;
            micPeakAcc = juce::jmax (micPeakAcc, std::abs (m));
        }
        for (int c = 0; c < numMic; ++c)
        {
            const float o = outFifo[(size_t) c][(size_t) fifoPos];
            mainOut.getWritePointer (c)[i] = o;
            outPeakAcc = juce::jmax (outPeakAcc, std::abs (o));
        }

        if (++fifoPos == B)
        {
            runBlock (numMic);
            fifoPos = 0;
        }
    }

    // any extra output channels (should not happen with the supported layouts)
    for (int c = numMic; c < mainOut.getNumChannels(); ++c)
        mainOut.clear (c, 0, numSamples);
}

void AbleAecProcessor::runBlock (int numMic) noexcept
{
    auto& active = cores[(size_t) activeMode];
    const float* refs[2] = { refIn[0].data(), refIn[1].data() };

    // stereo tracks fed by a mono mic carry identical channels: process once and copy
    const bool identical = numMic == 2
                           && std::memcmp (micIn[0].data(), micIn[1].data(), sizeof (float) * (size_t) B) == 0;
    if (numMic == 2 && channelsLinked && ! identical)
        active[1].reset(); // channel 1 was idle, start it fresh
    channelsLinked = numMic < 2 || identical;

    active[0].processBlock (refs, micIn[0].data(), outFifo[0].data());
    if (numMic == 2)
    {
        if (channelsLinked)
            std::copy (outFifo[0].begin(), outFifo[0].begin() + B, outFifo[1].begin());
        else
            active[1].processBlock (refs, micIn[1].data(), outFifo[1].data());
    }

    // meters (channel 0 is representative)
    const auto& c0 = active[0];
    meters.erleDb.store (c0.getErleDb());
    meters.echoDelayMs.store (c0.getEchoDelayMs() + pRefDelay->load());
    meters.leakage.store (c0.getLeakage());
    meters.converged.store (c0.isConverged());
    meters.effFilterMs.store (c0.getEffectiveFilterLengthMs());
    meters.effResAmount.store (c0.getEffectiveResAmount());
    meters.effResFloorDb.store (c0.getEffectiveResFloorDb());
    meters.echoPresent.store (c0.isEchoPresent());

    // fall ~28 dB/s regardless of block size
    const float fall = 28.0f * (float) B / (float) currentSampleRate;
    auto decay = [fall] (std::atomic<float>& m, float& acc)
    {
        const float db = toDb (acc);
        const float prev = m.load();
        m.store (db > prev ? db : juce::jmax (db, prev - fall));
        acc = 0.0f;
    };
    decay (meters.refLevelDb, refPeakAcc);
    decay (meters.micLevelDb, micPeakAcc);
    decay (meters.outLevelDb, outPeakAcc);
}

juce::AudioProcessorEditor* AbleAecProcessor::createEditor() { return new AbleAecEditor (*this); }

ui::Lang AbleAecProcessor::getUiLanguage() const
{
    const auto v = apvts.state.getProperty ("uiLanguage").toString();
    if (v == "ko") return ui::Lang::ko;
    if (v == "en") return ui::Lang::en;
    return juce::SystemStats::getUserLanguage().startsWithIgnoreCase ("ko")
                   || juce::SystemStats::getDisplayLanguage().startsWithIgnoreCase ("ko")
               ? ui::Lang::ko
               : ui::Lang::en;
}

void AbleAecProcessor::setUiLanguage (ui::Lang l)
{
    apvts.state.setProperty ("uiLanguage", l == ui::Lang::ko ? "ko" : "en", nullptr);
}

void AbleAecProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AbleAecProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AbleAecProcessor(); }
