// End-to-end test: loads the built AbleAEC.vst3 the way a DAW does, enables the sidechain bus,
// feeds mic (main input) + speaker (sidechain) signals in host-sized blocks and checks
//   1. bus layout (main stereo in/out + stereo sidechain) is accepted
//   2. reported latency matches the actual output alignment (Raw mic mode)
//   3. echo is cancelled through the real plugin wrapper (Clean mode)
//   4. with the sidechain bus disabled the plugin is a transparent (delayed) pass-through
//
// usage: AbleAecHostTest <path/to/AbleAEC.vst3>

#include "SimSignals.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdio>
#include <memory>

using namespace sim;

namespace
{
int failures = 0;
void check (bool ok, const char* what)
{
    std::printf ("  [%s] %s\n", ok ? " OK " : "FAIL", what);
    if (! ok) ++failures;
}

juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& p, const juce::String& name)
{
    for (auto* prm : p.getParameters())
        if (prm->getName (64) == name) return prm;
    return nullptr;
}

struct Result
{
    std::vector<float> out;
    int latency = 0;
};

void setChoice (juce::AudioPluginInstance& plugin, const juce::String& name, int index)
{
    if (auto* p = findParam (plugin, name))
        p->setValueNotifyingHost ((float) index / (float) juce::jmax (1, p->getNumSteps() - 1));
}

// mic is mono and duplicated to both main channels; refL / refR feed the stereo sidechain
Result run (juce::AudioPluginInstance& plugin, const std::vector<float>& mic, const std::vector<float>& refL,
            const std::vector<float>& refR, int modeIndex, bool sidechainOn, int hostBlock, int latencyMode = 0)
{
    auto layout = plugin.getBusesLayout();
    layout.inputBuses.getReference (1) = sidechainOn ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
    plugin.setBusesLayout (layout);

    setChoice (plugin, "Output", modeIndex);
    setChoice (plugin, "Latency", latencyMode);

    plugin.prepareToPlay (kSr, hostBlock);
    Result r;
    r.latency = plugin.getLatencySamples();
    r.out.assign (mic.size(), 0.0f);

    const int numCh = juce::jmax (plugin.getTotalNumInputChannels(), plugin.getTotalNumOutputChannels());
    juce::AudioBuffer<float> buf (numCh, hostBlock);
    juce::MidiBuffer midi;
    const int total = (int) mic.size();
    for (int pos = 0; pos < total; pos += hostBlock)
    {
        const int n = juce::jmin (hostBlock, total - pos);
        buf.setSize (numCh, n, false, false, true);
        buf.clear();
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, mic.data() + pos, n);
        if (sidechainOn && numCh >= 4)
        {
            buf.copyFrom (2, 0, refL.data() + pos, n);
            buf.copyFrom (3, 0, refR.data() + pos, n);
        }
        plugin.processBlock (buf, midi);
        std::copy (buf.getReadPointer (0), buf.getReadPointer (0) + n, r.out.begin() + pos);
    }
    plugin.releaseResources();
    return r;
}

