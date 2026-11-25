// Offline simulation of a speaker -> room -> mic echo path, used to verify AecCore
// without a DAW. Builds and runs on any platform (no JUCE dependency).
//
//  Phase A  0-10 s : far-end only (single talk)          -> measure ERLE
//  Phase B 10-16 s : far-end + near-end (double talk)     -> measure near-end preservation
//  Phase C 16-26 s : far-end only, echo path changed      -> measure re-convergence

#include "../src/dsp/AecCore.h"

#include <cmath>
#include <cstdio>
#include <vector>

#include "SimSignals.h"

using namespace ableaec;
using namespace sim;

namespace
{
double power (const std::vector<float>& v, int a, int b, int shift = 0)
{
    double s = 0;
    for (int i = a; i < b; ++i) s += (double) v[(size_t) (i + shift)] * v[(size_t) (i + shift)];
    return s / (b - a) + 1e-20;
}

double powerDiff (const std::vector<float>& x, int xShift, const std::vector<float>& y, int a, int b)
{
    double s = 0;
    for (int i = a; i < b; ++i)
    {
        const double d = (double) x[(size_t) (i + xShift)] - y[(size_t) i];
        s += d * d;
    }
    return s / (b - a) + 1e-20;
}

double db (double r) { return 10.0 * std::log10 (r); }
} // namespace

