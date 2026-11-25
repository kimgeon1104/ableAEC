#include "AecCore.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ableaec
{
namespace
{
constexpr float kRefActiveThreshold = 1.0e-8f; // mean square, ~ -80 dBFS
constexpr float kMinLeak = 0.005f;
constexpr float kEps = 1.0e-10f;

// Kalman filter tuning. Per-hop constants are specified for a 512-sample hop at 48 kHz (~10.7 ms)
// and rescaled to the actual hop duration in processBlock().
constexpr double kKalmanA = 0.999;          // state transition per reference hop (echo-path drift)
constexpr float kKalmanQ = 1.0e-8f;         // process-noise floor (keeps the filter able to track)
constexpr float kKalmanP0 = 1.0f;           // initial state uncertainty
constexpr float kPathChangeLeak = 0.5f;     // leakage above which the echo path is assumed to have changed
constexpr double kPathChangeInflate = 1.5;  // growth of the state uncertainty per reference hop after a path change
constexpr float kPathChangeMinEcho = 0.05f; // ... only while the filter predicts a real echo (>= -13 dB of the mic)
constexpr double kPromoteSignificance = 4.0; // background must beat the foreground by ~2 sigma ...
constexpr double kPromoteMinGain = 0.9;      // ... and by at least ~0.5 dB

template <typename T>
void zero (std::vector<T>& v) { std::fill (v.begin(), v.end(), T {}); }

float energy (const float* x, int n) noexcept
{
    float s = 0.0f;
    for (int i = 0; i < n; ++i) s += x[i] * x[i];
    return s;
}

int pow2Near (double target)
{
    int b = 64;
    while (b * 2 <= (int) (target * 1.25)) b *= 2;
    return b;
}
} // namespace

int AecCore::blockSizeFor (double sampleRate, bool lowLatency) noexcept
{
    return pow2Near ((lowLatency ? 256.0 : 512.0) * sampleRate / 48000.0);
}

int AecCore::partitionSizeFor (double sampleRate, bool lowLatency) noexcept
{
    (void) lowLatency; // both modes use ~43 ms partitions (frequency resolution ~23 Hz)
    return pow2Near (2048.0 * sampleRate / 48000.0);
}

void AecCore::prepare (double sampleRate, float maxFilterLengthMs, int blockSize, int numRefs, int partitionSize)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    B = std::max (32, blockSize);
    L = std::max (B, partitionSize > 0 ? partitionSize : B);
    O = L / B;
    N = 2 * L;
    K = L + 1;
    C = std::clamp (numRefs, 1, kMaxRefs);
    Ca = C;
    Pmax = std::max (1, (int) std::ceil (maxFilterLengthMs * 0.001 * sr / L));
    R = (Pmax - 1) * O + 1;

    fft.prepare (N);
    X.assign ((size_t) C * (size_t) R * (size_t) K, cpx {});
    const size_t wsize = (size_t) C * (size_t) Pmax * (size_t) K;
    Wfg.assign (wsize, cpx {});
    Wbg.assign (wsize, cpx {});
    Pcov.assign (wsize, kKalmanP0);
    refHist.assign ((size_t) C * (size_t) N, 0.0f);
    frame.assign ((size_t) N, 0.0f);
    for (auto* v : { &yF, &yB, &eF, &eB, &micCopy, &scratch, &prevMic, &prevY }) v->assign ((size_t) B, 0.0f);
    for (auto* v : { &acc, &EF, &EB, &YF }) v->assign ((size_t) K, cpx {});
    for (auto* v : { &kalDen, &psiS, &meanE, &meanY }) v->assign ((size_t) K, 0.0f);

    Nr = 2 * B;
    Kr = B + 1;
    fftRes.prepare (Nr);
    window.resize ((size_t) Nr);
    for (int i = 0; i < Nr; ++i) // periodic sqrt-Hann -> perfect reconstruction at 50 % overlap
        window[(size_t) i] = (float) std::sqrt (0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * i / Nr));
    for (auto* v : { &resE, &resY, &ola, &resFrame }) v->assign ((size_t) Nr, 0.0f);
    RE.assign ((size_t) Kr, cpx {});
    RY.assign ((size_t) Kr, cpx {});
    gain.assign ((size_t) Kr, 1.0f);

    P = 1;
    setSettings (settings);
    reset();
}

void AecCore::reset()
{
    zero (X);
    zero (refHist);
    zero (resE);
    zero (resY);
    zero (ola);
    zero (prevMic);
    zero (prevY);
    std::fill (gain.begin(), gain.end(), 1.0f);
    xHead = 0;
    resetFilter();
}

