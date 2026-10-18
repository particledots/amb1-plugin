// SPDX-License-Identifier: GPL-3.0-only
// drum_test: the three drum levels (thump, closed hat, open hat). No JUCE.
// Each case renders the same garden (Pulse and Hits at 100, Reverb and Echo at 0) in a fresh child process, so the
// engine's random choices are the same in every case and only the drum levels differ. The reference is the render
// with all three levels at 0; what a case adds to it is that case's drum sound.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "AmbCore.h"

extern "C" void en_midi_out(uint32_t, uint32_t, uint32_t) {}

static const double kRate = 48000.0;
static const int kSeconds = 30;

static AmbControls garden(int t, int h, int o)
{
    AmbControls c;
    c.par[P_PULSE] = 100;
    c.par[P_HITS] = 100;
    c.par[P_REVERB] = 0;
    c.par[P_ECHO] = 0;
    c.drum = {{t, h, o}};
    return c;
}

static int child(int t, int h, int o)
{
    AmbCore core;
    core.acquire();
    AmbControls c = garden(t, h, o);
    core.prepare(kRate, c);
    std::vector<float> L(480), R(480);
    for (int i = 0; i < kSeconds * 100; ++i)
    {
        core.apply(c);
        core.render(L.data(), R.data(), 480, 1.0f);
        fwrite(L.data(), sizeof(float), L.size(), stdout);
    }
    fflush(stdout);
    return 0;
}

static std::vector<float> run(const char* self, int t, int h, int o)
{
    std::string cmd = std::string("\"") + self + "\" child " + std::to_string(t) + " " + std::to_string(h) + " " + std::to_string(o);
    std::vector<float> out;
    FILE* p = popen(cmd.c_str(), "r");
    if (p == nullptr)
        return out;
    float buf[4096];
    size_t n;
    while ((n = fread(buf, sizeof(float), 4096, p)) > 0)
        out.insert(out.end(), buf, buf + n);
    pclose(p);
    return out;
}

static double rmsDiff(const std::vector<float>& a, const std::vector<float>& b)
{
    double s = 0.0;
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i)
    {
        const double d = (double)a[i] - (double)b[i];
        s += d * d;
    }
    return n ? std::sqrt(s / (double)n) : 0.0;
}

static int fails = 0;
static void report(const char* what, bool ok, const std::string& detail = "")
{
    printf("  %s: %s %s\n", what, ok ? "ok" : "FAIL", detail.c_str());
    if (!ok)
        ++fails;
}

int main(int argc, char** argv)
{
    if (argc == 5 && std::strcmp(argv[1], "child") == 0)
        return child(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]));

    printf("drum levels: %d s of the same garden per case, fresh process each\n", kSeconds);
    const size_t want = (size_t)kSeconds * 100 * 480;
    const auto zero = run(argv[0], 0, 0, 0);
    report("the reference render has the expected length", zero.size() == want, "(" + std::to_string(zero.size()) + " samples)");

    struct Case { int t, h, o; };
    const Case cases[] = {{100, 100, 100}, {100, 0, 0}, {0, 100, 0}, {0, 0, 100}, {50, 0, 0}, {150, 0, 0}, {0, 50, 0},
                          {0, 150, 0}, {0, 0, 50}, {0, 0, 150}, {0, 400, 0}, {0, 0, 400}};
    std::vector<double> r;
    for (const auto& c : cases)
    {
        const auto out = run(argv[0], c.t, c.h, c.o);
        r.push_back(out.size() == zero.size() ? rmsDiff(out, zero) : -1.0);
        printf("  thump %3d  closed hat %3d  open hat %3d : drum sound (RMS over the reference) %.5f\n", c.t, c.h, c.o, r.back());
    }
    report("with all three at 100 the drums are audible", r[0] > 1e-4);
    report("thump alone is audible", r[1] > 1e-4);
    report("closed hat alone is audible", r[2] > 1e-4);
    report("open hat alone is audible", r[3] > 1e-4);
    // the output goes through a soft clipper, so the ratios are close to, not exactly, the level ratios
    char t[200];
    const char* names[3] = {"thump", "closed hat", "open hat"};
    const double full[3] = {r[1], r[2], r[3]};
    const double half[3] = {r[4], r[6], r[8]};
    const double more[3] = {r[5], r[7], r[9]};
    for (int d = 0; d < 3; ++d)
    {
        const double a = half[d] / full[d], b = more[d] / full[d];
        std::snprintf(t, sizeof t, "(50%% gives %.2f of 100%%, 150%% gives %.2f)", a, b);
        report((std::string(names[d]) + " follows its level").c_str(), a > 0.42 && a < 0.58 && b > 1.35 && b < 1.65, t);
    }
    for (int d = 1; d < 3; ++d)   // v0.12: the hats go up to 400 %
    {
        const double x = r[(size_t)(d == 1 ? 10 : 11)] / full[d];
        std::snprintf(t, sizeof t, "(400%% gives %.2f of 100%%)", x);
        report((std::string(names[d]) + " follows its level up to 400 %").c_str(), x > 3.4 && x < 4.6, t);
    }
    const double sumSq = full[0] * full[0] + full[1] * full[1] + full[2] * full[2];
    std::snprintf(t, sizeof t, "(together %.5f, the three alone add to %.5f)", r[0], std::sqrt(sumSq));
    report("the three drums add up to the full pulse (so each level moves only its own drum)",
           std::fabs(r[0] - std::sqrt(sumSq)) < 0.1 * std::sqrt(sumSq), t);
    printf(fails ? "drum_test: FAIL\n" : "drum_test: PASS\n");
    return fails ? 1 : 0;
}
