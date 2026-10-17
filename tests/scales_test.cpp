// SPDX-License-Identifier: GPL-3.0-only
// The Scale menu's scales (v0.7), engine side, no JUCE:
//  (1) the library table matches the 28 scales of the Logic Scripter file (baroque_quartet_generator_v4.js, SCALES):
//      its eight Modes are the engine's own, its other twenty are the library's entries 1..20, every pool is valid;
//  (2) the engine plays each of the 20 scales (3..12 notes) for 120 s at a root: finite audio, not silent, and every note
//      it plays is in the scale: the pad's chord (looked at every 20 ms) and every pitched MIDI note-on it sends out
//      (pad, bells, bass; captured through the engine's own MIDI-out hook, so bells and bass are checked too);
//  (3) changing the scale while playing, 21 scales in a jumpy order and then on every 20 ms block, keeps everything in the
//      new scale, and so does clearing it again;
//  (4) a control: judged against the wrong scale the same check fails, so it can fail.
// Not testable here: how any of it sounds.   ./scales_test
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>
#include "AmbCore.h"
#include "Scale.h"

static bool gOk = true;
static void report(const char* what, bool ok, const char* detail = "")
{
    printf("  %-66s %s %s\n", what, ok ? "ok" : "FAIL", detail);
    gOk = gOk && ok;
}

// ---- the engine's pitched notes, from its MIDI-out hook (status 0x90/0x91/0x92 = pad/bells/bass note-on) ----
static uint32_t gAbsPool = 0;   // the scale every note must be in (absolute pitch classes, bit 0 = C)
static long gOn[3] = {0, 0, 0}, gOut[3] = {0, 0, 0};
extern "C" void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    if ((st & 0xF0u) != 0x90u || d2 == 0 || (st & 0x0Fu) > 2u)
        return;   // not a note-on, or the unpitched drums (channel 10)
    const uint32_t ch = st & 0x0Fu;
    ++gOn[ch];
    if (!(gAbsPool >> (d1 % 12) & 1u))
        ++gOut[ch];
}
static void resetNotes() { gOn[0] = gOn[1] = gOn[2] = gOut[0] = gOut[1] = gOut[2] = 0; }

// ---- the Scripter file's scales, as written there (SCALES and SCALE_LABELS) ----
struct Scripter { const char* name; std::vector<int> notes; };
static const std::vector<Scripter> kScripter = {
    {"Major (Ionian)", {0,2,4,5,7,9,11}}, {"Dorian", {0,2,3,5,7,9,10}}, {"Phrygian", {0,1,3,5,7,8,10}},
    {"Lydian", {0,2,4,6,7,9,11}}, {"Mixolydian", {0,2,4,5,7,9,10}}, {"Aeolian", {0,2,3,5,7,8,10}},
    {"Locrian", {0,1,3,5,6,8,10}}, {"Harmonic Minor", {0,2,3,5,7,8,11}}, {"Melodic Minor", {0,2,3,5,7,9,11}},
    {"Harmonic Major", {0,2,4,5,7,8,11}}, {"Hungarian Minor", {0,2,3,6,7,8,11}}, {"Phrygian Dominant", {0,1,4,5,7,8,10}},
    {"Double Harmonic", {0,1,4,5,7,8,11}}, {"Neapolitan Minor", {0,1,3,5,7,8,11}}, {"Neapolitan Major", {0,1,3,5,7,9,11}},
    {"Enigmatic", {0,1,4,6,8,10,11}}, {"Major Pentatonic", {0,2,4,7,9}}, {"Minor Pentatonic", {0,3,5,7,10}},
    {"Blues", {0,3,5,6,7,10}}, {"Hirajoshi", {0,2,3,7,8}}, {"In Sen", {0,1,5,7,10}}, {"Kumoi", {0,2,3,7,9}},
    {"Iwato", {0,1,5,6,10}}, {"Whole Tone", {0,2,4,6,8,10}}, {"Octatonic WH", {0,2,3,5,6,8,9,11}},
    {"Octatonic HW", {0,1,3,4,6,7,9,10}}, {"Prometheus", {0,2,4,6,9,10}}, {"Chromatic", {0,1,2,3,4,5,6,7,8,9,10,11}}};