void AecCore::resetFilter() noexcept
{
    zero (Wfg);
    zero (Wbg);
    std::fill (Pcov.begin(), Pcov.end(), kKalmanP0);
    zero (psiS);
    zero (meanE);
    zero (meanY);
    pey = 0.0;
    pyy = 1.0;
    leak = 1.0f;
    sFF = sDD = 0.0;
    davg1 = davg2 = dvar1 = dvar2 = 0.0;
    accSf = accSb = accDbf = 0.0;
    accCount = 0;
    micPowS = errPowS = 1e-9;
    erleDb = 0.0f;
    echoDelayMs = 0.0f;
    erlePeak = 0.0;
    echoRatioS = 0.0;
    activeSeconds = 0.0;
    echoPresent = false;
    growVotes = shrinkVotes = 0;
}

void AecCore::setActiveRefs (int n) noexcept
{
    n = std::clamp (n, 1, C);
    if (n == Ca) return;
    Ca = n;
    zero (X);
    zero (refHist);
    resetFilter();
}

void AecCore::setSettings (const AecSettings& s) noexcept
{
    if (s.autoTune && ! settings.autoTune) // entering auto: start from the user's values
    {
        autoFilterMs = s.filterLengthMs;
        autoRes = s.resAmount;
        autoFloorDb = s.resFloorDb;
        growVotes = shrinkVotes = 0;
    }
    settings = s;
    applyFilterLength (s.autoTune ? autoFilterMs : s.filterLengthMs);
}

void AecCore::applyFilterLength (float ms) noexcept
{
    const int newP = std::clamp ((int) std::ceil (ms * 0.001 * sr / L), 1, Pmax);
    if (newP < P) // discard taps that are no longer used
        for (int c = 0; c < C; ++c)
            for (int p = newP; p < P; ++p)
            {
                std::fill (Wf (c, p), Wf (c, p) + K, cpx {});
                std::fill (Wb (c, p), Wb (c, p) + K, cpx {});
                std::fill (Pc (c, p), Pc (c, p) + K, kKalmanP0);
            }
    P = newP;
    constrainIdx %= P;
}

void AecCore::filter (std::vector<cpx>& W, float* yOut) noexcept
{
    std::fill (acc.begin(), acc.end(), cpx {});
    for (int c = 0; c < Ca; ++c)
        for (int p = 0; p < P; ++p)
        {
            const cpx* x = Xp (c, p);
            const cpx* w = W.data() + widx (c, p);
            for (int k = 0; k < K; ++k) acc[(size_t) k] += cmul (w[k], x[k]);
        }
    fft.inverse (acc.data(), frame.data());
    std::copy (frame.end() - B, frame.end(), yOut); // newest B of the L valid output samples
}

