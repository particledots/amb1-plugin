// SPDX-License-Identifier: GPL-3.0-only
// Sample-rate regression test (engine + resampler, no JUCE). The engine only runs at 44.1 kHz; the plugin
// resamples to the host's rate. This checks that:
//  (1) the resampler is transparent in the audio band and rejects images (44.1 -> 44.1/48/88.2/96/32 kHz),
//  (2) the garden keeps real time at every host rate (the same number of sixteenths in 30 s; the engine's
//      counter is not reset between runs, so the test measures the difference),
//  (3) only one AmbCore at a time owns the engine, and play/rest work.
//   ./rate_test
#include <cmath>
#include <cstdio>
#include <vector>
#include "AmbCore.h"

static const double kPi = 3.14159265358979323846;
static bool gOk = true;

static void report(const char* what, bool ok, const char* detail = "")
{
    printf("  %-62s %s %s\n", what, ok ? "ok" : "FAIL", detail);
    gOk = gOk && ok;
}

// Amplitude of the component at frequency f (Hann-windowed projection), over x[a..b).
static double amp(const std::vector<float>& x, size_t a, size_t b, double f, double sr)
{
    double re = 0, im = 0, ws = 0;
    for (size_t i = a; i < b; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(2 * kPi * (double)(i - a) / (double)(b - a));
        const double ph = 2 * kPi * f * (double)i / sr;
        re += w * x[i] * std::cos(ph);
        im += w * x[i] * std::sin(ph);
        ws += w;
    }
    return 2.0 * std::sqrt(re * re + im * im) / ws;
}
static double db(double a) { return 20.0 * std::log10(std::max(a, 1e-12)); }

static std::vector<float> resampleSine(double f, double dstRate, double secs)
{
    Resampler rs;
    rs.prepare(44100.0, dstRate);
    long n = 0;
    auto src = [&](float* l, float* r, int count) {
        for (int i = 0; i < count; ++i, ++n)
            l[i] = r[i] = (float)std::sin(2 * kPi * f * (double)n / 44100.0);
    };
    const int total = (int)(secs * dstRate);
    std::vector<float> L((size_t)total), R((size_t)total);
    // odd block sizes on purpose
    for (int done = 0; done < total;)
    {
        const int k = std::min(333, total - done);
        rs.process(src, &L[(size_t)done], &R[(size_t)done], k);
        done += k;
    }
    return L;
}

static void resamplerTests()
{
    printf("resampler\n");
    {   // equal rates: exactly the input
        const auto y = resampleSine(1234.5, 44100.0, 0.5);
        double maxErr = 0;
        for (size_t i = 0; i < y.size(); ++i)
            maxErr = std::max(maxErr, (double)std::fabs(y[i] - (float)std::sin(2 * kPi * 1234.5 * (double)i / 44100.0)));
        char d[64]; snprintf(d, sizeof d, "(max error %.2e)", maxErr);
        report("44.1 -> 44.1 kHz is a pass-through", maxErr < 1e-5, d);
    }
    for (double dst : {48000.0, 88200.0, 96000.0, 32000.0})
        for (double f : {1000.0, 10000.0, 15000.0, 18000.0})
        {
            if (dst == 32000.0 && f > 12000.0)
                continue;   // above the output's passband
            const auto y = resampleSine(f, dst, 2.0);
            const double g = db(amp(y, (size_t)(0.5 * dst), (size_t)(1.5 * dst), f, dst));
            char what[96], d[64];
            snprintf(what, sizeof what, "44.1 -> %.1f kHz: %5.0f Hz sine keeps its level", dst / 1000.0, f);
            snprintf(d, sizeof d, "(%+.3f dB)", g);
            report(what, std::fabs(g) < (f > 16000.0 ? 0.3 : 0.05), d);
        }
    for (double dst : {48000.0, 96000.0})
    {   // an 18 kHz tone makes an image at 26.1 kHz when resampled; it must not be there (at 48 kHz it would alias to 21.9 kHz)
        const auto y = resampleSine(18000.0, dst, 2.0);
        const double f = dst == 48000.0 ? 48000.0 - 26100.0 : 26100.0;
        const double rel = db(amp(y, (size_t)(0.5 * dst), (size_t)(1.5 * dst), f, dst)) -
                           db(amp(y, (size_t)(0.5 * dst), (size_t)(1.5 * dst), 18000.0, dst));
        char what[96], d[64];
        snprintf(what, sizeof what, "44.1 -> %.0f kHz: image of 18 kHz at %.1f kHz", dst / 1000.0, f / 1000.0);
        snprintf(d, sizeof d, "(%.1f dB below)", -rel);
        report(what, rel < -70.0, d);
    }
}

