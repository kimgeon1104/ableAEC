#pragma once
// AecCore: mono acoustic echo canceller.
//
//  ref (far-end / speaker signal) ──┐
//                                   ▼
//  mic (near-end + echo) ──► [two-path PBFDAF linear filter] ──► e ──► [residual echo suppressor] ──► out
//
// Linear stage
//   Partitioned-block frequency-domain adaptive filter (overlap-save, per-bin normalised).
//   "Two-path" structure (Ochiai et al. / Speex): a *background* filter adapts continuously
//   with a fixed step; the *foreground* filter, which produces the output, only takes over the
//   background coefficients when they demonstrably cancel more echo. When the background filter
//   is disturbed by double-talk it is reset from the foreground copy. This gives fast tracking of
//   echo-path changes while staying robust during double-talk, without a separate double-talk
//   detector.
//
// Non-linear stage
//   STFT (sqrt-Hann, 50 % overlap) Wiener-style gain; the residual echo is estimated from the
//   echo estimate scaled by the measured "leakage" (after J.-M. Valin, 2007).
//
// Block size B is fixed. Algorithmic latency of processBlock() is B samples.
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
    float filterLengthMs = 200.0f; // echo tail covered by the adaptive filter
    float stepSize = 0.5f;         // background NLMS step (0..1)
    float resAmount = 0.5f;        // residual echo suppression strength (0..1)
    float resFloorDb = -30.0f;     // max attenuation of the suppressor
    bool freeze = false;           // stop adapting (keep current filter)
    OutputMode mode = OutputMode::Clean;
};

class AecCore
{
public:
    static constexpr int B = 256;      // block / hop size
    static constexpr int N = 2 * B;    // FFT size
    static constexpr int K = N / 2 + 1;

    void prepare (double sampleRate, float maxFilterLengthMs);
    void reset();
    void resetFilter() noexcept;
    void setSettings (const AecSettings& s) noexcept;

    // Processes exactly B samples. out may alias mic.
    void processBlock (const float* ref, const float* mic, float* out) noexcept;

    // --- metering
    float getErleDb() const noexcept { return erleDb; }
    float getEchoDelayMs() const noexcept { return echoDelayMs; }
    float getLeakage() const noexcept { return leak; }
    bool isConverged() const noexcept { return erleDb > 6.0f; }
    bool isReferenceActive() const noexcept { return refActive; }
    int getNumPartitions() const noexcept { return P; }

private:
    cpx* Xp (int p) noexcept { return X.data() + (size_t) ((xHead + p) % Pmax) * K; }
    cpx* Wf (int p) noexcept { return Wfg.data() + (size_t) p * K; }
    cpx* Wb (int p) noexcept { return Wbg.data() + (size_t) p * K; }
    void filter (std::vector<cpx>& W, float* yOut) noexcept;
    void constrainPartition (int p) noexcept;
    void updateDelayEstimate() noexcept;
    void residualSuppress (const float* e, const float* y, float* out, bool bypassGain) noexcept;

    double sr = 48000.0;
    int Pmax = 1, P = 1;
    AecSettings settings;

    Fft fft;
    std::vector<cpx> X, Wfg, Wbg;       // ref spectra ring, foreground / background filters
    int xHead = 0;
    std::vector<float> refPrev, frame, yF, yB, eF, eB;
    std::vector<cpx> acc, EF, EB, YF;
    std::vector<float> psum, psumSlow, stepK;
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