static uint32_t maskOf(const std::vector<int>& notes)
{
    uint32_t m = 0;
    for (int n : notes)
        m |= 1u << n;
    return m;
}

static void tableTests()
{
    printf("the library table\n");
    using namespace amb1scale;
    report("the library has 21 entries (Mode's own + 20), and 22 choices with Custom",
           kLibraryCount == 21 && kChoiceCount == 22 && kCustomChoice == 21 && std::string(choiceName(0)) == "Mode's own" &&
           std::string(choiceName(21)) == "Custom" && libraryPool(0) == 0u && libraryPool(21) == 0u && libraryPool(-1) == 0u);

    // the Scripter's eight Modes are the engine's, in this order
    static const int modeAt[8] = {0, 1, 2, 3, 4, 5, 16, 17};
    bool modesSame = (int)EN_NMODES == 8;
    for (int m = 0; m < 8 && modesSame; ++m)
        modesSame = maskOf(kScripter[(size_t)modeAt[m]].notes) == (uint32_t)EN_MODE_POOL[m];
    report("the Scripter's eight Modes are exactly the engine's EN_MODE_POOL", modesSame);

    // its other twenty are the library's entries 1..20, in this order
    static const int libAt[20] = {6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27};
    bool same = true;
    for (int i = 1; i < kLibraryCount; ++i)
        same = same && libraryPool(i) == maskOf(kScripter[(size_t)libAt[i - 1]].notes);
    report("the library's 20 scales are exactly the Scripter's other 20 (masks)", same);

    bool valids = true, distinct = true, notMode = true, short13 = true, round = true;
    std::set<uint32_t> seen;
    for (int i = 1; i < kLibraryCount; ++i)
    {
        const uint32_t p = libraryPool(i);
        valids = valids && valid(p);
        distinct = distinct && seen.insert(p).second;
        for (int m = 0; m < (int)EN_NMODES; ++m)
            notMode = notMode && p != (uint32_t)EN_MODE_POOL[m];
        short13 = short13 && std::string(libraryTable()[i].name).size() <= 13;
        round = round && libraryFind(p) == i;
    }
    report("every library scale is valid for the engine (root, 3..12 notes), distinct, and not one of the Modes",
           valids && distinct && notMode);
    report("names are at most 13 characters; libraryFind finds each scale; a Mode's pool is not found",
           short13 && round && libraryFind((uint32_t)EN_MODE_POOL[0]) == 0 && libraryFind(0x002u) == 0);
    report("sizes: 4 five-note, 3 six-note, 10 seven-note, 2 eight-note, 1 twelve-note", [] {
        int n[13] = {};
        for (int i = 1; i < kLibraryCount; ++i)
            ++n[count(libraryPool(i))];
        return n[5] == 4 && n[6] == 3 && n[7] == 10 && n[8] == 2 && n[12] == 1;
    }());
    report("basePool: the menu's scale if one is chosen, else the Mode's; isCustom follows it",
           basePool(MODE_IONIAN, 0) == (uint32_t)EN_MODE_POOL[MODE_IONIAN] && basePool(MODE_IONIAN, 1) == libraryPool(1) &&
           !isCustom(libraryPool(1), MODE_IONIAN, 1) && isCustom(libraryPool(1), MODE_IONIAN, 0) &&
           isCustom(libraryPool(1) | 4u, MODE_IONIAN, 1));   // (bit 2 is not in Locrian)
}

// ---- the engine ----
static AmbControls garden(int root, int drift)
{
    AmbControls c;
    int16_t par[P_NPARAMS];
    uint16_t pool;
    en_piece(0, par, &pool);   // the Garden's settings
    for (int i = 0; i < P_NPARAMS; ++i)
        c.par[(size_t)i] = par[i];
    c.par[(size_t)P_ROOT] = root;
    c.par[(size_t)P_DRIFT] = drift;   // 0 = a chord change every 14 to 26 s
    return c;
}

struct Result
{
    long nonfinite = 0, looks = 0, chordOut = 0, changes = 0;
    double peak = 0.0, rms = 0.0;
};

