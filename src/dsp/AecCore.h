#pragma once
// AecCore: acoustic echo canceller for one microphone channel with a mono or stereo reference.
//
//  ref L/R (speaker / backing track) ──┐
//                                      ▼
//  mic (vocal + echo) ──► [two-path PB-FDAF, Kalman-adapted] ──► e ──► [residual echo suppressor] ──► out
//
// Linear stage
//   Partitioned-block frequency-domain adaptive filter (overlap-save). One filter per reference
//   channel, so stereo playback through two loudspeakers (different paths to the mic) is modelled
//   correctly. The background filter is adapted with a diagonalised frequency-domain Kalman filter
//   (Kuech, Mabande & Enzner 2014): each (partition, bin) has its own uncertainty, which makes it
//   converge quickly on music and slow down by itself while the performer sings. A foreground copy
//   ("two-path", Ochiai et al.) produces the output and only takes over the background
//   coefficients when they demonstrably cancel more echo.
//
// Non-linear stage
//   STFT (sqrt-Hann, 50 % overlap) Wiener-style gain; residual echo is estimated from the echo
//   estimate scaled by the measured leakage (after J.-M. Valin, 2007).
//
// The block size B is chosen at prepare(). Algorithmic latency of processBlock() is B samples.
// No allocation happens after prepare().

#include "Fft.h"
#include <vector>

namespace ableaec
{
enum class OutputMode : int
{
    Clean = 0,      // linear AEC + residual echo suppression
    LinearOnly,     // linear AEC only
    EchoEstimate,   // the estimated echo (for monitoring / debugging)
    RawMic          // untouched mic (latency-aligned)
};

struct AecSettings
{
    float filterLengthMs = 250.0f; // echo tail covered by the adaptive filter
    float stepSize = 0.5f;         // adaptation speed (0..1), 0.5 = nominal Kalman gain
    float resAmount = 0.2f;        // residual echo suppression strength (0..1)
    float resFloorDb = -18.0f;     // max attenuation of the suppressor
    bool freeze = false;           // stop adapting (keep current filter)
    OutputMode mode = OutputMode::Clean;
};

class AecCore
{
public:
    static constexpr int kMaxRefs = 2;

    // Recommended block size: ~10.7 ms ("low latency") or ~21 ms ("quality") at any sample rate.
    static int blockSizeFor (double sampleRate, bool lowLatency) noexcept;

    // blockSize must be a power of two (>= 64). numRefs is 1 (mono) or 2 (stereo reference).
    void prepare (double sampleRate, float maxFilterLengthMs, int blockSize, int numRefs = 1);
    void reset();
    void resetFilter() noexcept;
    void setSettings (const AecSettings& s) noexcept;

    int getBlockSize() const noexcept { return B; }
    int getNumRefs() const noexcept { return Ca; }

    // Use only the first n (<= prepared) reference channels, e.g. to switch stereo -> mono at run time.
    // Resets the filter when the count changes. Real-time safe.
    void setActiveRefs (int n) noexcept;

    // Processes exactly getBlockSize() samples. refs holds getNumRefs() pointers. out may alias mic.
    void processBlock (const float* const* refs, const float* mic, float* out) noexcept;
    void processBlock (const float* ref, const float* mic, float* out) noexcept // mono-reference convenience
    {
        const float* r[kMaxRefs] = { ref, ref };
        processBlock (r, mic, out);
    }

    // --- metering
    float getErleDb() const noexcept { return erleDb; }
    float getEchoDelayMs() const noexcept { return echoDelayMs; }
    float getLeakage() const noexcept { return leak; }
    bool isConverged() const noexcept { return erleDb > 6.0f; }
    bool isReferenceActive() const noexcept { return refActive; }
    int getNumPartitions() const noexcept { return P; }

private:
    size_t idx (int c, int p) const noexcept { return ((size_t) c * (size_t) Pmax + (size_t) p) * (size_t) K; }
    cpx* Xp (int c, int p) noexcept { return X.data() + idx (c, (xHead + p) % Pmax); }
    cpx* Wf (int c, int p) noexcept { return Wfg.data() + idx (c, p); }
    cpx* Wb (int c, int p) noexcept { return Wbg.data() + idx (c, p); }
    float* Pc (int c, int p) noexcept { return Pcov.data() + idx (c, p); }

    void filter (std::vector<cpx>& W, float* yOut) noexcept;
    void constrainPartition (int c, int p) noexcept;
    void updateDelayEstimate() noexcept;
    void residualSuppress (const float* e, const float* y, float* out, bool bypassGain) noexcept;

    double sr = 48000.0;
    int B = 256, N = 512, K = 257, C = 1, Ca = 1; // C = allocated, Ca = active reference channels
    int Pmax = 1, P = 1;
    AecSettings settings;

    Fft fft;
    std::vector<cpx> X, Wfg, Wbg;       // ref spectra rings, foreground / background filters
    std::vector<float> Pcov;            // Kalman state uncertainty per (channel, partition, bin)
    int xHead = 0;
    std::vector<float> refPrev, frame, yF, yB, eF, eB, micCopy, scratch;
    std::vector<cpx> acc, EF, EB, YF;
    std::vector<float> kalDen, psiS;
    std::vector<float> meanE, meanY;    // for leakage estimation
    double pey = 0.0, pyy = 1.0;
    float leak = 1.0f;

    // two-path control
    double sFF = 0.0, sBB = 0.0, sDD = 0.0;
    int betterCount = 0, worseCount = 0;

    // state / meters
    bool refActive = false;
    int constrainIdx = 0, blockCounter = 0;
    double micPowS = 1e-9, errPowS = 1e-9;
    float erleDb = 0.0f, echoDelayMs = 0.0f;

    // residual suppressor
    std::vector<float> window, resE, resY, ola, prevMic, prevY;
    std::vector<cpx> RE, RY;
    std::vector<float> gain;
};
} // namespace ableaec
