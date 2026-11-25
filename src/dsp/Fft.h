#pragma once
// Minimal, dependency-free radix-2 FFT used by the AEC core.
// Real-input helpers return/accept the non-redundant half spectrum (N/2 + 1 bins).

#include <cmath>
#include <complex>
#include <vector>

namespace ableaec
{
using cpx = std::complex<float>;

// Plain complex multiply (avoids the slow NaN-checking path of std::complex operator*).
inline cpx cmul (cpx a, cpx b) noexcept
{
    return { a.real() * b.real() - a.imag() * b.imag(),
             a.real() * b.imag() + a.imag() * b.real() };
}

// conj(a) * b
inline cpx cmulConj (cpx a, cpx b) noexcept
{
    return { a.real() * b.real() + a.imag() * b.imag(),
             a.real() * b.imag() - a.imag() * b.real() };
}

inline float norm2 (cpx a) noexcept { return a.real() * a.real() + a.imag() * a.imag(); }

class Fft
{
public:
    void prepare (int size)
    {
        n = size;
        twiddle.resize ((size_t) n / 2);
        for (int k = 0; k < n / 2; ++k)
        {
            const double a = -2.0 * 3.14159265358979323846 * k / n;
            twiddle[(size_t) k] = { (float) std::cos (a), (float) std::sin (a) };
        }

        int bits = 0;
        while ((1 << bits) < n) ++bits;
        bitrev.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            bitrev[(size_t) i] = r;
        }
        work.assign ((size_t) n, cpx {});
    }

    int size() const noexcept { return n; }

    // in: n real samples -> out: n/2+1 complex bins (unscaled)
    void forward (const float* in, cpx* out) noexcept
    {
        for (int i = 0; i < n; ++i) work[(size_t) i] = { in[i], 0.0f };
        transform (false);
        for (int k = 0; k <= n / 2; ++k) out[k] = work[(size_t) k];
    }

    // in: n/2+1 complex bins -> out: n real samples (scaled by 1/n)
    void inverse (const cpx* in, float* out) noexcept
    {
        const int half = n / 2;
        work[0] = { in[0].real(), 0.0f };
        work[(size_t) half] = { in[half].real(), 0.0f };
        for (int k = 1; k < half; ++k)
        {
            work[(size_t) k] = in[k];
            work[(size_t) (n - k)] = std::conj (in[k]);
        }
        transform (true);
        const float scale = 1.0f / (float) n;
        for (int i = 0; i < n; ++i) out[i] = work[(size_t) i].real() * scale;
    }

private:
    void transform (bool inv) noexcept
    {
        cpx* a = work.data();
        for (int i = 0; i < n; ++i)
        {
            const int r = bitrev[(size_t) i];
            if (i < r) std::swap (a[i], a[r]);
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len >> 1;
            const int step = n / len;
            for (int i = 0; i < n; i += len)
            {
                for (int j = 0; j < half; ++j)
                {
                    cpx w = twiddle[(size_t) (j * step)];
                    if (inv) w = std::conj (w);
                    const cpx u = a[i + j];
                    const cpx v = cmul (a[i + j + half], w);
                    a[i + j] = u + v;
                    a[i + j + half] = u - v;
                }
            }
        }
    }

    int n = 0;
    std::vector<cpx> twiddle;
    std::vector<int> bitrev;
    std::vector<cpx> work;
};
} // namespace ableaec