// Run `seconds` at 48 kHz in 20 ms blocks with the engine's notes captured. `judge` is the scale (relative) every note is
// checked against; the controls' own scale is c.scale. Counts into gOn/gOut (reset by the caller).
static Result watch(AmbCore& core, const AmbControls& c, double seconds, uint32_t judgeAbs, bool rmsFrom10s = true)
{
    gAbsPool = judgeAbs;
    Result r;
    const int blk = 960;
    std::vector<float> L((size_t)blk), R((size_t)blk);
    double sum2 = 0.0;
    long n = 0;
    uint64_t prev = 0;
    for (double t = 0.0; t < seconds; t += 0.02)
    {
        core.apply(c);
        core.render(L.data(), R.data(), blk, 1.0f);
        for (int i = 0; i < blk; ++i)
            for (float v : {L[(size_t)i], R[(size_t)i]})
            {
                if (!std::isfinite(v))
                {
                    ++r.nonfinite;
                    continue;
                }
                r.peak = std::max(r.peak, (double)std::fabs(v));
                if (!rmsFrom10s || t > 10.0)
                {
                    sum2 += (double)v * v;
                    ++n;
                }
            }
        ++r.looks;
        bool in = (judgeAbs >> (en_chord_root % 12) & 1u) != 0u;
        uint64_t now = en_chord_root;
        for (int k = 0; k < EN_NPAD; ++k)
        {
            in = in && (judgeAbs >> (en_chord[k] % 12) & 1u);
            now = now * 131u + en_chord[k];
        }
        if (!in)
            ++r.chordOut;
        if (r.looks > 1 && now != prev)
            ++r.changes;
        prev = now;
    }
    r.rms = n ? std::sqrt(sum2 / (double)n) : 0.0;
    return r;
}

static void startEngine(AmbCore& core, const AmbControls& c)
{
    core.acquire();
    core.prepare(48000.0, c);
    en_set(P_MIDI, 1);   // let the engine report its notes to the hook above (the plugin itself keeps this off)
}

