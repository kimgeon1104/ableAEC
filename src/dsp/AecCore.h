#pragma once
// AecCore: acoustic echo canceller for one microphone channel with a mono or stereo reference.
//
//  ref L/R (speaker / backing track) ──┐
//                                      ▼
//  mic (vocal + echo) ──► [two-path GMDF, Kalman-adapted] ──► e ──► [residual echo suppressor] ──► out
//
// Linear stage
//   Generalised multidelay filter (GMDF, Moulines et al. 1995): overlap-save partitions of
//   L taps (FFT size 2L) updated every hop of B samples, B <= L. The partition length sets the
//   frequency resolution (crucial for tonal music and sub-bass), the hop sets the latency, so the
//   two can be chosen independently. One filter per reference channel, so stereo playback through
//   two loudspeakers is modelled correctly.
//   The background filter is adapted with a diagonalised frequency-domain Kalman filter
//   (Kuech, Mabande & Enzner 2014): every (channel, partition, bin) keeps its own uncertainty,
//   which makes it converge quickly on music and slow down by itself while the performer sings.
//   A foreground copy ("two-path", Ochiai et al.) produces the output and only takes over the
//   background coefficients when they demonstrably cancel more echo.
//
// Non-linear stage
//   STFT (sqrt-Hann, 2B frame, 50 % overlap) Wiener-style gain; residual echo is estimated from
//   the echo estimate scaled by the measured leakage (after J.-M. Valin, 2007).
//
// Algorithmic latency of processBlock() is B samples. No allocation happens after prepare().

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
    bool autoTune = false;         // let the core choose filter length and residual suppression
    OutputMode mode = OutputMode::Clean;
};

class AecCore
{
public:
    static constexpr int kMaxRefs = 2;

    // Hop (= latency) and partition (= frequency resolution) sizes for the two plugin modes,
    // scaled with the sample rate so durations stay the same:
    //   quality      hop ~10.7 ms, partition ~42.7 ms (512 / 2048 at 48 kHz)
    //   low latency  hop  ~5.3 ms, partition ~42.7 ms (256 / 2048 at 48 kHz)
    static int blockSizeFor (double sampleRate, bool lowLatency) noexcept;
    static int partitionSizeFor (double sampleRate, bool lowLatency) noexcept;

    // blockSize (hop) and partitionSize must be powers of two with blockSize <= partitionSize.
    // partitionSize <= 0 means "same as blockSize" (classic MDF). numRefs is 1 or 2.
    void prepare (double sampleRate, float maxFilterLengthMs, int blockSize, int numRefs = 1, int partitionSize = 0);
    void reset();
    void resetFilter() noexcept;
    void setSettings (const AecSettings& s) noexcept;

    int getBlockSize() const noexcept { return B; }
    int getPartitionSize() const noexcept { return L; }
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

    // Values in effect (equal to the settings unless autoTune chose them).
    float getEffectiveFilterLengthMs() const noexcept { return (float) (P * (double) L * 1000.0 / sr); }
    float getEffectiveResAmount() const noexcept { return settings.autoTune ? autoRes : settings.resAmount; }
    float getEffectiveResFloorDb() const noexcept { return settings.autoTune ? autoFloorDb : settings.resFloorDb; }
    bool isEchoPresent() const noexcept { return echoPresent; }

private:
    size_t widx (int c, int p) const noexcept { return ((size_t) c * (size_t) Pmax + (size_t) p) * (size_t) K; }
    // reference spectrum of channel c for partition p (= p * O hops ago)
    cpx* Xp (int c, int p) noexcept { return X.data() + ((size_t) c * (size_t) R + (size_t) ((xHead + p * O) % R)) * (size_t) K; }
    cpx* Wf (int c, int p) noexcept { return Wfg.data() + widx (c, p); }
    cpx* Wb (int c, int p) noexcept { return Wbg.data() + widx (c, p); }
    float* Pc (int c, int p) noexcept { return Pcov.data() + widx (c, p); }

    void filter (std::vector<cpx>& W, float* yOut) noexcept;
    void constrainPartition (int c, int p) noexcept;
    void updateDelayEstimate() noexcept;
    void residualSuppress (const float* e, const float* y, float* out, bool bypassGain) noexcept;
    void applyFilterLength (float ms) noexcept;
    void autoTuneStep (float micEnergy) noexcept;
    int tailPartition() noexcept;

    double sr = 48000.0;
    int B = 256;          // hop (block) size
    int L = 256;          // partition length
    int N = 512, K = 257; // filter FFT size (2L) and bins
    int O = 1;            // hops per partition (L / B)
    int R = 1;            // reference-spectrum ring length in hops
    int C = 1, Ca = 1;    // allocated / active reference channels
    int Pmax = 1, P = 1;
    AecSettings settings;

    Fft fft;                            // size N (filter)
    std::vector<cpx> X, Wfg, Wbg;       // ref spectra rings, foreground / background filters
    std::vector<float> Pcov;            // Kalman state uncertainty per (channel, partition, bin)
    int xHead = 0;
    std::vector<float> refHist;         // last N reference samples per channel
    std::vector<float> frame, yF, yB, eF, eB, micCopy, scratch;
    std::vector<cpx> acc, EF, EB, YF;
    std::vector<float> kalDen, psiS;
    std::vector<float> meanE, meanY;    // for leakage estimation
    double pey = 0.0, pyy = 1.0;
    float leak = 1.0f;

    // two-path control
    double sFF = 0.0, sDD = 0.0;                         // divergence guard
    double davg1 = 0.0, davg2 = 0.0, dvar1 = 0.0, dvar2 = 0.0; // foreground/background statistics
    double accSf = 0.0, accSb = 0.0, accDbf = 0.0;              // energies accumulated per decision
    int accCount = 0;

    // state / meters
    bool refActive = false;
    int constrainIdx = 0, blockCounter = 0;
    double micPowS = 1e-9, errPowS = 1e-9;
    float erleDb = 0.0f, echoDelayMs = 0.0f;

    // auto tuning
    float autoFilterMs = 250.0f, autoRes = 0.2f, autoFloorDb = -18.0f;
    double erlePeak = 0.0, echoRatioS = 0.0, activeSeconds = 0.0;
    bool echoPresent = false;
    int autoCounter = 0, growVotes = 0, shrinkVotes = 0;

    // residual suppressor (own STFT of size 2B)
    Fft fftRes;
    int Nr = 512, Kr = 257;
    std::vector<float> window, resE, resY, ola, resFrame, prevMic, prevY;
    std::vector<cpx> RE, RY;
    std::vector<float> gain;
};
} // namespace ableaec
