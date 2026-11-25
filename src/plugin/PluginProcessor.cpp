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

    resetMidiMap();
    reloadUserPresets();
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

void AbleAecProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    handleMidi (midi);
    midi.clear();
    if (wrapperType == wrapperType_Standalone)
        applyInputRouting (buffer);

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
    auto state = apvts.copyState();
    state.setProperty ("currentPreset", currentPreset.load(), nullptr);
    state.setProperty ("inputRouting", inputRouting.load(), nullptr);
    state.removeChild (state.getChildWithName ("MIDI"), nullptr);
    juce::ValueTree midiMap ("MIDI");
    for (int t = 0; t < ctl::numTargets; ++t)
        midiMap.setProperty ("cc" + juce::String (t), midiCc[(size_t) t].load(), nullptr);
    state.appendChild (midiMap, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AbleAecProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    auto state = juce::ValueTree::fromXml (*xml);
    if (auto midiMap = state.getChildWithName ("MIDI"); midiMap.isValid())
    {
        for (int t = 0; t < ctl::numTargets; ++t)
            if (midiMap.hasProperty ("cc" + juce::String (t)))
                midiCc[(size_t) t].store ((int) midiMap["cc" + juce::String (t)]);
        state.removeChild (midiMap, nullptr);
    }
    currentPreset.store ((int) state.getProperty ("currentPreset", currentPreset.load()));
    inputRouting.store ((int) state.getProperty ("inputRouting", 0));
    apvts.replaceState (state);
}

//==============================================================================
// presets

juce::File AbleAecProcessor::getUserPresetFolder()
{
   #if JUCE_MAC
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support");
   #else
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #endif
    return base.getChildFile ("AbleAEC").getChildFile ("Presets");
}

void AbleAecProcessor::reloadUserPresets()
{
    userPresets.clear();
    for (const auto& f : getUserPresetFolder().findChildFiles (juce::File::findFiles, false, "*.xml"))
        if (auto xml = juce::XmlDocument::parse (f); xml != nullptr && xml->hasTagName ("AbleAECPreset"))
            userPresets.add ({ xml->getStringAttribute ("name", f.getFileNameWithoutExtension()), f });
    std::sort (userPresets.begin(), userPresets.end(),
               [] (const UserPreset& a, const UserPreset& b) { return a.name.compareNatural (b.name) < 0; });
}

juce::String AbleAecProcessor::getPresetName (int index, ui::Lang lang) const
{
    const int nf = (int) ctl::factoryPresets().size();
    if (index >= 0 && index < nf)
        return ui::tr (ctl::factoryPresets()[(size_t) index].name, lang);
    if (index >= nf && index < nf + userPresets.size())
        return userPresets[index - nf].name;
    return {};
}

void AbleAecProcessor::applyPreset (int index)
{
    const int nf = (int) ctl::factoryPresets().size();
    if (index < 0 || index >= getNumPresets())
        return;

    // start from the defaults so a preset fully defines the sound
    for (auto* id : ctl::presetParamIds())
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->getDefaultValue());

    if (index < nf)
    {
        for (const auto& [id, value] : ctl::factoryPresets()[(size_t) index].values)
            setParamReal (id, value);
    }
    else if (auto xml = juce::XmlDocument::parse (userPresets[index - nf].file))
    {
        for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
            setParamReal (e->getStringAttribute ("id").toRawUTF8(), (float) e->getDoubleAttribute ("value"));
    }
    currentPreset.store (index);
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

bool AbleAecProcessor::saveUserPreset (const juce::String& rawName)
{
    const auto name = rawName.trim();
    if (name.isEmpty())
        return false;
    auto folder = getUserPresetFolder();
    if (! folder.createDirectory())
        return false;

    juce::XmlElement xml ("AbleAECPreset");
    xml.setAttribute ("name", name);
    xml.setAttribute ("version", JucePlugin_VersionString);
    for (auto* id : ctl::presetParamIds())
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
        {
            auto* e = xml.createNewChildElement ("PARAM");
            e->setAttribute ("id", id);
            e->setAttribute ("value", p->convertFrom0to1 (p->getValue()));
        }
    const auto file = folder.getChildFile (juce::File::createLegalFileName (name) + ".xml");
    if (! xml.writeTo (file))
        return false;

    reloadUserPresets();
    for (int i = 0; i < userPresets.size(); ++i)
        if (userPresets[i].file == file)
            currentPreset.store ((int) ctl::factoryPresets().size() + i);
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
    return true;
}

bool AbleAecProcessor::deleteUserPreset (int index)
{
    const int nf = (int) ctl::factoryPresets().size();
    if (index < nf || index >= getNumPresets())
        return false;
    const bool ok = userPresets[index - nf].file.deleteFile();
    reloadUserPresets();
    currentPreset.store (0);
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
    return ok;
}

