// SPDX-License-Identifier: GPL-3.0-only
// pulse_test: the Pulse sound switch (Drums / Melodic). No JUCE. Every case runs in a fresh child process (the engine's
// random generator is file-static), prints one line of facts, and the parent compares them.
//   ./pulse_test                                                       the checks (exit 0 = all passed)
//   ./pulse_test child <voice> <pct> <seed> <piece> <secs> <layers> <scale> <thump> <hat> <open> <chance>
//   voice: 0 drums, 1 melodic, 2 switching between the two every 5 s
//   layers: 0 the piece as it is; 1 bass line + pulse loop; 2 pulse loop only; 3 pulse muted; 4 bass line only
//   scale: 0 the Mode's own, 1..20 the Scale menu's entries
#include <chrono>
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
#include "Scale.h"

namespace
{
// what the hook sees while a case runs
uint64_t gPulse = 1469598103934665603ull, gPulseNotes = 1469598103934665603ull, gOther = 1469598103934665603ull;
long gPulseOn = 0, gOtherOn = 0, gMelOn = 0, gMelOff = 0, gBadChord = 0, gBadScale = 0, gBadRange = 0;
uint32_t gScaleAbs = 0;   // the scale that sounds, as absolute pitch classes (bit 0 = C)

int drumOf(uint32_t s, uint32_t note)
{
    if (s == 0x99)
        return note == 36 ? 0 : note == 42 ? 1 : 2;
    return note < 60 ? 0 : note < 72 ? 1 : 2;   // the melodic registers: C3.., C4.., C5..
}
}

extern "C" void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    const uint32_t s = st & 0xFF;
    const bool on = d2 > 0;
    if ((s == 0x99 || s == 0x93) && on)   // the pulse: a drum hit (channel 10) or a plucked note (channel 4)
    {
        ++gPulseOn;
        gPulse = (gPulse ^ ((uint64_t)en_bars << 8 | (uint64_t)drumOf(s, d1))) * 1099511628211ull;
        gPulseNotes = (gPulseNotes ^ ((uint64_t)en_bars << 8 | d1)) * 1099511628211ull;
        if (s == 0x93)
        {
            ++gMelOn;
            bool inChord = false;
            for (int i = 0; i < EN_NPAD; ++i)
                inChord |= (int)(en_chord[i] % 12) == (int)(d1 % 12);
            if (!inChord)
                ++gBadChord;
            if (!(gScaleAbs >> (d1 % 12) & 1u))
                ++gBadScale;
            if (d1 < 48 || d1 > 83 || d2 < 1 || d2 > 127)
                ++gBadRange;
        }
    }
    else if (s == 0x83 || (s == 0x93 && !on))
    {
        ++gMelOff;
    }
    else if ((s == 0x90 || s == 0x91 || s == 0x92) && on)   // pad, bells, bass
    {
        ++gOtherOn;
        gOther = (gOther ^ ((uint64_t)en_bars << 8 | (s & 3u))) * 1099511628211ull;
        gOther = (gOther ^ d1) * 1099511628211ull;
    }
}

