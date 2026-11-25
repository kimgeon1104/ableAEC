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

// mic & ref are mono; mic is duplicated to both main channels, ref to both sidechain channels
Result run (juce::AudioPluginInstance& plugin, const std::vector<float>& mic, const std::vector<float>& ref,
            int modeIndex, bool sidechainOn, int hostBlock)
{
    auto layout = plugin.getBusesLayout();
    layout.inputBuses.getReference (1) = sidechainOn ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
    plugin.setBusesLayout (layout);

    if (auto* mode = findParam (plugin, "Output"))
        mode->setValueNotifyingHost ((float) modeIndex / (float) juce::jmax (1, mode->getNumSteps() - 1));

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
        if (sidechainOn)
            for (int c = 2; c < juce::jmin (4, numCh); ++c) buf.copyFrom (c, 0, ref.data() + pos, n);
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

    // signals: 12 s far-end only through a room
    const int total = (int) (12 * kSr);
    const auto farEnd = speechLike (total, 11);
    const auto echo = convolve (farEnd, roomImpulse (18.0, 0.12, 5), 0, total);
    std::vector<float> mic ((size_t) total);
    for (int i = 0; i < total; ++i) mic[(size_t) i] = echo[(size_t) i] + 1.0e-4f * std::sin ((float) i * 0.01f);

    // 1. latency / alignment in Raw mic mode, odd host block size
    {
        const auto r = run (*plugin, mic, farEnd, 3, true, 441);
        double maxErr = 0;
        for (int i = r.latency; i < total; ++i)
            maxErr = std::max (maxErr, (double) std::abs (r.out[(size_t) i] - mic[(size_t) (i - r.latency)]));
        std::printf ("  reported latency %d samples, max alignment error %.2e\n", r.latency, maxErr);
        check (r.latency == 512, "reported latency is 512 samples");
        check (maxErr < 1e-6, "Raw mic output == input delayed by reported latency");
    }

    // 2. echo cancellation through the wrapper
    {
        const auto r = run (*plugin, mic, farEnd, 0, true, 480);
        const int a = (int) (8 * kSr), b = total - 1;
        double num = 0;
        for (int i = a; i < b; ++i) num += (double) mic[(size_t) (i - r.latency)] * mic[(size_t) (i - r.latency)];
        num = num / (b - a) + 1e-20;
        const double erle = 10.0 * std::log10 (num / pw (r.out, a, b));
        std::printf ("  ERLE through plugin (8-12 s): %.1f dB\n", erle);
        check (erle > 25.0, "echo reduced by more than 25 dB");
    }

    // 3. sidechain disabled -> transparent
    {
        const auto r = run (*plugin, mic, farEnd, 0, false, 512);
        double maxErr = 0;
        for (int i = r.latency; i < total; ++i)
            maxErr = std::max (maxErr, (double) std::abs (r.out[(size_t) i] - mic[(size_t) (i - r.latency)]));
        std::printf ("  no-sidechain max deviation %.2e\n", maxErr);
        check (maxErr < 1e-4, "without sidechain the plugin passes the mic through");
    }

    plugin.reset();
    std::printf (failures == 0 ? "ALL PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