void AecCore::processBlock (const float* const* refs, const float* mic, float* out) noexcept
{
    float* curMic = micCopy.data(); // `out` may alias `mic`
    std::copy (mic, mic + B, curMic);

    // time constants are tuned for a 512-sample hop at 48 kHz
    const double hopScale = (double) B * 48000.0 / (512.0 * sr);

    // ---------------------------------------------------------------- 1. reference spectra
    xHead = (xHead - 1 + R) % R; // newest spectrum lives at hop 0
    float sx = 0.0f;
    for (int c = 0; c < Ca; ++c)
    {
        float* hist = refHist.data() + (size_t) c * (size_t) N;
        std::copy (hist + B, hist + N, hist); // slide by one hop
        std::copy (refs[c], refs[c] + B, hist + N - B);
        fft.forward (hist, Xp (c, 0));
        sx += energy (refs[c], B);
    }
    refActive = sx / (float) (B * Ca) > kRefActiveThreshold;

    // ---------------------------------------------------------------- 2. both filters
    filter (Wfg, yF.data());
    filter (Wbg, yB.data());
    for (int i = 0; i < B; ++i)
    {
        eF[(size_t) i] = curMic[i] - yF[(size_t) i];
        eB[(size_t) i] = curMic[i] - yB[(size_t) i];
    }
    const float sd = energy (curMic, B);
    float sf = energy (eF.data(), B);
    float sb = energy (eB.data(), B);
    const auto wCount = (ptrdiff_t) Wfg.size();

    // ---------------------------------------------------------------- 3. two-path control
    // Statistical foreground/background decision (as in Speex mdf.c): an improvement of the background
    // filter only counts when it is large compared with what near-end sound could cause by chance,
    // which scales with the energy of the difference between the two echo estimates (Dbf).
    if (refActive && ! settings.freeze)
    {
        double dbf = 0.0;
        for (int i = 0; i < B; ++i)
        {
            const double d = (double) yF[(size_t) i] - yB[(size_t) i];
            dbf += d * d;
        }
        // Accumulate over ~10.7 ms regardless of the hop size so that short hops do not get more
        // chances to pass the test by luck.
        accSf += sf;
        accSb += sb;
        accDbf += dbf;
        const int hopsPerDecision = std::max (1, (int) std::lround (1.0 / hopScale));
        if (++accCount >= hopsPerDecision)
        {
            const double Sf = accSf, Sb = accSb, Dbf = accDbf + 1.0e-9 * B * accCount;
            accSf = accSb = accDbf = 0.0;
            accCount = 0;

            const double diff = Sf - Sb; // > 0: background better
            davg1 = 0.6 * davg1 + 0.4 * diff;
            davg2 = 0.85 * davg2 + 0.15 * diff;
            dvar1 = 0.36 * dvar1 + 0.16 * Sf * Dbf;
            dvar2 = 0.7225 * dvar2 + 0.0225 * Sf * Dbf;

            // Strict: with no (or very weak) echo the background can only "improve" by chasing the
            // performer's voice by chance; promoting that would carve pieces out of the vocal.
            const double sig = kPromoteSignificance;
            const bool promote = Sb < kPromoteMinGain * Sf
                                 && (diff * std::abs (diff) > sig * Sf * Dbf
                                     || davg1 * std::abs (davg1) > sig * 0.5 * dvar1
                                     || davg2 * std::abs (davg2) > sig * 0.25 * dvar2);
            if (promote)
            {
                std::copy (Wbg.begin(), Wbg.begin() + wCount, Wfg.begin());
                std::copy (yB.begin(), yB.end(), yF.begin());
                std::copy (eB.begin(), eB.end(), eF.begin());
                sf = sb;
                davg1 = davg2 = dvar1 = dvar2 = 0.0;
            }
            else
            {
                constexpr double backtrack = 64.0; // lenient: Kalman steps can briefly raise the error while learning fast
                const bool resetBg = -diff * std::abs (diff) > backtrack * Sf * Dbf
                                     || -davg1 * std::abs (davg1) > backtrack * dvar1
                                     || -davg2 * std::abs (davg2) > backtrack * dvar2;
                if (resetBg) // background disturbed (typically by double-talk): restart from foreground
                {
                    std::copy (Wfg.begin(), Wfg.begin() + wCount, Wbg.begin());
                    std::copy (eF.begin(), eF.end(), eB.begin());
                    davg1 = davg2 = dvar1 = dvar2 = 0.0;
                }
            }
        }

        // foreground divergence guard (should never trigger in normal operation)
        const double a = 1.0 - std::pow (0.75, hopScale);
        sFF = (1.0 - a) * sFF + a * sf;
        sDD = (1.0 - a) * sDD + a * sd;
        if (sFF > 8.0 * sDD + 1.0e-6 * B)
        {
            resetFilter();
            std::fill (yF.begin(), yF.end(), 0.0f);
            std::copy (curMic, curMic + B, eF.begin());
            std::copy (curMic, curMic + B, eB.begin());
            sf = sd;
        }
    }

    // ---------------------------------------------------------------- 4. spectra (error in the newest B of N)
    std::fill (frame.begin(), frame.end() - B, 0.0f);
    std::copy (eB.begin(), eB.end(), frame.end() - B);
    fft.forward (frame.data(), EB.data());
    std::copy (eF.begin(), eF.end(), frame.end() - B);
    fft.forward (frame.data(), EF.data());
    std::copy (yF.begin(), yF.end(), frame.end() - B);
    fft.forward (frame.data(), YF.data());

    // ---------------------------------------------------------------- 5. leakage + meters (foreground)
    if (refActive)
    {
        double peyInst = 0.0, pyyInst = 0.0;
        const float am = (float) (1.0 - std::pow (0.95, hopScale));
        for (int k = 0; k < K; ++k)
        {
            const float e2 = norm2 (EF[(size_t) k]);
            const float y2 = norm2 (YF[(size_t) k]);
            meanE[(size_t) k] += am * (e2 - meanE[(size_t) k]);
            meanY[(size_t) k] += am * (y2 - meanY[(size_t) k]);
            const double de = e2 - meanE[(size_t) k];
            const double dy = y2 - meanY[(size_t) k];
            peyInst += de * dy;
            pyyInst += dy * dy;
        }
        const double a = 1.0 - std::pow (0.95, hopScale);
        pey = (1.0 - a) * pey + a * peyInst;
        pyy = (1.0 - a) * pyy + a * pyyInst;
        leak = (float) std::clamp (pyy > 1e-20 ? pey / pyy : 1.0, (double) kMinLeak, 1.0);

        const double am2 = 1.0 - std::pow (0.97, hopScale);
        micPowS = (1.0 - am2) * micPowS + am2 * sd;
        errPowS = (1.0 - am2) * errPowS + am2 * sf;
        erleDb = (float) (10.0 * std::log10 ((micPowS + 1e-12) / (errPowS + 1e-12)));
    }

    // ---------------------------------------------------------------- 6. background adaptation
    // Diagonalised frequency-domain Kalman filter (Kuech, Mabande & Enzner 2014) for the GMDF.
    // The observation is the newest B of N samples, hence the B/N and N/B factors.
    // Every (channel, partition, bin) keeps its own state uncertainty Pc: bins that have never been
    // excited (e.g. a new chord in the backing track) learn at full speed as soon as they appear,
    // while near-end sound (raising the observation noise psiS) automatically slows adaptation.
    if (refActive && ! settings.freeze)
    {
        const float a2 = (float) std::pow (kKalmanA, 2.0 * hopScale);
        const float q = kKalmanQ * (float) hopScale;
        const float obsFrac = (float) B / (float) N;   // B/N
        const float psiScale = (float) N / (float) B;  // N/B

        // Echo-path change (mic / speaker moved): the error is still strongly correlated with the echo
        // estimate, unlike near-end singing. Re-open the state uncertainty so the filter re-learns fast.
        // The leakage estimate is meaningless while the echo estimate is ~0 (no echo reaches the mic),
        // so this only applies when the foreground predicts a real echo.
        if (leak > kPathChangeLeak && energy (yF.data(), B) > kPathChangeMinEcho * sd)
        {
            const float inflate = (float) std::pow (kPathChangeInflate, hopScale);
            for (auto& v : Pcov) v = std::min (kKalmanP0, v * inflate + 1.0e-6f);
        }

        const float gainScale = 2.0f * settings.stepSize; // default 0.5 -> 1
        const float ap = (float) std::pow (0.5, hopScale);
        for (int k = 0; k < K; ++k)
        {
            psiS[(size_t) k] = ap * psiS[(size_t) k] + (1.0f - ap) * norm2 (EB[(size_t) k]);
            kalDen[(size_t) k] = 0.0f;
        }
        for (int c = 0; c < Ca; ++c)
            for (int p = 0; p < P; ++p)
            {
                const cpx* x = Xp (c, p);
                const float* pc = Pc (c, p);
                for (int k = 0; k < K; ++k) kalDen[(size_t) k] += pc[k] * norm2 (x[k]);
            }
        const float delta = (float) N * 1.0e-9f;
        for (int k = 0; k < K; ++k) // kalDen := 1 / (sum Pc|X|^2 + psi)
            kalDen[(size_t) k] = 1.0f / (kalDen[(size_t) k] + psiScale * psiS[(size_t) k] + delta);

        for (int c = 0; c < Ca; ++c)
            for (int p = 0; p < P; ++p)
            {
                const cpx* x = Xp (c, p);
                cpx* w = Wb (c, p);
                float* pc = Pc (c, p);
                for (int k = 0; k < K; ++k)
                {
                    const float x2 = norm2 (x[k]);
                    const float g = pc[k] * kalDen[(size_t) k];              // Kalman gain / conj(X)
                    w[k] += cmulConj (x[k], EB[(size_t) k]) * (gainScale * g);
                    const float shrink = std::max (0.0f, 1.0f - obsFrac * g * x2);
                    pc[k] = a2 * shrink * pc[k] + (1.0f - a2) * norm2 (w[k]) + q;
                }
            }

        // gradient constraint (time-domain causality), round-robin, every channel
        for (int n = 0; n < 2; ++n)
        {
            for (int c = 0; c < Ca; ++c) constrainPartition (c, constrainIdx);
            constrainIdx = (constrainIdx + 1) % P;
        }
    }

    if (++blockCounter >= std::max (1, 8192 / B))
    {
        blockCounter = 0;
        updateDelayEstimate();
    }

    if (settings.autoTune)
        autoTuneStep (sd);

    // ---------------------------------------------------------------- 7. output
    switch (settings.mode)
    {
        case OutputMode::Clean:
            residualSuppress (eF.data(), yF.data(), out, getEffectiveResAmount() <= 0.0f);
            break;
        case OutputMode::LinearOnly:
            residualSuppress (eF.data(), yF.data(), out, true);
            break;
        case OutputMode::EchoEstimate:
        case OutputMode::RawMic:
        {
            // keep the suppressor state running so that switching modes is seamless
            residualSuppress (eF.data(), yF.data(), scratch.data(), true);
            const auto& src = settings.mode == OutputMode::RawMic ? prevMic : prevY;
            std::copy (src.begin(), src.end(), out);
            break;
        }
    }
    std::copy (yF.begin(), yF.end(), prevY.begin());
    std::copy (curMic, curMic + B, prevMic.begin());
}