static int child(int voice, int pct, uint32_t seed, int piece, double secs, int layers, int scaleChoice, int t, int h, int o, int chance)
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
    if (layers == 1) { c.par[(size_t)P_PAD] = 0; c.par[(size_t)P_BELLS] = 0; c.par[(size_t)P_BASS] = 60; c.par[(size_t)P_PULSE] = 60; c.par[(size_t)P_HITS] = 100; }
    if (layers == 2) { c.par[(size_t)P_PAD] = 0; c.par[(size_t)P_BELLS] = 0; c.par[(size_t)P_BASS] = 0; c.par[(size_t)P_PULSE] = 60; c.par[(size_t)P_HITS] = 100; }
    if (layers == 3) c.par[(size_t)P_PULSE] = 0;
    if (layers == 4) { c.par[(size_t)P_PAD] = 0; c.par[(size_t)P_BELLS] = 0; c.par[(size_t)P_BASS] = 60; c.par[(size_t)P_PULSE] = 0; }
    if (layers == 5) { c.par[(size_t)P_PULSE] = 100; c.par[(size_t)P_HITS] = 100; }   // the loudest case: Pulse and Hits at the top
    c.scale = amb1scale::libraryPool(scaleChoice);
    c.drum = {{t, h, o}};
    c.palette = pct;
    c.pulseVoice = voice == 1 ? 1 : 0;
    c.pulseChance = chance;
    c.seed = seed;
    gScaleAbs = amb1scale::toAbsolute(amb1scale::effective(c.scale, c.par[(size_t)P_MODE]), c.par[(size_t)P_ROOT]);
    core.prepare(44100.0, c);
    en_set(P_MIDI, 1);
    std::vector<float> l(441), r(441);
    uint64_t hsh = 1469598103934665603ull;
    double peak = 0, sumsq = 0, diff = 0, prev = 0, maxJump = 0, wdiff = 0, wsq = 0;
    long nan = 0, n = 0, wn = 0;
    std::string windows;
    const long total = (long)(secs * 44100.0);
    const auto t0 = std::chrono::steady_clock::now();
    double renderSecs = 0.0;
    for (long done = 0; done < total; done += 441)
    {
        if (voice == 2)
            c.pulseVoice = (done / (5 * 44100)) % 2;
        core.apply(c);
        const auto a = std::chrono::steady_clock::now();
        core.render(l.data(), r.data(), 441, 1.0f);
        renderSecs += std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count();
        for (int i = 0; i < 441; ++i)
        {
            double x = l[(size_t)i];
            if (x != x) { ++nan; x = 0; }
            uint32_t u;
            const float f = (float)x;
            std::memcpy(&u, &f, 4);
            hsh = (hsh ^ u) * 1099511628211ull;
            peak = std::fmax(peak, std::fabs(x));
            sumsq += x * x;
            diff += std::fabs(x - prev);
            maxJump = std::fmax(maxJump, std::fabs(x - prev));
            wdiff += std::fabs(x - prev);
            wsq += x * x;
            prev = x;
            ++n;
            if (++wn == 20 * 44100)   // brightness (how much the wave moves for its level) in 20 s windows
            {
                const double rm = std::sqrt(wsq / (double)wn);
                char b[32];
                std::snprintf(b, sizeof b, "%s%.4f", windows.empty() ? "" : ",", rm > 0 ? wdiff / (double)wn / rm : 0.0);
                windows += b;
                wdiff = wsq = 0;
                wn = 0;
            }
        }
    }
    (void)t0;
    const double rms = std::sqrt(sumsq / (double)n);
    printf("audio %016llx nan %ld peak %.4f rms %.5f jump %.4f cpu %.3f windows %s pulse %016llx pulsenotes %016llx pulseon %ld other %016llx otheron %ld "
           "mel %ld melOff %ld badchord %ld badscale %ld badrange %ld\n",
           (unsigned long long)hsh, nan, peak, rms, maxJump, 100.0 * renderSecs / secs, windows.empty() ? "-" : windows.c_str(),
           (unsigned long long)gPulse, (unsigned long long)gPulseNotes, gPulseOn, (unsigned long long)gOther, gOtherOn, gMelOn, gMelOff,
           gBadChord, gBadScale, gBadRange);
    return 0;
}

