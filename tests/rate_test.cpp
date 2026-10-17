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
#include <array>
#include <string>
#include <vector>
#include "AmbCore.h"
#include "Scale.h"
#include "GardenState.h"

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

    printf("tempo sync\n");
    {
        // Sixteenths over `secs` seconds. The host tempo is reported before every block, as the plugin does.
        struct Run
        {
            AmbCore core;
            AmbControls c = controlsFor(0);   // Garden: 72 bpm, so 4.8 sixteenths a second on its own
            double sr;
            int block;
            std::vector<float> L, R;
            explicit Run(double rate) : sr(rate), block(rate == 48000.0 ? 480 : 512), L((size_t)block), R((size_t)block)
            {
                core.acquire();
                core.prepare(sr, c);
            }
            uint32_t go(double secs, double bpm)
            {
                // Warm up half a second before taking the count. en_bars is only written when the engine steps, and
                // a (re)start from the top sets the step counter to 0: right after prepare it can still hold the
                // previous run's value.
                for (long n = 0; n < (long)(0.5 * sr); n += block)
                {
                    core.setHostTempo(bpm);
                    core.apply(c);
                    core.render(L.data(), R.data(), block, 1.0f);
                }
                const uint32_t base = en_bars;
                for (long n = 0; n < (long)(secs * sr); n += block)
                {
                    core.setHostTempo(bpm);
                    core.apply(c);
                    core.render(L.data(), R.data(), block, 1.0f);
                }
                return en_bars - base;
            }
        };
        auto near = [](uint32_t got, int want, int tol) { return std::abs((int)got - want) <= tol; };
        char d[96];

        for (double sr : {44100.0, 48000.0, 96000.0})
        {
            Run run(sr);
            run.go(3.0, 0.0);   // sync off: the host's tempo is ignored
            const uint32_t off = run.go(20.0, 120.0);
            run.c.syncToHost = true;
            run.go(2.0, 120.0);   // settle
            const uint32_t at120 = run.go(20.0, 120.0);
            const uint32_t at90 = run.go(20.0, 90.0);
            char what[96];
            snprintf(what, sizeof what, "%.1f kHz host: off ignores the host tempo, sync follows it", sr / 1000.0);
            snprintf(d, sizeof d, "(off %u, 120 bpm %u, 90 bpm %u; want 96 / 160 / 120)", (unsigned)off, (unsigned)at120, (unsigned)at90);
            report(what, near(off, 96, 2) && near(at120, 160, 2) && near(at90, 120, 2), d);
        }
        {
            Run run(48000.0);
            run.c.syncToHost = true;
            const uint32_t unknown = run.go(20.0, 0.0);   // host never gives a tempo: stays on its own
            snprintf(d, sizeof d, "(%u sixteenths in 20 s; want 96)", (unsigned)unknown);
            report("sync on but no host tempo yet: keeps the Tempo parameter's tempo", near(unknown, 96, 2), d);
        }
        {
            Run run(48000.0);
            run.c.syncToHost = true;
            run.go(2.0, 100.0);
            const uint32_t held = run.go(20.0, 0.0);   // the host stops reporting: the last tempo is kept
            snprintf(d, sizeof d, "(%u sixteenths in 20 s; want 133 at 100 bpm)", (unsigned)held);
            report("host stops reporting a tempo: the last one is kept", near(held, 133, 2), d);
        }
        {
            Run run(44100.0);
            run.c.syncToHost = true;
            run.go(2.0, 120.0);
            run.c.syncToHost = false;
            run.go(2.0, 120.0);   // the engine takes a few chunks to notice the ticks stopped
            const uint32_t back = run.go(20.0, 120.0);
            snprintf(d, sizeof d, "(%u sixteenths in 20 s; want 96)", (unsigned)back);
            report("sync switched off: back to the Tempo parameter's tempo", near(back, 96, 2), d);
        }
        {
            Run run(48000.0);
            run.c.syncToHost = true;
            run.go(2.0, 60.0);
            uint32_t total = 0;
            for (double bpm : {60.0, 90.0, 120.0, 90.0})   // a tempo change every 5 s
                total += run.go(5.0, bpm);
            snprintf(d, sizeof d, "(%u sixteenths in 20 s; want 20 + 30 + 40 + 30 = 120)", (unsigned)total);
            report("tempo changes while playing are followed", near(total, 120, 3), d);
        }
        {
            // every sixteenth's length at 120 bpm should be ~125 ms: the spread of tick-to-tick timing is small
            Run run(48000.0);
            run.c.syncToHost = true;
            run.go(2.0, 120.0);
            std::vector<double> at;   // time (s) at which en_bars changed
            uint32_t last = en_bars;
            double t = 0;
            for (long n = 0; n < (long)(20.0 * run.sr); n += run.block)
            {
                run.core.setHostTempo(120.0);
                run.core.apply(run.c);
                run.core.render(run.L.data(), run.R.data(), run.block, 1.0f);
                t += (double)run.block / run.sr;
                if (en_bars != last)
                {
                    at.push_back(t);
                    last = en_bars;
                }
            }
            double worst = 0;
            for (size_t i = 1; i < at.size(); ++i)
                worst = std::max(worst, std::fabs((at[i] - at[i - 1]) - 0.125));
            snprintf(d, sizeof d, "(%zu steps seen; worst gap error %.1f ms, which includes the %.1f ms block size)", at.size(),
                     worst * 1000.0, 1000.0 * run.block / run.sr);
            report("sixteenths arrive evenly at 120 bpm (within one block)", at.size() > 100 && worst < (double)run.block / run.sr + 0.004, d);
        }
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

// The snapshot the editor draws from: does it follow the engine, and is it sane while the garden plays?
static void gardenTests()
{
    printf("garden state (what the editor shows)\n");
    const int lv[4] = {100, 100, 100, 100};
    {
        const GardenState c = GardenState::capture(false, 0, MODE_IONIAN, lv);
        const GardenState d = GardenState::capture(false, 2, MODE_IONIAN, lv);
        char t[96];
        snprintf(t, sizeof t, "(C Ionian 0x%03X, D Ionian 0x%03X; want 0xAB5, 0xAD6)", c.pool, d.pool);
        report("the scale is the mode's pool turned to the root", c.pool == 0xAB5u && d.pool == 0xAD6u, t);
        report("not live (another instance owns the engine): nothing else is shown", !c.live && !c.playing && c.chord == 0 &&
               c.chordName[0] == 0 && c.levels[0] == 0.0f);
    }
    AmbCore core;
    core.acquire();
    AmbControls c = controlsFor(0);
    // en_init() does not clear the engine's frozen flag, and an earlier test left it set: thaw it by hand so this
    // test starts from a plain garden. (A real re-prepare with Freeze off has the same carry-over: noted in the status file.)
    core.prepare(48000.0, c);
    en_freeze(0);   // (after prepare: en_init() empties the command queue)
    std::vector<float> L(480), R(480);
    bool nameOk = true, chordOk = true, rangeOk = true, posMoved = false, glowSeen = false, meterSeen = false, notesSeen = false,
         hitsSeen = false, playing = true;
    float maxDrift = 0.0f;
    int lastPos = -1;
    char first[16] = "";
    const int lv2[4] = {c.par[P_PAD], c.par[P_BELLS], c.par[P_BASS], c.par[P_PULSE]};
    // look every 50 ms, like the editor does (the glow of a bell fades in well under a second)
    for (int step = 0; step < 1800; ++step)
    {
        for (int i = 0; i < 5; ++i)
        {
            core.apply(c);
            core.render(L.data(), R.data(), 480, 1.0f);
        }
        const GardenState g = GardenState::capture(true, c.par[P_ROOT], c.par[P_MODE], lv2);
        if (step == 0)
            snprintf(first, sizeof first, "%s", g.chordName);
        playing = playing && g.playing;
        nameOk = nameOk && g.chordName[0] >= 'A' && g.chordName[0] <= 'G';
        const int n = __builtin_popcount(g.chord);
        chordOk = chordOk && n >= 3 && n <= 4;
        maxDrift = std::max(maxDrift, g.drift);
        for (int l = 0; l < EN_NLOOP; ++l)
        {
            rangeOk = rangeOk && g.loopPos[(size_t)l] >= 0 && g.loopPos[(size_t)l] < EN_LOOP_LEN[l];
            for (int i = 0; i < EN_LOOP_LEN[l]; ++i)
                notesSeen = notesSeen || g.loopHasNote[(size_t)l][(size_t)i];
            glowSeen = glowSeen || g.loopGlow[(size_t)l] > 0.2f;
        }
        if (lastPos >= 0 && g.loopPos[0] != lastPos)
            posMoved = true;
        lastPos = g.loopPos[0];
        for (int d = 0; d < EN_NDRUM; ++d)
            hitsSeen = hitsSeen || g.drumPat[(size_t)d] != 0;
        for (float v : g.levels)
        {
            rangeOk = rangeOk && v >= 0.0f && v <= 1.0f;
            meterSeen = meterSeen || v > 0.05f;
        }
    }
    char d[96];
    snprintf(d, sizeof d, "(first chord \"%s\", max drift %.2f in 90 s)", first, maxDrift);
    report("live: playing, with a named 3- or 4-note chord, the whole time", playing && nameOk && chordOk, d);
    report("loops hold notes, playheads move, positions stay inside their loops", notesSeen && posMoved && rangeOk);
    report("bells glow, meters move, the pulse has hits, drift advances", glowSeen && meterSeen && hitsSeen && maxDrift > 0.02f);

    c.freeze = true;
    core.apply(c);
    core.render(L.data(), R.data(), 480, 1.0f);
    const int lvl[4] = {c.par[P_PAD], c.par[P_BELLS], c.par[P_BASS], c.par[P_PULSE]};
    const GardenState f0 = GardenState::capture(true, c.par[P_ROOT], c.par[P_MODE], lvl);
    for (int i = 0; i < 1000; ++i)
    {
        core.apply(c);
        core.render(L.data(), R.data(), 480, 1.0f);
    }
    const GardenState f1 = GardenState::capture(true, c.par[P_ROOT], c.par[P_MODE], lvl);
    report("frozen: the snapshot says so and the chord holds for 10 s", f0.frozen && f1.frozen && f0.chord == f1.chord &&
           std::string(f0.chordName) == f1.chordName);
    c.freeze = false;
    c.play = false;
    core.apply(c);
    core.render(L.data(), R.data(), 480, 1.0f);
    report("resting: the snapshot says not playing", !GardenState::capture(true, 0, 0, lvl).playing);
}

// A re-prepare (sample-rate change, audio stop/start) must start from what the controls say, not from what the
// engine's file-static flags held from before.
static void reprepareTests()
{
    printf("re-prepare starts from the controls\n");
    std::vector<float> L(480), R(480);
    auto settle = [&](AmbCore& core, const AmbControls& c) {
        for (int i = 0; i < 20; ++i)
        {
            core.apply(c);
            core.render(L.data(), R.data(), 480, 1.0f);
        }
    };
    AmbCore core;
    core.acquire();
    AmbControls c = controlsFor(0);
    core.prepare(48000.0, c);
    c.freeze = true;
    settle(core, c);
    const bool wasFrozen = en_frozen == 1;
    c.freeze = false;
    core.prepare(48000.0, c);
    settle(core, c);
    report("frozen, then re-prepared with Freeze off: not frozen", wasFrozen && en_frozen == 0);

    c.freeze = true;
    core.prepare(44100.0, c);
    settle(core, c);
    report("re-prepared with Freeze on: frozen", en_frozen == 1);

    c.freeze = false;
    c.play = false;
    core.prepare(48000.0, c);
    settle(core, c);
    report("playing, then re-prepared with Play off: resting", en_playing == 0);

    c.play = true;
    core.prepare(48000.0, c);
    settle(core, c);
    report("re-prepared with Play on: playing, not frozen", en_playing == 1 && en_frozen == 0);
}


// A scale of your own (v0.6): the rules, what the engine does with it, and that nothing changes without one.
// "In the pool" means: every pitch class of the chord the pad plays (en_chord, MIDI notes) is in the absolute scale.
static bool chordInPool(uint32_t absPool)
{
    for (int k = 0; k < EN_NPAD; ++k)
        if (!(absPool >> (en_chord[k] % 12) & 1u))
            return false;
    return (absPool >> (en_chord_root % 12) & 1u) != 0u;
}

// Run `seconds`, looking at the chord every 20 ms; count the looks and how many had a note outside `absPool`.
static int gChordChanges = 0;   // how many times the chord changed during the last watchChord()
static void watchChord(AmbCore& core, const AmbControls& c, double seconds, uint32_t absPool, int& looks, int& outside)
{
    std::vector<float> L(960), R(960);
    looks = outside = 0;
    gChordChanges = 0;
    uint64_t prev = 0;
    for (double t = 0.0; t < seconds; t += 0.02)
    {
        core.apply(c);
        core.render(L.data(), R.data(), 960, 1.0f);   // 20 ms at 48 kHz
        ++looks;
        if (!chordInPool(absPool))
            ++outside;
        uint64_t now = en_chord_root;
        for (int k = 0; k < EN_NPAD; ++k)
            now = now * 131u + en_chord[k];
        if (looks > 1 && now != prev)
            ++gChordChanges;
        prev = now;
    }
}

static void scaleTests()
{
    printf("a scale of your own\n");
    using namespace amb1scale;
    char t[160];
    {
        const uint32_t ionian = modePool(MODE_IONIAN);
        uint32_t out = 0;
        report("the pool rules: valid needs the root, 12 bits at most, 3 notes",
               valid(0x091u) && !valid(0u) && !valid(0x003u) && !valid(0x002u | 0x004u | 0x008u) && !valid(0x1091u) && valid(ionian));
        report("toggling the root is refused", toggle(ionian, 0, 0, out) == Toggle::RootStays &&
               toggle(ionian, 2, 2, out) == Toggle::RootStays);
        report("toggling a note in adds it (C ionian + C#)", toggle(ionian, 0, 1, out) == Toggle::Changed && out == (ionian | 2u));
        report("the note is counted from the root (root D: E is 2 above)", toggle(0x091u, 2, 4, out) == Toggle::Changed && out == (0x091u | 4u));
        report("toggling a note out takes it out (C ionian - B)", toggle(ionian, 0, 11, out) == Toggle::Changed && out == (ionian & ~(1u << 11)));
        report("fewer than 3 notes is refused", toggle(0x091u, 0, 4, out) == Toggle::TooFew && toggle(0x091u, 0, 7, out) == Toggle::TooFew);
        report("a custom scale equal to the Mode's is not custom; a valid other one is; an invalid one is ignored",
               !isCustom(ionian, MODE_IONIAN) && isCustom(0x091u, MODE_IONIAN) && !isCustom(0x002u, MODE_IONIAN) &&
               effective(0x002u, MODE_IONIAN) == ionian && effective(0u, MODE_DORIAN) == modePool(MODE_DORIAN) &&
               effective(0x091u, MODE_IONIAN) == 0x091u);
        report("turned to a root: C ionian is 0xAB5, D with the same shape is 0xAD6", toAbsolute(ionian, 0) == 0xAB5u &&
               toAbsolute(ionian, 2) == 0xAD6u);
    }

    // The engine plays your scale: a lopsided 5-note one (root, minor 2nd, major 3rd, tritone, major 6th).
    const uint32_t mine = 1u | 1u << 1 | 1u << 4 | 1u << 6 | 1u << 9;
    AmbCore core;
    core.acquire();
    AmbControls c = controlsFor(0);
    const int root = c.par[(size_t)P_ROOT], mode = c.par[(size_t)P_MODE];
    const uint32_t absMine = toAbsolute(mine, root), absMode = toAbsolute(modePool(mode), root);
    int looks = 0, outside = 0;

    core.prepare(48000.0, c);   // no scale of your own: the Mode's
    watchChord(core, c, 20.0, absMode, looks, outside);
    snprintf(t, sizeof t, "(%d looks, %d outside)", looks, outside);
    report("no scale of your own: every chord note is in the Mode's scale", outside == 0, t);
    watchChord(core, c, 20.0, absMine, looks, outside);
    snprintf(t, sizeof t, "(%d of %d looks outside the 5-note scale: the test can tell)", outside, looks);
    report("control: with the Mode's scale, chord notes do fall outside the 5-note scale", outside > 0, t);

    c.scale = mine;   // changed while playing
    core.apply(c);
    std::vector<float> L(960), R(960);
    for (int i = 0; i < 100; ++i)   // 2 s to settle (the pad moves to the new chord)
    {
        core.apply(c);
        core.render(L.data(), R.data(), 960, 1.0f);
    }
    watchChord(core, c, 180.0, absMine, looks, outside);
    snprintf(t, sizeof t, "(%d looks, %d outside, the chord changed %d times)", looks, outside, gChordChanges);
    report("your scale set while playing: every chord note is in it for 180 s, through chord changes",
           outside == 0 && gChordChanges >= 3, t);

    core.prepare(44100.0, c);   // a re-prepare (sample-rate change, audio stop/start) keeps it
    watchChord(core, c, 20.0, absMine, looks, outside);
    snprintf(t, sizeof t, "(%d looks, %d outside)", looks, outside);
    report("after a re-prepare with your scale: every chord note is in it", outside == 0, t);

    c.scale = 0;   // back to the Mode's
    for (int i = 0; i < 100; ++i)
    {
        core.apply(c);
        core.render(L.data(), R.data(), 960, 1.0f);
    }
    watchChord(core, c, 30.0, absMode, looks, outside);
    snprintf(t, sizeof t, "(%d looks, %d outside)", looks, outside);
    report("scale cleared: back to the Mode's scale", outside == 0, t);

    c.scale = 0x002u;   // not a scale (no root, 1 note): ignored, the Mode's scale stays
    core.prepare(48000.0, c);
    watchChord(core, c, 20.0, absMode, looks, outside);
    snprintf(t, sizeof t, "(%d looks, %d outside)", looks, outside);
    report("an invalid scale is ignored (the Mode's scale sounds)", outside == 0, t);

    // The Garden's ring follows it.
    {
        const int lv[4] = {100, 100, 100, 100};
        const GardenState a = GardenState::capture(false, root, mode, lv);
        const GardenState b = GardenState::capture(false, root, mode, lv, mine);
        const GardenState d = GardenState::capture(false, root, mode, lv, modePool(mode));
        report("garden state: your scale is the ring's scale and says custom", b.custom && b.pool == absMine && !a.custom &&
               a.pool == absMode && !d.custom && d.pool == absMode);
    }
}

int main()
{
    resamplerTests();
    coreTests();
    gardenTests();
    reprepareTests();
    scaleTests();
    printf(gOk ? "PASS\n" : "FAIL\n");
    return gOk ? 0 : 1;
}
