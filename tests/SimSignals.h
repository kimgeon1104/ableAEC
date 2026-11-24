#pragma once
// Synthetic test signals shared by the simulation and host tests.

#include <cmath>
#include <random>
#include <vector>

namespace sim
{
constexpr double kSr = 48000.0;

inline std::vector<float> speechLike (int n, unsigned seed)
{
    // AR(2)-coloured noise with syllable-rate amplitude modulation and pauses
    std::mt19937 rng (seed);
    std::normal_distribution<float> g (0.0f, 1.0f);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    std::vector<float> s ((size_t) n);
    float y1 = 0, y2 = 0, env = 0, target = 0;
    int nextChange = 0;
    for (int i = 0; i < n; ++i)
    {
        if (i >= nextChange)
        {
            target = u (rng) < 0.25f ? 0.0f : 0.3f + 0.7f * u (rng);
            nextChange = i + (int) (kSr * (0.08 + 0.2 * u (rng)));
        }
        env += 0.002f * (target - env);
        const float y = g (rng) + 1.6f * y1 - 0.75f * y2;
        y2 = y1;
        y1 = y;
        s[(size_t) i] = 0.03f * env * y;
    }
    return s;
}

inline std::vector<float> roomImpulse (double delayMs, double t60, unsigned seed)
{
    std::mt19937 rng (seed);
    std::normal_distribution<float> g (0.0f, 1.0f);
    const int d = (int) (delayMs * 0.001 * kSr);
    const int len = d + (int) (t60 * kSr);
    std::vector<float> h ((size_t) len, 0.0f);
    h[(size_t) d] = 0.5f;
    const double decay = std::log (1000.0) / (t60 * kSr); // -60 dB over t60
    for (int i = d + 1; i < len; ++i)
        h[(size_t) i] = 0.08f * g (rng) * (float) std::exp (-decay * (i - d));
    return h;
}

inline std::vector<float> convolve (const std::vector<float>& x, const std::vector<float>& h, int from, int to)
{
    std::vector<float> y (x.size(), 0.0f);
    for (int n = from; n < to; ++n)
    {
        double acc = 0.0;
        const int kmax = std::min<int> ((int) h.size(), n + 1);
        for (int k = 0; k < kmax; ++k)
            if (h[(size_t) k] != 0.0f) acc += h[(size_t) k] * x[(size_t) (n - k)];
        y[(size_t) n] = (float) acc;
    }
    return y;
}

} // namespace sim
