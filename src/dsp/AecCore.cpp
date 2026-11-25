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

// Kalman filter tuning
constexpr float kKalmanA = 0.999f;   // state transition (echo-path drift)
constexpr float kKalmanQ = 1.0e-8f;  // process-noise floor (keeps the filter able to track)
constexpr float kKalmanP0 = 1.0f;    // initial state uncertainty
constexpr float kPsiScale = 2.0f;    // N/B: |E|^2 of a B-sample error vs |X|^2 of an N-sample frame
constexpr float kPathChangeLeak = 0.5f; // leakage above which the echo path is assumed to have changed
constexpr float kPathChangeInflate = 1.5f; // per-block growth of the state uncertainty after a path change

template <typename T>
void zero (std::vector<T>& v) { std::fill (v.begin(), v.end(), T {}); }

float energy (const float* x, int n) noexcept
{
    float s = 0.0f;
    for (int i = 0; i < n; ++i) s += x[i] * x[i];
    return s;
}
} // namespace

int AecCore::blockSizeFor (double sampleRate, bool lowLatency) noexcept
{
    // 256 / 512 samples at 44.1-48 kHz, scaled so the block keeps the same duration
    const double target = (lowLatency ? 256.0 : 512.0) * sampleRate / 48000.0;
    int b = 64;
    while (b * 2 <= (int) (target * 1.25)) b *= 2;
    return b;
}

void AecCore::prepare (double sampleRate, float maxFilterLengthMs, int blockSize, int numRefs)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    B = std::max (64, blockSize);
    N = 2 * B;
    K = N / 2 + 1;
    C = std::clamp (numRefs, 1, kMaxRefs);
    Ca = C;
    Pmax = std::max (1, (int) std::ceil (maxFilterLengthMs * 0.001 * sr / B));

    fft.prepare (N);
    const size_t wsize = (size_t) C * (size_t) Pmax * (size_t) K;
    X.assign (wsize, cpx {});
    Wfg.assign (wsize, cpx {});
    Wbg.assign (wsize, cpx {});
    Pcov.assign (wsize, kKalmanP0);
    refPrev.assign ((size_t) C * (size_t) B, 0.0f);
    frame.assign ((size_t) N, 0.0f);
    for (auto* v : { &yF, &yB, &eF, &eB, &micCopy, &scratch, &prevMic, &prevY }) v->assign ((size_t) B, 0.0f);
    for (auto* v : { &acc, &EF, &EB, &YF, &RE, &RY }) v->assign ((size_t) K, cpx {});
    for (auto* v : { &kalDen, &psiS, &meanE, &meanY }) v->assign ((size_t) K, 0.0f);

    window.resize ((size_t) N);
    for (int i = 0; i < N; ++i) // periodic sqrt-Hann -> perfect reconstruction at 50 % overlap
        window[(size_t) i] = (float) std::sqrt (0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * i / N));
    resE.assign ((size_t) N, 0.0f);
    resY.assign ((size_t) N, 0.0f);
    ola.assign ((size_t) N, 0.0f);
    gain.assign ((size_t) K, 1.0f);

    P = 1;
    setSettings (settings);
    reset();
}

void AecCore::reset()
{
    zero (X);
    zero (refPrev);
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
    sFF = sBB = sDD = 0.0;
    betterCount = worseCount = 0;
    micPowS = errPowS = 1e-9;
    erleDb = 0.0f;
    echoDelayMs = 0.0f;
}

void AecCore::setActiveRefs (int n) noexcept
{
    n = std::clamp (n, 1, C);
    if (n == Ca) return;
    Ca = n;
    zero (X);
    zero (refPrev);
    resetFilter();
}

void AecCore::setSettings (const AecSettings& s) noexcept
{
    settings = s;
    const int newP = std::clamp ((int) std::ceil (s.filterLengthMs * 0.001 * sr / B), 1, Pmax);
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
            const cpx* w = W.data() + idx (c, p);
            for (int k = 0; k < K; ++k) acc[(size_t) k] += cmul (w[k], x[k]);
        }
    fft.inverse (acc.data(), frame.data());
    std::copy (frame.begin() + B, frame.end(), yOut); // overlap-save: valid half
}

