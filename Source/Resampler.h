// SPDX-License-Identifier: GPL-3.0-only
// Streaming windowed-sinc resampler (Kaiser window), no dependencies.
//
// The AMB-1 engine only runs at 44.1 kHz. The plugin runs it at 44.1 kHz and uses this to deliver the
// audio at the host's rate. Source frames are pulled from a callback in fixed chunks.
//
//   Resampler r;  r.prepare(44100.0, hostRate);
//   r.process([&](float* l, float* r, int count) { /* fill `count` source frames */ }, outL, outR, n);
//
// At equal rates the filter phase is exactly 0 and the output equals the input (no delay).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

class Resampler
{
public:
    static constexpr int kHalfBase = 32;   // taps each side at full bandwidth (64 taps)
    static constexpr int kPhases = 1024;   // table rows; linear interpolation between rows
    static constexpr int kChunk = 128;     // source frames requested per callback
    static constexpr double kBeta = 9.0;   // Kaiser beta (about 80 dB stopband)

    void prepare(double srcRate, double dstRate)
    {
        ratio_ = srcRate / dstRate;
        const double fc = std::min(1.0, 1.0 / ratio_);   // cutoff relative to the source Nyquist
        half_ = (int)std::ceil((double)kHalfBase / fc);
        taps_ = 2 * half_;
        buildTable(fc);
        cap_ = std::max(4096, 4 * taps_ + 4 * kChunk);
        L_.assign((size_t)cap_, 0.0f);
        R_.assign((size_t)cap_, 0.0f);
        reset();
    }

    void reset()
    {
        std::fill(L_.begin(), L_.end(), 0.0f);
        std::fill(R_.begin(), R_.end(), 0.0f);
        count_ = half_;   // `half_` frames of zero history
        pos_ = (double)half_;
    }

    double ratio() const { return ratio_; }

    template <class Source>
    void process(Source&& src, float* outL, float* outR, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            int n0 = (int)pos_;
            while (count_ < n0 + half_ + 1)
            {
                if (count_ + kChunk > cap_)
                {
                    compact(n0);
                    n0 = (int)pos_;
                }
                src(&L_[(size_t)count_], &R_[(size_t)count_], kChunk);
                count_ += kChunk;
            }
            const double frac = pos_ - (double)n0;
            const double ph = frac * kPhases;
            const int p = (int)ph;
            const float w = (float)(ph - (double)p);
            const float* r0 = &table_[(size_t)p * (size_t)taps_];
            const float* r1 = r0 + taps_;
            const float* xl = &L_[(size_t)(n0 - half_ + 1)];
            const float* xr = &R_[(size_t)(n0 - half_ + 1)];
            float sl = 0.0f, sr = 0.0f;
            for (int j = 0; j < taps_; ++j)
            {
                const float c = r0[j] + w * (r1[j] - r0[j]);
                sl += c * xl[j];
                sr += c * xr[j];
            }
            outL[i] = sl;
            outR[i] = sr;
            pos_ += ratio_;
        }
    }

private:
    static double besselI0(double x)
    {
        double sum = 1.0, term = 1.0;
        const double q = x * x * 0.25;
        for (int k = 1; k < 200; ++k)
        {
            term *= q / ((double)k * (double)k);
            sum += term;
            if (term < 1e-14 * sum)
                break;
        }
        return sum;
    }

    void buildTable(double fc)
    {
        constexpr double kPi = 3.14159265358979323846;
        table_.assign((size_t)(kPhases + 1) * (size_t)taps_, 0.0f);
        const double i0b = besselI0(kBeta);
        std::vector<double> row((size_t)taps_);
        for (int p = 0; p <= kPhases; ++p)
        {
            const double frac = (double)p / (double)kPhases;
            double sum = 0.0;
            for (int j = 0; j < taps_; ++j)
            {
                const int k = j - half_ + 1;
                const double d = (double)k - frac;   // distance from this source frame to the output instant
                const double x = d / (double)half_;
                const double win = std::fabs(x) < 1.0 ? besselI0(kBeta * std::sqrt(1.0 - x * x)) / i0b : 0.0;
                const double a = kPi * fc * d;
                const double s = std::fabs(a) < 1e-12 ? 1.0 : std::sin(a) / a;
                row[(size_t)j] = fc * s * win;
                sum += row[(size_t)j];
            }
            for (int j = 0; j < taps_; ++j)   // unity gain at DC for every phase
                table_[(size_t)p * (size_t)taps_ + (size_t)j] = (float)(row[(size_t)j] / sum);
        }
    }

    void compact(int n0)
    {
        const int keep = n0 - half_;   // first frame still needed
        if (keep <= 0)
            return;
        const int rest = count_ - keep;
        std::memmove(L_.data(), L_.data() + keep, (size_t)rest * sizeof(float));
        std::memmove(R_.data(), R_.data() + keep, (size_t)rest * sizeof(float));
        count_ = rest;
        pos_ -= (double)keep;
    }

    double ratio_ = 1.0;
    double pos_ = 0.0;
    int half_ = kHalfBase, taps_ = 2 * kHalfBase, cap_ = 0, count_ = 0;
    std::vector<float> table_, L_, R_;
};
