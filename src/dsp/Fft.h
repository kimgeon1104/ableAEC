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
    // Real FFT of size n (power of two, >= 4), computed with one complex FFT of size n/2.
    void prepare (int size)
    {
        n = size;
        m = n / 2;
        twiddle.resize ((size_t) m / 2 + 1);
        for (int k = 0; k <= m / 2; ++k) // twiddles of the m-point complex FFT (first half suffices)
        {
            const double a = -2.0 * 3.14159265358979323846 * k / m;
            twiddle[(size_t) k] = { (float) std::cos (a), (float) std::sin (a) };
        }
        split.resize ((size_t) m + 1);
        for (int k = 0; k <= m; ++k) // W_n^k used to split / merge the half-size transform
        {
            const double a = -2.0 * 3.14159265358979323846 * k / n;
            split[(size_t) k] = { (float) std::cos (a), (float) std::sin (a) };
        }

        int bits = 0;
        while ((1 << bits) < m) ++bits;
        bitrev.resize ((size_t) m);
        for (int i = 0; i < m; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            bitrev[(size_t) i] = r;
        }
        work.assign ((size_t) m, cpx {});
    }

    int size() const noexcept { return n; }

    // in: n real samples -> out: n/2+1 complex bins (unscaled)
    void forward (const float* in, cpx* out) noexcept
    {
        for (int i = 0; i < m; ++i) work[(size_t) i] = { in[2 * i], in[2 * i + 1] };
        transform (false);
        const cpx z0 = work[0];
        out[0] = { z0.real() + z0.imag(), 0.0f };
        out[m] = { z0.real() - z0.imag(), 0.0f };
        for (int k = 1; k < m; ++k)
        {
            const cpx zk = work[(size_t) k];
            const cpx zc = std::conj (work[(size_t) (m - k)]);
            const cpx fe = 0.5f * (zk + zc);
            const cpx d = 0.5f * (zk - zc);
            const cpx fo = { d.imag(), -d.real() }; // d / i
            out[k] = fe + cmul (split[(size_t) k], fo);
        }
    }

    // in: n/2+1 complex bins -> out: n real samples (scaled by 1/n)
    void inverse (const cpx* in, float* out) noexcept
    {
        for (int k = 0; k < m; ++k)
        {
            const cpx xk = in[k];
            const cpx xc = std::conj (in[m - k]);
            const cpx fe = 0.5f * (xk + xc);
            const cpx fo = cmul (0.5f * (xk - xc), std::conj (split[(size_t) k]));
            work[(size_t) k] = fe + cpx { -fo.imag(), fo.real() }; // fe + i*fo
        }
        transform (true);
        const float scale = 1.0f / (float) m;
        for (int i = 0; i < m; ++i)
        {
            out[2 * i] = work[(size_t) i].real() * scale;
            out[2 * i + 1] = work[(size_t) i].imag() * scale;
        }
    }

private:
    void transform (bool inv) noexcept
    {
        cpx* a = work.data();
        for (int i = 0; i < m; ++i)
        {
            const int r = bitrev[(size_t) i];
            if (i < r) std::swap (a[i], a[r]);
        }
        for (int len = 2; len <= m; len <<= 1)
        {
            const int half = len >> 1;
            const int step = m / len;
            for (int i = 0; i < m; i += len)
            {
                for (int j = 0; j < half; ++j)
                {
                    cpx w = twiddle[(size_t) (j * step)]; // j * step < m / 2 always
                    if (inv) w = std::conj (w);
                    const cpx u = a[i + j];
                    const cpx v = cmul (a[i + j + half], w);
                    a[i + j] = u + v;
                    a[i + j + half] = u - v;
                }
            }
        }
    }

    int n = 0, m = 0;
    std::vector<cpx> twiddle, split;
    std::vector<int> bitrev;
    std::vector<cpx> work;
};
} // namespace ableaec