void AecCore::processBlock (const float* const* refs, const float* mic, float* out) noexcept
{
    float* curMic = micCopy.data(); // `out` may alias `mic`
    std::copy (mic, mic + B, curMic);

    // ---------------------------------------------------------------- 1. reference spectra
    xHead = (xHead - 1 + Pmax) % Pmax; // newest spectrum lives at partition 0
    float sx = 0.0f;
    for (int c = 0; c < Ca; ++c)
    {
        float* prev = refPrev.data() + (size_t) c * (size_t) B;
        std::copy (prev, prev + B, frame.begin());
        std::copy (refs[c], refs[c] + B, frame.begin() + B);
        std::copy (refs[c], refs[c] + B, prev);
        fft.forward (frame.data(), Xp (c, 0));
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
    const float sb = energy (eB.data(), B);
    const auto wCount = (ptrdiff_t) Wfg.size();

    // ---------------------------------------------------------------- 3. two-path control
    if (refActive && ! settings.freeze)
    {
        constexpr double a = 0.25;
        sFF = (1.0 - a) * sFF + a * sf;
        sBB = (1.0 - a) * sBB + a * sb;
        sDD = (1.0 - a) * sDD + a * sd;

        if (sBB < 0.8 * sFF) // background cancels > ~1 dB more: promote it
        {
            if (++betterCount >= 2)
            {
                std::copy (Wbg.begin(), Wbg.begin() + wCount, Wfg.begin());
                std::copy (yB.begin(), yB.end(), yF.begin());
                std::copy (eB.begin(), eB.end(), eF.begin());
                sf = sb;
                sFF = sBB;
                betterCount = 0;
            }
        }
        else
        {
            betterCount = 0;
        }

        // background clearly worse than foreground (typically double-talk): restart from foreground
        if (sBB > 2.0 * sFF + 1.0e-7 * B)
        {
            if (++worseCount >= 4)
            {
                std::copy (Wfg.begin(), Wfg.begin() + wCount, Wbg.begin());
                std::copy (eF.begin(), eF.end(), eB.begin());
                sBB = sFF;
                worseCount = 0;
            }
        }
        else
        {
            worseCount = 0;
        }

        // foreground divergence guard (should never trigger in normal operation)
        if (sFF > 8.0 * sDD + 1.0e-6 * B)
        {
            resetFilter();
            std::fill (yF.begin(), yF.end(), 0.0f);
            std::copy (curMic, curMic + B, eF.begin());
            std::copy (curMic, curMic + B, eB.begin());
            sf = sd;
        }
    }

    // ---------------------------------------------------------------- 4. spectra
    std::fill (frame.begin(), frame.begin() + B, 0.0f);
    std::copy (eB.begin(), eB.end(), frame.begin() + B);
    fft.forward (frame.data(), EB.data());
    std::copy (eF.begin(), eF.end(), frame.begin() + B);
    fft.forward (frame.data(), EF.data());
    std::copy (yF.begin(), yF.end(), frame.begin() + B);
    fft.forward (frame.data(), YF.data());

    // ---------------------------------------------------------------- 5. leakage + meters (foreground)
    if (refActive)
    {
        double peyInst = 0.0, pyyInst = 0.0;
        for (int k = 0; k < K; ++k)
        {
            const float e2 = norm2 (EF[(size_t) k]);
            const float y2 = norm2 (YF[(size_t) k]);
            meanE[(size_t) k] += 0.05f * (e2 - meanE[(size_t) k]);
            meanY[(size_t) k] += 0.05f * (y2 - meanY[(size_t) k]);
            const double de = e2 - meanE[(size_t) k];
            const double dy = y2 - meanY[(size_t) k];
            peyInst += de * dy;
            pyyInst += dy * dy;
        }
        constexpr double a = 0.05;
        pey = (1.0 - a) * pey + a * peyInst;
        pyy = (1.0 - a) * pyy + a * pyyInst;
        leak = (float) std::clamp (pyy > 1e-20 ? pey / pyy : 1.0, (double) kMinLeak, 1.0);

        micPowS = 0.97 * micPowS + 0.03 * sd;
        errPowS = 0.97 * errPowS + 0.03 * sf;
        erleDb = (float) (10.0 * std::log10 ((micPowS + 1e-12) / (errPowS + 1e-12)));
    }

    // ---------------------------------------------------------------- 6. background adaptation
    // Diagonalised partitioned-block frequency-domain Kalman filter (Kuech, Mabande & Enzner 2014).
    // Every (channel, partition, bin) keeps its own state uncertainty Pc: bins that have never been
    // excited (e.g. a new chord in the backing track) learn at full speed as soon as they appear,
    // while near-end sound (raising the observation noise psiS) automatically slows adaptation.
    if (refActive && ! settings.freeze)
    {
        const float a2 = kKalmanA * kKalmanA;

        // Echo-path change (mic / speaker moved): the error is still strongly correlated with the echo
        // estimate, unlike near-end singing. Re-open the state uncertainty so the filter re-learns fast.
        if (leak > kPathChangeLeak)
            for (auto& v : Pcov) v = std::min (kKalmanP0, v * kPathChangeInflate + 1.0e-6f);

        const float gainScale = 2.0f * settings.stepSize; // default 0.5 -> 1
        for (int k = 0; k < K; ++k)
        {
            psiS[(size_t) k] = 0.5f * psiS[(size_t) k] + 0.5f * norm2 (EB[(size_t) k]);
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
            kalDen[(size_t) k] = 1.0f / (kalDen[(size_t) k] + kPsiScale * psiS[(size_t) k] + delta);

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
                    const float shrink = std::max (0.0f, 1.0f - 0.5f * g * x2); // 0.5 = B/N
                    pc[k] = a2 * shrink * pc[k] + (1.0f - a2) * norm2 (w[k]) + kKalmanQ;
                }
            }

        // gradient constraint (time-domain causality), round-robin, every channel
        for (int n = 0; n < 2; ++n)
        {
            for (int c = 0; c < Ca; ++c) constrainPartition (c, constrainIdx);
            constrainIdx = (constrainIdx + 1) % P;
        }
    }

    if (++blockCounter >= std::max (1, 4096 / B))
    {
        blockCounter = 0;
        updateDelayEstimate();
    }

    // ---------------------------------------------------------------- 7. output
    switch (settings.mode)
    {
        case OutputMode::Clean:
            residualSuppress (eF.data(), yF.data(), out, settings.resAmount <= 0.0f);
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

void AecCore::constrainPartition (int c, int p) noexcept
{
    cpx* w = Wb (c, p);
    fft.inverse (w, frame.data());
    std::fill (frame.begin() + B, frame.end(), 0.0f);
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
            for (int i = 0; i < B; ++i)
            {
                const float a = std::abs (frame[(size_t) i]);
                if (a > peak) { peak = a; bestTap = p * B + i; }
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

    for (int i = 0; i < N; ++i) frame[(size_t) i] = resE[(size_t) i] * window[(size_t) i];
    fft.forward (frame.data(), RE.data());

    if (! bypassGain)
    {
        for (int i = 0; i < N; ++i) frame[(size_t) i] = resY[(size_t) i] * window[(size_t) i];
        fft.forward (frame.data(), RY.data());

        const float floorGain = std::pow (10.0f, settings.resFloorDb / 20.0f);
        const float over = 4.0f * settings.resAmount;
        const float leakRes = std::max (leak, 0.02f);
        for (int k = 0; k < K; ++k)
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

    fft.inverse (RE.data(), frame.data());
    for (int i = 0; i < N; ++i) ola[(size_t) i] += frame[(size_t) i] * window[(size_t) i];
    std::copy (ola.begin(), ola.begin() + B, out);
    std::copy (ola.begin() + B, ola.end(), ola.begin());
    std::fill (ola.begin() + B, ola.end(), 0.0f);
}
} // namespace ableaec