// Where the learned echo tail meets the filter's estimation-noise floor, as a partition index.
// After the strongest partition the energy decays with the room's reverb until it reaches a floor
// set by estimation noise (singing, background noise), typically -20..-30 dB. The floor is the
// 25th percentile of the partitions after the peak; the tail ends at the first partition within
// 3 dB of it. Returns P - 1 when the decay has not reached a clear floor (the filter is too short).
// By Parseval, a partition's frequency-domain energy is its impulse response energy.
int AecCore::tailPartition() noexcept
{
    double perPart[512], sorted[512];
    const int np = std::min (P, 512);
    int peakIdx = 0;
    for (int p = 0; p < np; ++p)
    {
        double e = 0.0;
        for (int c = 0; c < Ca; ++c)
        {
            const cpx* w = Wf (c, p);
            for (int k = 0; k < K; ++k) e += norm2 (w[k]);
        }
        perPart[p] = e;
        if (e > perPart[peakIdx]) peakIdx = p;
    }
    if (perPart[peakIdx] <= 0.0) return -1;
    const int n = np - peakIdx - 1;
    if (n < 3) return np - 1;
    for (int i = 0; i < n; ++i) sorted[i] = perPart[peakIdx + 1 + i];
    std::sort (sorted, sorted + n);
    const double floorE = sorted[n / 4];
    if (floorE > perPart[peakIdx] * 0.03) // no decay of at least 15 dB: no clear floor
    {
        // A real reverb tail that is longer than the filter decays smoothly right up to the end.
        // Ill-conditioned material (identical L/R, pure tones) leaves irregular, non-decaying energy
        // in the late partitions instead; growing the filter for that only makes things worse.
        const bool smoothDecayToEnd = perPart[np - 1] < perPart[np - 2] && perPart[np - 2] < perPart[np - 3];
        return smoothDecayToEnd ? np - 1 : -1;
    }
    for (int p = peakIdx + 1; p < np; ++p)
        if (perPart[p] < 2.0 * floorE) return p;
    return np - 1;
}