static void engineTests()
{
    printf("the engine plays each library scale (120 s each, Drift 0, root C or G, host 48 kHz)\n");
    using namespace amb1scale;
    char t[400];
    long totalOut = 0, totalOn = 0;
    bool allOk = true;
    for (int i = 1; i < kLibraryCount; ++i)
    {
        const int root = (i % 2) ? 0 : 7;
        AmbControls c = garden(root, 0);
        c.scale = libraryPool(i);   // the route the plugin takes: the processor hands AmbCore the menu's scale as c.scale
        AmbCore core;
        startEngine(core, c);
        resetNotes();
        const Result r = watch(core, c, 120.0, toAbsolute(libraryPool(i), root));
        const long out = gOut[0] + gOut[1] + gOut[2];
        const bool ok = r.nonfinite == 0 && r.peak < 1.0 && r.rms > 0.02 && r.chordOut == 0 && out == 0 && r.changes >= 2 &&
                        gOn[0] > 20 && gOn[1] > 20 && gOn[2] > 0;
        snprintf(t, sizeof t, "(%2d notes, root %2d: peak %.3f rms %.4f, %ld chord looks / %ld outside, %ld changes, notes %ld/%ld/%ld outside %ld)",
                 count(libraryPool(i)), root, r.peak, r.rms, r.looks, r.chordOut, r.changes, gOn[0], gOn[1], gOn[2], out);
        report(libraryTable()[i].name, ok, t);
        allOk = allOk && ok;
        totalOut += out + r.chordOut;
        totalOn += gOn[0] + gOn[1] + gOn[2];
    }
    snprintf(t, sizeof t, "(%ld notes played in all, %ld outside their scale)", totalOn, totalOut);
    report("all 20 scales: finite, audible, every chord note and every note in the scale", allOk && totalOut == 0, t);

    printf("control: judged against the wrong scale the check fails (so it can fail)\n");
    {
        AmbControls c = garden(0, 0);
        c.scale = libraryPool(20);   // Chromatic, judged against Ionian
        AmbCore core;
        startEngine(core, c);
        resetNotes();
        const Result r = watch(core, c, 120.0, toAbsolute((uint32_t)EN_MODE_POOL[MODE_IONIAN], 0));
        snprintf(t, sizeof t, "(%ld of %ld chord looks and %ld/%ld/%ld notes outside Ionian)", r.chordOut, r.looks, gOut[0], gOut[1], gOut[2]);
        report("Chromatic judged against Ionian: chord notes and notes fall outside", r.chordOut > 1000 && gOut[0] > 0 && gOut[1] > 0, t);
    }

    printf("changing the scale while playing\n");
    {
        AmbControls c = garden(0, 0);
        AmbCore core;
        startEngine(core, c);
        resetNotes();
        long chordOut = 0, looks = 0, nonfinite = 0, swOut = 0;
        double peak = 0.0;
        // 21 steps (Mode's own and the 20), jumpy order (stride 8 is coprime with 21), 20 s each
        int idx = 0;
        for (int n = 0; n < kLibraryCount; ++n, idx = (idx + 8) % kLibraryCount)
        {
            c.scale = libraryPool(idx);   // 0 = the Mode's own scale
            const uint32_t abs = toAbsolute(basePool((int)c.par[(size_t)P_MODE], idx), 0);
            // the pad may still hold a note from the old scale for a moment: look only from 1 s after the change
            core.apply(c);
            watch(core, c, 1.0, abs);   // (the notes of this first second are not judged below)
            resetNotes();
            const Result r = watch(core, c, 19.0, abs);
            chordOut += r.chordOut;
            looks += r.looks;
            nonfinite += r.nonfinite;
            peak = std::max(peak, r.peak);
            swOut += gOut[0] + gOut[1] + gOut[2];
        }
        snprintf(t, sizeof t, "(%ld chord looks, %ld outside; notes outside %ld; non-finite %ld; peak %.3f)", looks, chordOut, swOut, nonfinite, peak);
        report("21 scales in a jumpy order, 20 s each: all in the new scale", chordOut == 0 && swOut == 0 && nonfinite == 0 && peak < 1.0, t);

        // a burst: Chromatic <-> Iwato (12 <-> 5 notes) on every 20 ms block for 30 s: the degree can be past the end
        const uint32_t chrom = libraryPool(20), iwato = libraryPool(15);
        resetNotes();
        long bOut = 0, bLooks = 0, bNon = 0;
        gAbsPool = toAbsolute(chrom, 0);
        std::vector<float> L(960), R(960);
        for (int b = 0; b < 30 * 50; ++b)
        {
            c.scale = (b % 2) ? iwato : chrom;
            core.apply(c);
            gAbsPool = toAbsolute(c.scale, 0);
            core.render(L.data(), R.data(), 960, 1.0f);
            for (int i = 0; i < 960; ++i)
                bNon += !std::isfinite(L[(size_t)i]) + !std::isfinite(R[(size_t)i]);
            bool in = (gAbsPool >> (en_chord_root % 12) & 1u) != 0u;
            for (int k = 0; k < EN_NPAD; ++k)
                in = in && (gAbsPool >> (en_chord[k] % 12) & 1u);
            ++bLooks;
            bOut += !in;
        }
        const long notesOut = gOut[0] + gOut[1] + gOut[2];
        snprintf(t, sizeof t, "(%ld switches, %ld chord looks outside, %ld notes outside, non-finite %ld)", bLooks, bOut, notesOut, bNon);
        report("Chromatic <-> Iwato on every block for 30 s: nothing outside, nothing non-finite", bOut == 0 && notesOut == 0 && bNon == 0, t);

        // and back to the Mode's own scale
        c.scale = 0;
        const uint32_t modeAbs = toAbsolute((uint32_t)EN_MODE_POOL[c.par[(size_t)P_MODE]], 0);
        watch(core, c, 1.0, modeAbs);
        resetNotes();
        const Result back = watch(core, c, 59.0, modeAbs);
        snprintf(t, sizeof t, "(%ld chord looks, %ld outside, notes outside %ld)", back.looks, back.chordOut, gOut[0] + gOut[1] + gOut[2]);
        report("cleared (c.scale = 0): back inside the Mode's scale", back.chordOut == 0 && gOut[0] + gOut[1] + gOut[2] == 0, t);
    }
}

int main()
{
    tableTests();
    engineTests();
    printf(gOk ? "PASS\n" : "FAIL\n");
    return gOk ? 0 : 1;
}