void AbleAecProcessor::setCurrentProgram (int index)
{
    if (juce::MessageManager::existsAndIsCurrentThread())
        applyPreset (index);
    else
    {
        pendingPreset.store (index);
        triggerAsyncUpdate();
    }
}

void AbleAecProcessor::stepPreset (int delta)
{
    const int n = getNumPresets();
    if (n > 0)
        applyPreset (((juce::jmax (0, currentPreset.load()) + delta) % n + n) % n);
}

void AbleAecProcessor::handleAsyncUpdate()
{
    if (const int p = pendingPreset.exchange (-1); p >= 0)
        applyPreset (p);
    if (const int d = pendingPresetStep.exchange (0); d != 0)
        stepPreset (d);
}

//==============================================================================
// parameter actions (shared by MIDI and keyboard shortcuts)

void AbleAecProcessor::setParamReal (const char* id, float value)
{
    if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
        p->setValueNotifyingHost (p->convertTo0to1 (p->getNormalisableRange().snapToLegalValue (value)));
}

void AbleAecProcessor::nudgeParam (const char* id, float delta)
{
    if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
        setParamReal (id, p->convertFrom0to1 (p->getValue()) + delta);
}

void AbleAecProcessor::toggleParam (const char* id)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
}

void AbleAecProcessor::cycleChoice (const char* id)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id)))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) ((p->getIndex() + 1) % p->choices.size())));
}

//==============================================================================
// MIDI

void AbleAecProcessor::resetMidiMap()
{
    for (int t = 0; t < ctl::numTargets; ++t)
        midiCc[(size_t) t].store (ctl::info ((ctl::Target) t).defaultCc);
    lastCcValue.fill (0);
}

void AbleAecProcessor::handleMidi (const juce::MidiBuffer& midi) noexcept
{
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isProgramChange())
        {
            pendingPreset.store (msg.getProgramChangeNumber());
            triggerAsyncUpdate();
            continue;
        }
        if (! msg.isController())
            continue;

        const int cc = msg.getControllerNumber();
        const int value = msg.getControllerValue();

        if (const int learn = midiLearn.exchange (-1); learn >= 0)
        {
            for (auto& m : midiCc) // one CC drives one target
                if (m.load() == cc) m.store (-1);
            midiCc[(size_t) learn].store (cc);
            lastCcValue[(size_t) learn] = value;
            continue;
        }

        for (int t = 0; t < ctl::numTargets; ++t)
        {
            if (midiCc[(size_t) t].load() != cc)
                continue;
            const auto& inf = ctl::info ((ctl::Target) t);
            const bool rising = value >= 64 && lastCcValue[(size_t) t] < 64;
            lastCcValue[(size_t) t] = value;
            auto* param = inf.paramId != nullptr ? apvts.getParameter (inf.paramId) : nullptr;

            switch (inf.kind)
            {
                case ctl::Kind::continuous:
                    if (param != nullptr) param->setValueNotifyingHost ((float) value / 127.0f);
                    break;
                case ctl::Kind::toggle:
                    if (param != nullptr) param->setValueNotifyingHost (value >= 64 ? 1.0f : 0.0f);
                    break;
                case ctl::Kind::choice:
                    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
                    {
                        const int n = choice->choices.size();
                        choice->setValueNotifyingHost (choice->convertTo0to1 ((float) juce::jmin (n - 1, value * n / 128)));
                    }
                    break;
                case ctl::Kind::trigger:
                    if (! rising) break;
                    if ((ctl::Target) t == ctl::Target::reset) requestReset();
                    else
                    {
                        pendingPresetStep.fetch_add ((ctl::Target) t == ctl::Target::presetNext ? 1 : -1);
                        triggerAsyncUpdate();
                    }
                    break;
            }
        }
    }
}

//==============================================================================
// app input routing

void AbleAecProcessor::setInputRouting (InputRouting r)
{
    inputRouting.store ((int) r);
}

void AbleAecProcessor::applyInputRouting (juce::AudioBuffer<float>& buffer) noexcept
{
    // "1 mic, 2 speaker": device input 1 is the mic (to both main channels), input 2 the reference
    if (getInputRouting() != InputRouting::mic1Ref2 || buffer.getNumChannels() < 4)
        return;
    const int n = buffer.getNumSamples();
    buffer.copyFrom (2, 0, buffer, 1, 0, n);
    buffer.copyFrom (3, 0, buffer, 1, 0, n);
    buffer.copyFrom (1, 0, buffer, 0, 0, n);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AbleAecProcessor(); }