// Auto mode: adjusts filter length and residual suppression from what the filter has learned.
//  - Filter length: grows one partition (up to 500 ms) when the learned echo tail still decays
//    smoothly at the end of the filter, i.e. the room's reverb is longer than the filter. It never
//    goes below the knob value. Each step needs three agreeing checks ~0.5 s apart.
//  - Residual suppression: follows how much echo is in the mic (echo estimate vs mic over ~2 s).
//    Plenty of echo -> the standard 20 % / -18 dB; the weaker the echo, the gentler, down to off
//    when no echo reaches the mic, so the voice is never processed harder than the echo warrants.
//  - The filter only grows while the echo is not yet well cancelled (< 15 dB): if it is, the
//    filter length is not what limits it. Length changes start after ~8 s of speaker sound.
void AecCore::autoTuneStep (float micEnergy) noexcept
{
    const double hopSec = (double) B / sr;
    erlePeak = std::max ((double) erleDb, erlePeak - 1.0 * hopSec); // decays 1 dB/s
    if (refActive)
    {
        activeSeconds += hopSec;
        const double ratio = (energy (yF.data(), B) + 1e-12) / (micEnergy + 1e-12);
        const double a = std::min (1.0, hopSec / 2.0); // ~2 s
        echoRatioS += a * (ratio - echoRatioS);
    }
    // echo estimate within 30 dB of the mic, with hysteresis
    echoPresent = echoPresent ? echoRatioS > 1.0e-3 * 0.5 : echoRatioS > 1.0e-3;

    if (++autoCounter < std::max (1, (int) std::lround (0.5 / hopSec)))
        return;
    autoCounter = 0;

    // residual suppression
    if (! echoPresent)
    {
        autoRes = 0.0f;
        autoFloorDb = -12.0f;
    }
    else
    {
        const double ratioDb = 10.0 * std::log10 (echoRatioS + 1e-12);           // echo share of the mic
        autoRes = 0.2f * (float) std::clamp ((ratioDb + 20.0) / 12.0, 0.0, 1.0); // >= -8 dB -> 20 %, <= -20 dB -> 0
        autoFloorDb = -18.0f;
    }

    // filter length (only once the filter has learned something)
    // ... and only after ~8 s of speaker sound, when the filter has settled
    if (! refActive || settings.freeze || erlePeak < 10.0 || ! echoPresent || activeSeconds < 8.0)
        return;
    const int tail = tailPartition();
    if (tail < 0) { growVotes = shrinkVotes = 0; return; } // no reliable reading
    const double partMs = (double) L * 1000.0 / sr;
    const int wanted = std::clamp (tail + 2, (int) std::ceil (100.0 / partMs), Pmax); // + one partition of margin
    // Grow only, never below the knob: shrinking saves little CPU and in tests cost up to 3 dB.
    if (wanted > P && erlePeak < 15.0) // and only while cancellation is still poor
    {
        if (++growVotes >= 3) { autoFilterMs = (float) ((P + 1) * partMs); growVotes = 0; }
    }
    else
    {
        growVotes = 0;
    }
    autoFilterMs = std::clamp (autoFilterMs, settings.filterLengthMs, 500.0f);
    applyFilterLength (autoFilterMs);
}