using Facts = std::map<std::string, std::string>;
static Facts run(const char* self, int voice, int pct, uint32_t seed, int piece, double secs, int layers = 0, int scale = 0, int t = 100,
                 int h = 100, int o = 100, int chance = 100)
{
    const std::string cmd = std::string("\"") + self + "\" child " + std::to_string(voice) + " " + std::to_string(pct) + " " +
                            std::to_string(seed) + " " + std::to_string(piece) + " " + std::to_string(secs) + " " + std::to_string(layers) +
                            " " + std::to_string(scale) + " " + std::to_string(t) + " " + std::to_string(h) + " " + std::to_string(o) + " " + std::to_string(chance);
    Facts f;
    FILE* p = popen(cmd.c_str(), "r");
    if (!p)
        return f;
    char buf[1024] = {};
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
static double num(const Facts& f, const char* k) { return f.count(k) ? std::atof(f.at(k).c_str()) : -1.0; }
static long inum(const Facts& f, const char* k) { return f.count(k) ? std::atol(f.at(k).c_str()) : -1; }
static std::string str(const Facts& f, const char* k) { return f.count(k) ? f.at(k) : std::string("?"); }

static int fails = 0;
static void report(const std::string& what, bool ok, const std::string& detail = "")
{
    printf("  %-4s %s %s\n", ok ? "ok" : "FAIL", what.c_str(), detail.c_str());
    if (!ok)
        ++fails;
}
static std::string fmt(const char* f, double a, double b = 0, double c = 0)
{
    char s[160];
    std::snprintf(s, sizeof s, f, a, b, c);
    return s;
}
static std::vector<double> windowsOf(const Facts& f)
{
    std::vector<double> w;
    std::stringstream ss(str(f, "windows"));
    std::string t;
    while (std::getline(ss, t, ','))
        w.push_back(std::atof(t.c_str()));
    return w;
}

int main(int argc, char** argv)
{
    if (argc >= 2 && std::strcmp(argv[1], "child") == 0)   // (never fall through to the parent: that would spawn children forever)
    {
        if (argc != 13)
            return 2;
        return child(atoi(argv[2]), atoi(argv[3]), (uint32_t)atoi(argv[4]), atoi(argv[5]), atof(argv[6]), atoi(argv[7]), atoi(argv[8]),
                     atoi(argv[9]), atoi(argv[10]), atoi(argv[11]), atoi(argv[12]));
    }
    const char* self = argv[0];
    printf("pulse sound: fresh process per case\n");

    // 1. Drums is today's sound, bit for bit. These hashes of the left channel were taken from the v0.13.0 engine (40 s, seed 7, host 44.1 kHz).
    static const char* const kDrumsHash[EN_NPIECES] = {"dc5d794a735575ef", "715704d140c336ca", "d0212cfc87a88716", "bf6b16629338ca18",
                                                        "58a43f582a562c48", "2f93ce0c810d8d39", "2802a9af8a179f15", "6eb0094a5b29f692"};
    for (int piece = 0; piece < EN_NPIECES; ++piece)
    {
        const Facts f = run(self, 0, 0, 7, piece, 40);
        report("piece " + std::to_string(piece) + " in Drums: the audio is exactly the v0.13.0 audio", str(f, "audio") == kDrumsHash[piece],
               "(" + str(f, "audio") + ")");
    }
    {
        static const struct { int piece; const char* hash; } loud[3] = {{0, "d05d51870071e677"}, {4, "99a16f48bb34c616"}, {7, "07282ab88652d714"}};
        for (const auto& l : loud)
        {
            const Facts f = run(self, 0, 0, 7, l.piece, 40, 5);   // Pulse and Hits at 100: the most drums there are
            report("piece " + std::to_string(l.piece) + " in Drums, Pulse and Hits at 100: exactly the v0.13.0 audio", str(f, "audio") == l.hash,
                   "(" + str(f, "audio") + ")");
        }
    }

    // 2. Everything but the pulse is the same in both modes.
    for (int piece : {0, 3, 4, 7})
    {
        const Facts d = run(self, 0, 0, 5, piece, 60, 3), m = run(self, 1, 0, 5, piece, 60, 3);
        report("piece " + std::to_string(piece) + ": with the pulse muted, Drums and Melodic are bit-identical",
               !d.empty() && str(d, "audio") == str(m, "audio"));
    }
    for (int piece : {0, 4, 7})
    {
        const Facts d = run(self, 0, 0, 5, piece, 90), m = run(self, 1, 0, 5, piece, 90);
        report("piece " + std::to_string(piece) + ": pad, bells and bass play the same notes in both modes",
               !d.empty() && str(d, "other") == str(m, "other") && inum(d, "otheron") == inum(m, "otheron") && inum(d, "otheron") > 20,
               "(" + str(d, "otheron") + " notes)");
        report("piece " + std::to_string(piece) + ": Melodic hits on the same beats as the drums (same bars, same pattern)",
               str(d, "pulse") == str(m, "pulse") && inum(d, "pulseon") == inum(m, "pulseon") && inum(d, "pulseon") > 10,
               "(" + str(d, "pulseon") + " hits)");
        report("piece " + std::to_string(piece) + ": Drums plays no pluck, Melodic plays no drum note", inum(d, "mel") == 0 && inum(m, "mel") == inum(m, "pulseon"));
    }

    // 3. Every plucked note is a tone of the sounding chord and of the scale, in range.
    long notes = 0;
    bool allOk = true;
    for (int piece = 0; piece < EN_NPIECES; ++piece)
    {
        const Facts m = run(self, 1, piece % 2 ? 100 : 0, 11, piece, 120, 5);
        const bool ok = !m.empty() && inum(m, "badchord") == 0 && inum(m, "badscale") == 0 && inum(m, "badrange") == 0 && inum(m, "mel") > 20;
        notes += inum(m, "mel");
        allOk &= ok;
        report("piece " + std::to_string(piece) + ": every plucked note is in the chord, in the scale, C3..B5, velocity 1..127", ok,
               "(" + str(m, "mel") + " notes)");
    }
    for (int sc = 1; sc < amb1scale::kLibraryCount; ++sc)
    {
        const Facts m = run(self, 1, 50, 20 + (uint32_t)sc, 4, 60, 5, sc);
        const bool ok = !m.empty() && inum(m, "badchord") == 0 && inum(m, "badscale") == 0 && inum(m, "badrange") == 0 && inum(m, "mel") > 10;
        notes += inum(m, "mel");
        allOk &= ok;
        if (!ok)
            report(std::string("scale ") + amb1scale::choiceName(sc) + ": every plucked note is in the chord and the scale", false,
                   "(chord " + str(m, "badchord") + ", scale " + str(m, "badscale") + ", range " + str(m, "badrange") + ", notes " + str(m, "mel") + ")");
    }
    report("all 20 menu scales, 60 s each: every plucked note is in the chord and the scale", allOk, "(" + std::to_string(notes) + " notes in all)");

    // 4. Level: with a bass line and a drum loop, not only on noise.
    for (int piece : {0, 4, 7})
    {
        const Facts bd = run(self, 0, 0, 5, piece, 90, 1), bm = run(self, 1, 0, 5, piece, 90, 1);
        const Facts pd = run(self, 0, 0, 5, piece, 90, 2), pm = run(self, 1, 0, 5, piece, 90, 2);
        const Facts bo = run(self, 0, 0, 5, piece, 90, 4);
        const double dbMix = 20.0 * std::log10(num(bm, "rms") / num(bd, "rms")), dbPulse = 20.0 * std::log10(num(pm, "rms") / num(pd, "rms"));
        char line[200];
        std::snprintf(line, sizeof line, "(%+.2f dB; rms: bass alone %.4f, with drums %.4f, with plucks %.4f)", dbMix, num(bo, "rms"), num(bd, "rms"),
                      num(bm, "rms"));
        report("piece " + std::to_string(piece) + ": bass line + pulse loop: Melodic within 2 dB of Drums", std::fabs(dbMix) < 2.0, line);
        report("piece " + std::to_string(piece) + ": pulse loop alone: Melodic within 3 dB of Drums", std::fabs(dbPulse) < 3.0,
               fmt("(%+.2f dB; rms %.4f against %.4f)", dbPulse, num(pm, "rms"), num(pd, "rms")));
    }

    // 5. No clipping, no non-finite samples, no clicks.
    for (int piece = 0; piece < EN_NPIECES; ++piece)
    {
        const Facts m = run(self, 1, 100, 9, piece, 90, 5);
        report("piece " + std::to_string(piece) + ": Melodic at Palette 100, Pulse and Hits 100: finite, below clipping",
               !m.empty() && inum(m, "nan") == 0 && num(m, "peak") < 0.95, "(peak " + str(m, "peak") + ")");
    }
    {
        const Facts m = run(self, 1, 100, 9, 4, 90, 5, 0, 150, 400, 400);
        report("Melodic with the three levels at 150 / 400 / 400: still below clipping (a pluck's level stops at 150 %)",
               !m.empty() && num(m, "peak") < 0.95, "(peak " + str(m, "peak") + ")");
    }
    {
        const Facts m = run(self, 1, 0, 3, 4, 60, 2), d = run(self, 0, 0, 3, 4, 60, 2);
        report("the pulse loop alone: no step in the wave bigger than the drums' own (no clicks at note starts)",
               !m.empty() && num(m, "jump") <= num(d, "jump"), "(largest sample-to-sample step " + str(m, "jump") + " against " + str(d, "jump") + ")");
    }
    {
        const Facts s = run(self, 2, 100, 9, 4, 120, 5);
        report("switching Drums <-> Melodic every 5 s for 2 minutes: finite, below clipping, no note left on",
               !s.empty() && inum(s, "nan") == 0 && num(s, "peak") < 0.95 && inum(s, "melOff") >= inum(s, "mel") - 12,
               "(peak " + str(s, "peak") + ", " + str(s, "mel") + " notes on, " + str(s, "melOff") + " off)");
    }
    {
        const Facts m = run(self, 1, 0, 3, 4, 120, 5);
        report("Melodic: note-offs follow the note-ons (at most 12 still sounding at the end)",
               inum(m, "melOff") <= inum(m, "mel") && inum(m, "melOff") >= inum(m, "mel") - 12, "(" + str(m, "mel") + " on, " + str(m, "melOff") + " off)");
    }

    // 5b. Pulse chance: the share of hits that play. Melodic only.
    {
        const Facts full = run(self, 1, 0, 6, 4, 120, 5), half = run(self, 1, 0, 6, 4, 120, 5, 0, 100, 100, 100, 50),
                    none = run(self, 1, 0, 6, 4, 120, 5, 0, 100, 100, 100, 0), dFull = run(self, 0, 0, 6, 4, 60, 5),
                    dNone = run(self, 0, 0, 6, 4, 60, 5, 0, 100, 100, 100, 0);
        const double share = (double)inum(half, "mel") / (double)inum(full, "mel");
        report("Pulse chance 100 plays every hit (the default)", inum(full, "mel") > 100 && inum(full, "mel") == inum(full, "pulseon"));
        report("Pulse chance 50 plays about half of the hits", share > 0.4 && share < 0.6, fmt("(%.0f %% of %.0f)", 100.0 * share, (double)inum(full, "mel")));
        report("Pulse chance 0 plays no pluck, and the rest of the music is untouched",
               inum(none, "mel") == 0 && str(none, "other") == str(full, "other"));
        report("at chance 50 every plucked note is still in the chord and the scale, in range",
               inum(half, "badchord") == 0 && inum(half, "badscale") == 0 && inum(half, "badrange") == 0);
        report("Drums ignores Pulse chance (bit-identical at 0 and 100)", str(dFull, "audio") == str(dNone, "audio"));
    }

    // 6. The morph: at Palette 100 the pluck drifts; the notes and the rhythm do not.
    {
        const Facts p0 = run(self, 1, 0, 202, 4, 180, 2), p100 = run(self, 1, 100, 202, 4, 180, 2), p100b = run(self, 1, 100, 303, 4, 180, 2);
        report("Palette 100 plays the same plucked notes as Palette 0 (same seed)",
               str(p0, "pulsenotes") == str(p100, "pulsenotes") && inum(p0, "mel") == inum(p100, "mel") && inum(p0, "mel") > 30,
               "(" + str(p0, "mel") + " notes)");
        report("Palette 100 sounds different from Palette 0", str(p0, "audio") != str(p100, "audio"));
        report("another seed gives another morph", str(p100, "audio") != str(p100b, "audio"));
        const auto w0 = windowsOf(p0), w100 = windowsOf(p100);
        double lo0 = 1e9, hi0 = 0, lo1 = 1e9, hi1 = 0;
        for (double x : w0) { lo0 = std::fmin(lo0, x); hi0 = std::fmax(hi0, x); }
        for (double x : w100) { lo1 = std::fmin(lo1, x); hi1 = std::fmax(hi1, x); }
        printf("  brightness of the plucks in 20 s windows (waveform movement / level): Palette 0 %.3f..%.3f, Palette 100 %.3f..%.3f\n", lo0,
               hi0, lo1, hi1);
        report("the pluck's tone moves over 3 minutes at Palette 100 more than at Palette 0", !w0.empty() && !w100.empty() && (hi1 / lo1) > (hi0 / lo0) * 1.1,
               fmt("(spread %.2fx against %.2fx)", hi1 / lo1, hi0 / lo0));
    }

    // 7. CPU (this machine; one core). The whole garden at Pulse and Hits 100.
    {
        const Facts d = run(self, 0, 100, 9, 4, 60, 5), m = run(self, 1, 100, 9, 4, 60, 5);
        report("CPU: the whole garden with plucks stays under 3 % of one core", num(m, "cpu") < 3.0,
               fmt("(Melodic %.2f %%, Drums %.2f %%)", num(m, "cpu"), num(d, "cpu")));
    }
    printf(fails ? "pulse_test: FAIL\n" : "pulse_test: PASS\n");
    return fails ? 1 : 0;
}