int main()
{
    const int total = (int) (26 * kSr);
    const int aEnd = (int) (10 * kSr), bEnd = (int) (16 * kSr);

    auto farEnd = speechLike (total, 1);
    auto nearEnd = speechLike (total, 2);
    for (int i = 0; i < total; ++i)
        if (i < aEnd || i >= bEnd) nearEnd[(size_t) i] = 0.0f;

    // loudspeaker with mild saturation, two different rooms (path change at bEnd)
    std::vector<float> spk ((size_t) total);
    for (int i = 0; i < total; ++i) spk[(size_t) i] = std::tanh (3.0f * farEnd[(size_t) i]) / 3.0f;
    const auto echo1 = convolve (spk, roomImpulse (22.0, 0.15, 7), 0, bEnd);
    const auto echo2 = convolve (spk, roomImpulse (35.0, 0.18, 9), bEnd, total);

    std::mt19937 rng (3);
    std::normal_distribution<float> noise (0.0f, 1.0f);
    std::vector<float> mic ((size_t) total), echo ((size_t) total);
    for (int i = 0; i < total; ++i)
    {
        echo[(size_t) i] = i < bEnd ? echo1[(size_t) i] : echo2[(size_t) i];
        mic[(size_t) i] = echo[(size_t) i] + nearEnd[(size_t) i] + 1.0e-4f * noise (rng);
    }

    int failures = 0;
    for (int modeIdx = 0; modeIdx < 2; ++modeIdx)
    {
        const auto mode = modeIdx == 0 ? OutputMode::LinearOnly : OutputMode::Clean;
        AecCore aec;
        aec.prepare (kSr, 500.0f, AecCore::blockSizeFor (kSr, false));
        AecSettings s;
        s.filterLengthMs = 250.0f;
        s.mode = mode;
        aec.setSettings (s);

        std::vector<float> out ((size_t) total, 0.0f);
        const int L = aec.getBlockSize(); // algorithmic latency
        for (int i = 0; i + L <= total; i += L)
            aec.processBlock (&farEnd[(size_t) i], &mic[(size_t) i], &out[(size_t) i]);

        // out[n] corresponds to input sample n - L
        auto erle = [&] (int a, int b) { return db (power (mic, a - L, b - L) / power (out, a, b)); };
        const double erleA = erle ((int) (6 * kSr), aEnd);
        const double erleC = erle ((int) (22 * kSr), total - 2 * L);
        const double erleC1 = erle (bEnd + (int) (1 * kSr), bEnd + (int) (3 * kSr));

        // double talk: how close is the output to the clean near-end, compared with the raw mic
        const int dtA = aEnd + (int) (0.5 * kSr), dtB = bEnd - L;
        const double nearPow = power (nearEnd, dtA, dtB, -L);
        const double micDist = db (nearPow / powerDiff (mic, -L, std::vector<float> (nearEnd.begin(), nearEnd.end()), dtA, dtB));
        std::vector<float> nearShift ((size_t) total, 0.0f);
        for (int i = L; i < total; ++i) nearShift[(size_t) i] = nearEnd[(size_t) (i - L)];
        const double outDist = db (nearPow / powerDiff (out, 0, nearShift, dtA, dtB));
        std::vector<float> micShift ((size_t) total, 0.0f);
        for (int i = L; i < total; ++i) micShift[(size_t) i] = mic[(size_t) (i - L)];
        const double micSer = db (nearPow / powerDiff (micShift, 0, nearShift, dtA, dtB));
        (void) micDist;

        std::printf ("[%s]\n", modeIdx == 0 ? "LinearOnly" : "Clean");
        std::printf ("  ERLE single-talk (6-10 s)          : %6.1f dB\n", erleA);
        std::printf ("  ERLE right after path change (+1-3s): %6.1f dB\n", erleC1);
        std::printf ("  ERLE after path change (22-26 s)   : %6.1f dB\n", erleC);
        std::printf ("  Double-talk near-end SER  mic / out : %6.1f dB / %6.1f dB\n", micSer, outDist);
        std::printf ("  estimated echo delay               : %6.1f ms (true 35.0 ms), leak %.3f, adapted %d\n",
                     aec.getEchoDelayMs(), aec.getLeakage(), aec.isConverged() ? 1 : 0);

        if (modeIdx == 0)
        {
            if (erleA < 20.0) { std::printf ("  FAIL: single-talk ERLE < 20 dB\n"); ++failures; }
            if (erleC < 15.0) { std::printf ("  FAIL: re-converged ERLE < 15 dB\n"); ++failures; }
            if (outDist - micSer < 10.0) { std::printf ("  FAIL: double-talk improvement < 10 dB\n"); ++failures; }
            if (std::abs (aec.getEchoDelayMs() - 35.0f) > 2.0f) { std::printf ("  FAIL: delay estimate\n"); ++failures; }
        }
        else
        {
            if (erleA < 30.0) { std::printf ("  FAIL: clean ERLE < 30 dB\n"); ++failures; }
            if (erleC < 30.0) { std::printf ("  FAIL: clean re-converged ERLE < 30 dB\n"); ++failures; }
            if (outDist - micSer < 8.0) { std::printf ("  FAIL: clean double-talk improvement < 8 dB\n"); ++failures; }
        }
    }

    // pass-through sanity: with no reference the output must equal the (delayed) mic
    {
        AecCore aec;
        aec.prepare (kSr, 200.0f, AecCore::blockSizeFor (kSr, true));
        const int Bs = aec.getBlockSize();
        std::vector<float> zeros ((size_t) Bs, 0.0f), in ((size_t) Bs), o ((size_t) Bs);
        std::vector<float> all, outAll;
        for (int blk = 0; blk < 50; ++blk)
        {
            for (int i = 0; i < Bs; ++i) in[(size_t) i] = nearEnd[(size_t) (aEnd + blk * Bs + i)];
            aec.processBlock (zeros.data(), in.data(), o.data());
            all.insert (all.end(), in.begin(), in.end());
            outAll.insert (outAll.end(), o.begin(), o.end());
        }
        double maxErr = 0;
        for (size_t i = (size_t) Bs; i < all.size(); ++i)
            maxErr = std::max (maxErr, (double) std::abs (outAll[i] - all[i - (size_t) Bs]));
        std::printf ("[Passthrough] max |out - mic(delayed)| = %.2e\n", maxErr);
        if (maxErr > 1e-4) { std::printf ("  FAIL: passthrough not transparent\n"); ++failures; }
    }

    std::printf (failures == 0 ? "ALL PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