double pw (const std::vector<float>& v, int a, int b)
{
    double s = 0;
    for (int i = a; i < b; ++i) s += (double) v[(size_t) i] * v[(size_t) i];
    return s / (b - a) + 1e-20;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: %s <AbleAEC.vst3>\n", argv[0]);
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile (types, juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]).getFullPathName());
    if (types.isEmpty())
    {
        std::printf ("FAIL: no plugin found in %s\n", argv[1]);
        return 1;
    }

    juce::String error;
    auto plugin = format.createInstanceFromDescription (*types[0], kSr, 480, error);
    if (plugin == nullptr)
    {
        std::printf ("FAIL: could not instantiate: %s\n", error.toRawUTF8());
        return 1;
    }
    std::printf ("Loaded %s (%d input buses, %d output buses)\n", plugin->getName().toRawUTF8(),
                 plugin->getBusCount (true), plugin->getBusCount (false));

    check (plugin->getBusCount (true) == 2, "plugin exposes main input + sidechain input");
    auto layout = plugin->getBusesLayout();
    if (layout.inputBuses.size() >= 2)
        layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
    check (plugin->checkBusesLayoutSupported (layout), "stereo main + stereo sidechain layout supported");
    check (plugin->setBusesLayout (layout), "layout applied");
    if (failures > 0) return 1;

    // signals: 14 s, two different loudspeaker signals (stereo backing) through two room paths
    const int total = (int) (14 * kSr);
    const auto refL = speechLike (total, 11);
    const auto refR = speechLike (total, 12);
    const auto echoL = convolve (refL, roomImpulse (18.0, 0.12, 5), 0, total);
    const auto echoR = convolve (refR, roomImpulse (24.0, 0.12, 6), 0, total);
    std::vector<float> mic ((size_t) total);
    for (int i = 0; i < total; ++i)
        mic[(size_t) i] = echoL[(size_t) i] + echoR[(size_t) i] + 1.0e-4f * std::sin ((float) i * 0.01f);

    auto erleOf = [&] (const Result& r)
    {
        const int a = (int) (10 * kSr), b = total - 1;
        double num = 0;
        for (int i = a; i < b; ++i) num += (double) mic[(size_t) (i - r.latency)] * mic[(size_t) (i - r.latency)];
        return 10.0 * std::log10 ((num / (b - a) + 1e-20) / pw (r.out, a, b));
    };
    auto alignmentError = [&] (const Result& r)
    {
        double maxErr = 0;
        for (int i = r.latency; i < total; ++i)
            maxErr = std::max (maxErr, (double) std::abs (r.out[(size_t) i] - mic[(size_t) (i - r.latency)]));
        return maxErr;
    };

    // 1. latency / alignment in Raw mic mode, odd host block size, both latency modes
    for (int lm = 0; lm < 2; ++lm)
    {
        const auto r = run (*plugin, mic, refL, refR, 3, true, 441, lm);
        const double maxErr = alignmentError (r);
        const int expected = lm == 0 ? 1024 : 512;
        std::printf ("  [%s] reported latency %d samples, max alignment error %.2e\n",
                     lm == 0 ? "Quality" : "Low", r.latency, maxErr);
        check (r.latency == expected, lm == 0 ? "Quality mode reports 1024 samples" : "Low mode reports 512 samples");
        check (maxErr < 1e-6, "Raw mic output == input delayed by reported latency");
    }

    // 2. echo cancellation through the wrapper with a stereo (L != R) sidechain
    {
        const auto stereo = run (*plugin, mic, refL, refR, 0, true, 480);
        const double erleStereo = erleOf (stereo);
        setChoice (*plugin, "Reference", 1);
        const auto mono = run (*plugin, mic, refL, refR, 0, true, 480);
        setChoice (*plugin, "Reference", 0);
        const double erleMono = erleOf (mono);
        std::printf ("  ERLE through plugin (10-14 s): stereo reference %.1f dB, mono-sum reference %.1f dB\n",
                     erleStereo, erleMono);
        check (erleStereo > 25.0, "stereo-reference echo reduced by more than 25 dB");
        check (erleStereo > erleMono + 6.0, "stereo reference clearly beats mono sum for L != R playback");
    }

    // 3. sidechain disabled -> transparent
    {
        const auto r = run (*plugin, mic, refL, refR, 0, false, 512);
        const double maxErr = alignmentError (r);
        std::printf ("  no-sidechain max deviation %.2e\n", maxErr);
        check (maxErr < 1e-4, "without sidechain the plugin passes the mic through");
    }

    // 4. presets (host programs) and MIDI control
    {
        auto* res = findParam (*plugin, "Residual Suppression");
        auto* autoP = findParam (*plugin, "Auto");
        const int programs = plugin->getNumPrograms();
        std::printf ("  programs: %d (", programs);
        for (int i = 0; i < juce::jmin (programs, 7); ++i)
            std::printf ("%s%s", i ? ", " : "", plugin->getProgramName (i).toRawUTF8());
        std::printf (")\n");
        check (programs >= 7, "built-in presets are exposed as host programs");

        plugin->setCurrentProgram (2); // Talk / call
        const auto talkRes = res != nullptr ? res->getText (res->getValue(), 32) : juce::String();
        plugin->setCurrentProgram (1); // Singing
        const auto singRes = res != nullptr ? res->getText (res->getValue(), 32) : juce::String();
        const bool singAuto = autoP != nullptr && autoP->getValue() > 0.5f;
        std::printf ("  Talk/call residual suppression %s, Singing %s (Auto %s)\n", talkRes.toRawUTF8(), singRes.toRawUTF8(),
                     singAuto ? "on" : "off");
        check (talkRes == "40 %" && singRes == "15 %" && singAuto, "presets set their values");

        // MIDI CC 23 (default: Residual Supp.) and CC 25 (Auto)
        plugin->prepareToPlay (kSr, 512);
        const int numCh = juce::jmax (plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels());
        juce::AudioBuffer<float> buf (numCh, 512);
        buf.clear();
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 23, 127), 0);
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 25, 0), 1);
        plugin->processBlock (buf, midi);
        plugin->releaseResources();
        const auto midiRes = res != nullptr ? res->getText (res->getValue(), 32) : juce::String();
        std::printf ("  after CC23=127, CC25=0: residual suppression %s, Auto %s\n", midiRes.toRawUTF8(),
                     autoP != nullptr && autoP->getValue() > 0.5f ? "on" : "off");
        check (midiRes == "100 %" && autoP != nullptr && autoP->getValue() < 0.5f, "MIDI CC moves the mapped controls");
    }

    plugin.reset();
    std::printf (failures == 0 ? "ALL PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
