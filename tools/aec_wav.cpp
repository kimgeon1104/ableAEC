// aec_wav: run the AbleAEC algorithm offline on WAV files.
//
//   aec_wav <ref.wav> <mic.wav> <out.wav> [options]
//
//   ref.wav  speaker / backing-track signal (what the sidechain would receive; channels are averaged)
//   mic.wav  microphone recording (first channel is used)
//   out.wav  processed mic, 32-bit float, time-aligned with mic.wav (algorithmic latency removed)
//
// options
//   --filter <ms>     filter length            (default 200)
//   --step <0..1>     adapt speed              (default 0.5)
//   --res <0..1>      residual suppression     (default 0.5)
//   --floor <dB>      suppression floor        (default -30)
//   --refdelay <ms>   delay applied to ref     (default 0)
//   --mode <clean|linear|echo|raw>             (default clean)
//   --latency <low|quality>  block size ~5 ms / ~10 ms   (default quality)
//   --monoref        sum a stereo ref to mono instead of using a stereo (2-channel) reference
//
// Both files must share the sample rate. Supports PCM 16/24/32-bit and 32-bit float WAV.

#include "AecCore.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace
{
struct Wav
{
    int sampleRate = 0;
    int channels = 0;
    std::vector<std::vector<float>> data; // [channel][sample]
};

uint32_t rd32 (const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24); }
uint16_t rd16 (const unsigned char* p) { return (uint16_t) (p[0] | (p[1] << 8)); }

bool readWav (const std::string& path, Wav& w)
{
    std::ifstream f (path, std::ios::binary);
    if (! f) { std::fprintf (stderr, "cannot open %s\n", path.c_str()); return false; }
    std::vector<unsigned char> b ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
    if (b.size() < 12 || std::memcmp (b.data(), "RIFF", 4) != 0 || std::memcmp (b.data() + 8, "WAVE", 4) != 0)
    {
        std::fprintf (stderr, "%s: not a RIFF/WAVE file\n", path.c_str());
        return false;
    }
    int format = 0, bits = 0;
    size_t pos = 12;
    const unsigned char* pcm = nullptr;
    size_t pcmBytes = 0;
    while (pos + 8 <= b.size())
    {
        const uint32_t len = rd32 (&b[pos + 4]);
        const unsigned char* c = &b[pos + 8];
        if (std::memcmp (&b[pos], "fmt ", 4) == 0)
        {
            format = rd16 (c);
            w.channels = rd16 (c + 2);
            w.sampleRate = (int) rd32 (c + 4);
            bits = rd16 (c + 14);
            if (format == 0xFFFE && len >= 26) format = rd16 (c + 24); // WAVE_FORMAT_EXTENSIBLE
        }
        else if (std::memcmp (&b[pos], "data", 4) == 0)
        {
            pcm = c;
            pcmBytes = std::min<size_t> (len, b.size() - (pos + 8));
        }
        pos += 8 + len + (len & 1);
    }
    if (pcm == nullptr || w.channels <= 0 || ! ((format == 1 && (bits == 16 || bits == 24 || bits == 32)) || (format == 3 && bits == 32)))
    {
        std::fprintf (stderr, "%s: unsupported WAV (format %d, %d bit)\n", path.c_str(), format, bits);
        return false;
    }
    const int bps = bits / 8;
    const size_t frames = pcmBytes / (size_t) (bps * w.channels);
    w.data.assign ((size_t) w.channels, std::vector<float> (frames));
    for (size_t i = 0; i < frames; ++i)
        for (int ch = 0; ch < w.channels; ++ch)
        {
            const unsigned char* p = pcm + (i * (size_t) w.channels + (size_t) ch) * (size_t) bps;
            float v;
            if (format == 3) { std::memcpy (&v, p, 4); }
            else if (bits == 16) v = (int16_t) rd16 (p) / 32768.0f;
            else if (bits == 24) v = (float) ((int32_t) ((p[0] << 8) | (p[1] << 16) | ((uint32_t) p[2] << 24)) >> 8) / 8388608.0f;
            else v = (float) ((int32_t) rd32 (p) / 2147483648.0);
            w.data[(size_t) ch][i] = v;
        }
    return true;
}