static AmbControls controlsFor(int piece)
{
    AmbControls c;
    int16_t par[P_NPARAMS];
    uint16_t pool;
    en_piece(piece, par, &pool);
    for (int i = 0; i < P_NPARAMS; ++i)
        c.par[(size_t)i] = par[i];
    return c;
}

static void coreTests()
{
    printf("engine through the resampler\n");
    uint32_t bars[3] = {0, 0, 0};
    int k = 0;
    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        AmbCore core;
        const bool got = core.acquire();
        core.prepare(sr, controlsFor(0));
        const int block = sr == 48000.0 ? 480 : 512;
        std::vector<float> L((size_t)block), R((size_t)block);
        double peak = 0, sumsq = 0;
        long nan = 0, n = 0;
        uint32_t base = 0;
        const long total = (long)(30.0 * sr);
        while (n < total)
        {
            core.apply(controlsFor(0));
            core.render(L.data(), R.data(), block, 1.0f);
            if (n == 0)
                base = en_bars;   // after the first block: the engine has started
            for (int i = 0; i < block; ++i)
            {
                for (float x : {L[(size_t)i], R[(size_t)i]})
                {
                    if (x != x) { ++nan; continue; }
                    peak = std::max(peak, (double)std::fabs(x));
                    sumsq += (double)x * x;
                }
            }
            n += block;
        }
        bars[k++] = en_bars - base;
        const double rms = std::sqrt(sumsq / (2.0 * (double)n));
        char what[96], d[96];
        snprintf(what, sizeof what, "Garden at %.1f kHz host rate: finite, audible, no clipping", sr / 1000.0);
        snprintf(d, sizeof d, "(peak %.3f, rms %.4f, %u sixteenths)", peak, rms, (unsigned)(en_bars - base));
        report(what, got && nan == 0 && rms > 0.005 && peak < 0.95, d);
    }
    {
        const bool same = std::abs((int)bars[0] - (int)bars[1]) <= 1 && std::abs((int)bars[0] - (int)bars[2]) <= 1;
        char d[96];
        snprintf(d, sizeof d, "(%u / %u / %u; Garden is 72 bpm, about 144 in 30 s)", (unsigned)bars[0], (unsigned)bars[1], (unsigned)bars[2]);
        report("same tempo at 44.1 / 48 / 96 kHz: sixteenths in 30 s agree", same, d);
    }

    printf("ownership and transport\n");
    {
        AmbCore a, b;
        report("first core acquires the engine", a.acquire());
        report("second core is refused while the first holds it", !b.acquire() && !b.isActive());
        a.release();
        report("after release, the second core can acquire", b.acquire() && b.isActive() && !a.isActive());
        b.prepare(48000.0, controlsFor(2));
        std::vector<float> L(512), R(512);
        AmbControls c = controlsFor(2);
        for (int i = 0; i < 20; ++i) { b.apply(c); b.render(L.data(), R.data(), 512, 1.0f); }
        report("play is on after prepare", en_playing == 1);
        c.play = false;
        b.apply(c);
        b.render(L.data(), R.data(), 512, 1.0f);
        report("play off rests the garden", en_playing == 0);
        c.play = true;
        b.apply(c);
        b.render(L.data(), R.data(), 512, 1.0f);
        report("play on grows it again", en_playing == 1);
        c.freeze = true;
        b.apply(c);
        b.render(L.data(), R.data(), 512, 1.0f);
        report("freeze holds the chord", en_frozen == 1);
    }
}

int main()
{
    resamplerTests();
    coreTests();
    printf(gOk ? "PASS\n" : "FAIL\n");
    return gOk ? 0 : 1;
}
