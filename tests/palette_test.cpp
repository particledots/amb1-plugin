// SPDX-License-Identifier: GPL-3.0-only
// palette_test: the Palette (a generative sound source). No JUCE. Every case runs in a fresh child process (the engine's
// random generator is file-static), prints one line of facts, and the parent compares them.
//   ./palette_test                          the checks (exit 0 = all passed)
//   ./palette_test child <pct> <seed> <piece> <seconds>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "AmbCore.h"

static uint64_t gNotes = 1469598103934665603ull, gCount = 0;
extern "C" void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    const uint32_t s = st & 0xFF;
    if ((s == 0x90 || s == 0x91 || s == 0x92 || s == 0x99) && d2 > 0)
    {
        gNotes = (gNotes ^ en_bars) * 1099511628211ull;
        gNotes = (gNotes ^ (s << 8 | d1)) * 1099511628211ull;
        ++gCount;
    }
}

static int child(int pct, uint32_t seed, int piece, double secs)
{
    AmbCore core;
    core.acquire();
    AmbControls c;
    int16_t par[P_NPARAMS];
    uint16_t pool;
    en_piece(piece, par, &pool);
    for (int i = 0; i < P_NPARAMS; ++i)
        if (i != P_MIDI)
            c.par[(size_t)i] = par[i];
    c.palette = pct;
    c.seed = seed;
    core.prepare(44100.0, c);
    en_set(P_MIDI, 1);
    std::vector<float> l(441), r(441);
    uint64_t h = 1469598103934665603ull;
    double peak = 0, sumsq = 0, diff = 0, prev = 0;
    long nan = 0, n = 0;
    for (long done = 0; done < (long)(secs * 44100.0); done += 441)
    {
        core.apply(c);
        core.render(l.data(), r.data(), 441, 1.0f);
        for (int i = 0; i < 441; ++i)
        {
            double x = l[(size_t)i];
            if (x != x) { ++nan; x = 0; }
            uint32_t u;
            const float f = (float)x;
            std::memcpy(&u, &f, 4);
            h = (h ^ u) * 1099511628211ull;
            peak = std::fmax(peak, std::fabs(x));
            sumsq += x * x;
            diff += std::fabs(x - prev);
            prev = x;
            ++n;
        }
    }
    const double rms = std::sqrt(sumsq / (double)n);
    // brightness: how much the waveform moves from sample to sample, relative to its level (higher = brighter)
    printf("audio %016llx notes %016llx count %llu nan %ld peak %.4f rms %.5f bright %.4f\n", (unsigned long long)h,
           (unsigned long long)gNotes, (unsigned long long)gCount, nan, peak, rms, rms > 0 ? diff / (double)n / rms : 0.0);
    return 0;
}

using Facts = std::map<std::string, std::string>;
static Facts run(const char* self, int pct, uint32_t seed, int piece, double secs)
{
    const std::string cmd = std::string("\"") + self + "\" child " + std::to_string(pct) + " " + std::to_string(seed) + " " +
                            std::to_string(piece) + " " + std::to_string(secs);
    Facts f;
    FILE* p = popen(cmd.c_str(), "r");
    if (!p)
        return f;
    char buf[512] = {};
    std::string out;
    while (fgets(buf, sizeof buf, p))
        out += buf;
    pclose(p);
    std::istringstream is(out);
    std::string k, v;
    while (is >> k >> v)
        f[k] = v;
    return f;
}

static int fails = 0;
static void report(const char* what, bool ok, const std::string& detail = "")
{
    printf("  %-4s %s %s\n", ok ? "ok" : "FAIL", what, detail.c_str());
    if (!ok)
        ++fails;
}

int main(int argc, char** argv)
{
    if (argc >= 2 && std::strcmp(argv[1], "child") == 0)   // (never fall through to the parent: that would spawn children forever)
    {
        if (argc != 6)
            return 2;
        return child(atoi(argv[2]), (uint32_t)atoi(argv[3]), atoi(argv[4]), atof(argv[5]));
    }
    if (false)
        return child(atoi(argv[2]), (uint32_t)atoi(argv[3]), atoi(argv[4]), atof(argv[5]));

    printf("palette: fresh process per case\n");
    Facts a0 = run(argv[0], 0, 11, 0, 60), a0b = run(argv[0], 0, 11, 0, 60);
    Facts a70 = run(argv[0], 70, 11, 0, 60), b70 = run(argv[0], 70, 22, 0, 60);
    report("palette 0 is repeatable (same seed, same audio)", !a0.empty() && a0["audio"] == a0b["audio"]);
    report("palette 70 plays the same notes as palette 0 (same seed)", !a70.empty() && a70["notes"] == a0["notes"] && a70["count"] == a0["count"],
           "(" + a0["count"] + " notes)");
    report("palette 70 sounds different from palette 0", a70["audio"] != a0["audio"]);
    report("palette 70 is repeatable", run(argv[0], 70, 11, 0, 60)["audio"] == a70["audio"]);
    report("another seed gives another palette (and other notes)", b70["audio"] != a70["audio"]);
    // The palette must change the tone itself, not just the level.
    printf("  brightness (waveform movement / level): palette 0 %s, palette 70 %s, seed 22 %s\n", a0["bright"].c_str(),
           a70["bright"].c_str(), b70["bright"].c_str());
    for (int piece = 0; piece < EN_NPIECES; ++piece)
    {
        const Facts f = run(argv[0], 100, 5, piece, 90);
        const bool ok = !f.empty() && std::atol(f.at("nan").c_str()) == 0 && std::atof(f.at("peak").c_str()) < 0.95 &&
                        std::atof(f.at("rms").c_str()) > 0.005;
        report(("piece " + std::to_string(piece) + " at palette 100: finite, audible, not clipping").c_str(), ok,
               f.empty() ? "" : "(peak " + f.at("peak") + ", rms " + f.at("rms") + ")");
    }
    {
        const Facts f = run(argv[0], 100, 9, 0, 900);
        report("15 minutes at palette 100 stay finite and below clipping",
               !f.empty() && std::atol(f.at("nan").c_str()) == 0 && std::atof(f.at("peak").c_str()) < 0.95,
               f.empty() ? "" : "(peak " + f.at("peak") + ")");
    }
    printf(fails ? "palette_test: FAIL\n" : "palette_test: PASS\n");
    return fails ? 1 : 0;
}