bool writeWav (const std::string& path, const std::vector<float>& x, int sampleRate)
{
    std::ofstream f (path, std::ios::binary);
    if (! f) return false;
    auto u32 = [&] (uint32_t v) { f.write (reinterpret_cast<const char*> (&v), 4); };
    auto u16 = [&] (uint16_t v) { f.write (reinterpret_cast<const char*> (&v), 2); };
    const uint32_t dataBytes = (uint32_t) (x.size() * 4);
    f.write ("RIFF", 4); u32 (36 + dataBytes); f.write ("WAVE", 4);
    f.write ("fmt ", 4); u32 (16); u16 (3); u16 (1); u32 ((uint32_t) sampleRate); u32 ((uint32_t) sampleRate * 4); u16 (4); u16 (32);
    f.write ("data", 4); u32 (dataBytes);
    f.write (reinterpret_cast<const char*> (x.data()), (std::streamsize) dataBytes);
    return (bool) f;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 4)
    {
        std::fprintf (stderr, "usage: %s <ref.wav> <mic.wav> <out.wav> [--filter ms] [--step x] [--res x] "
                              "[--floor dB] [--refdelay ms] [--mode clean|linear|echo|raw] [--latency low|quality] "
                              "[--monoref]\n", argv[0]);
        return 2;
    }
    ableaec::AecSettings s;
    float refDelayMs = 0.0f;
    bool lowLatency = false, monoRef = false;
    for (int i = 4; i < argc; i += 2)
    {
        const std::string k = argv[i];
        const char* v = i + 1 < argc ? argv[i + 1] : "";
        if (k == "--filter") s.filterLengthMs = (float) std::atof (v);
        else if (k == "--step") s.stepSize = (float) std::atof (v);
        else if (k == "--res") s.resAmount = (float) std::atof (v);
        else if (k == "--floor") s.resFloorDb = (float) std::atof (v);
        else if (k == "--refdelay") refDelayMs = (float) std::atof (v);
        else if (k == "--latency") lowLatency = std::string (v) == "low";
        else if (k == "--monoref") { monoRef = true; --i; }
        else if (k == "--mode")
        {
            const std::string m = v;
            s.mode = m == "linear" ? ableaec::OutputMode::LinearOnly
                   : m == "echo"   ? ableaec::OutputMode::EchoEstimate
                   : m == "raw"    ? ableaec::OutputMode::RawMic
                                   : ableaec::OutputMode::Clean;
        }
        else { std::fprintf (stderr, "unknown option %s\n", k.c_str()); return 2; }
    }

    Wav ref, mic;
    if (! readWav (argv[1], ref) || ! readWav (argv[2], mic)) return 1;
    if (ref.sampleRate != mic.sampleRate)
    {
        std::fprintf (stderr, "sample rates differ (%d vs %d)\n", ref.sampleRate, mic.sampleRate);
        return 1;
    }

    const int numRefs = (ref.channels >= 2 && ! monoRef) ? 2 : 1;
    ableaec::AecCore aec;
    aec.prepare (mic.sampleRate, std::max (500.0f, s.filterLengthMs),
                 ableaec::AecCore::blockSizeFor (mic.sampleRate, lowLatency), numRefs);
    aec.setSettings (s);
    const int B = aec.getBlockSize();

    const size_t n = mic.data[0].size();
    const size_t padded = ((n + (size_t) B) / (size_t) B + 1) * (size_t) B;
    const int delay = (int) std::lround (refDelayMs * 0.001 * ref.sampleRate);

    std::vector<std::vector<float>> r ((size_t) numRefs, std::vector<float> (padded, 0.0f));
    std::vector<float> m (padded, 0.0f), out (padded, 0.0f);
    for (size_t i = 0; i < n; ++i)
    {
        m[i] = mic.data[0][i];
        const long j = (long) i - delay;
        if (j < 0 || (size_t) j >= ref.data[0].size()) continue;
        if (numRefs == 2)
            for (int ch = 0; ch < 2; ++ch) r[(size_t) ch][i] = ref.data[(size_t) ch][(size_t) j];
        else
        {
            float acc = 0.0f;
            for (int ch = 0; ch < ref.channels; ++ch) acc += ref.data[(size_t) ch][(size_t) j];
            r[0][i] = acc / (float) ref.channels;
        }
    }

    for (size_t i = 0; i + (size_t) B <= padded; i += (size_t) B)
    {
        const float* refs[2] = { &r[0][i], &r[(size_t) numRefs - 1][i] };
        aec.processBlock (refs, &m[i], &out[i]);
    }

    std::vector<float> aligned (out.begin() + B, out.begin() + B + (long) n); // remove algorithmic latency
    if (! writeWav (argv[3], aligned, mic.sampleRate)) { std::fprintf (stderr, "cannot write %s\n", argv[3]); return 1; }

    std::printf ("processed %.2f s @ %d Hz | block %d | %s reference | final ERLE %.1f dB | echo delay %.1f ms\n",
                 (double) n / mic.sampleRate, mic.sampleRate, B, numRefs == 2 ? "stereo" : "mono",
                 aec.getErleDb(), aec.getEchoDelayMs() + refDelayMs);
    return 0;
}