void AecCore::constrainPartition (int c, int p) noexcept
{
    cpx* w = Wb (c, p);
    fft.inverse (w, frame.data());
    std::fill (frame.begin() + L, frame.end(), 0.0f);
    fft.forward (frame.data(), w);
}

void AecCore::updateDelayEstimate() noexcept
{
    // global peak of the foreground impulse responses
    int bestTap = -1;
    float peak = 0.0f;
    for (int c = 0; c < Ca; ++c)
        for (int p = 0; p < P; ++p)
        {
            fft.inverse (Wf (c, p), frame.data());
            for (int i = 0; i < L; ++i)
            {
                const float a = std::abs (frame[(size_t) i]);
                if (a > peak) { peak = a; bestTap = p * L + i; }
            }
        }
    echoDelayMs = bestTap < 0 ? 0.0f : (float) (bestTap * 1000.0 / sr);
}

void AecCore::residualSuppress (const float* e, const float* y, float* out, bool bypassGain) noexcept
{
    // slide analysis buffers
    std::copy (resE.begin() + B, resE.end(), resE.begin());
    std::copy (e, e + B, resE.begin() + B);
    std::copy (resY.begin() + B, resY.end(), resY.begin());
    std::copy (y, y + B, resY.begin() + B);

    for (int i = 0; i < Nr; ++i) resFrame[(size_t) i] = resE[(size_t) i] * window[(size_t) i];
    fftRes.forward (resFrame.data(), RE.data());

    if (! bypassGain)
    {
        for (int i = 0; i < Nr; ++i) resFrame[(size_t) i] = resY[(size_t) i] * window[(size_t) i];
        fftRes.forward (resFrame.data(), RY.data());

        const float floorGain = std::pow (10.0f, getEffectiveResFloorDb() / 20.0f);
        const float over = 4.0f * getEffectiveResAmount();
        const float leakRes = std::max (leak, 0.02f);
        for (int k = 0; k < Kr; ++k)
        {
            const float r = over * leakRes * norm2 (RY[(size_t) k]);
            const float g = std::max (floorGain, 1.0f - r / (norm2 (RE[(size_t) k]) + kEps));
            float& gs = gain[(size_t) k];
            gs = g < gs ? 0.5f * gs + 0.5f * g : 0.85f * gs + 0.15f * g; // fast attack, slow release
            RE[(size_t) k] *= gs;
        }
    }
    else
    {
        std::fill (gain.begin(), gain.end(), 1.0f);
    }

    fftRes.inverse (RE.data(), resFrame.data());
    for (int i = 0; i < Nr; ++i) ola[(size_t) i] += resFrame[(size_t) i] * window[(size_t) i];
    std::copy (ola.begin(), ola.begin() + B, out);
    std::copy (ola.begin() + B, ola.end(), ola.begin());
    std::fill (ola.begin() + B, ola.end(), 0.0f);
}
} // namespace ableaec
