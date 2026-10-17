// SPDX-License-Identifier: GPL-3.0-only
// The MIDI effect build's engine side, headless and JUCE-free: AmbCore::advance() (the engine runs with no audio out)
// and the notes it hands to en_midi_out() (collected by MidiFxHooks.cpp). Exit 0 = every check passed.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <utility>
#include <vector>
#include "AmbCore.h"
#include "MidiFxHooks.h"

static int failures = 0;
static void check(bool ok, const char* what)
{
    printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++failures;
}

struct Run
{
    long chunks = 0;
    long hostFrames = 0;
    bool offsetsOk = true;        // every chunk offset inside its block, never going backwards within a block
    long noteOns[16] = {};        // by channel
    long noteOffs[16] = {};       // 0x80 note-offs and note-ons with velocity 0, by channel
    long ccAllOff = 0;            // controller 123 (all notes off)
    bool dataOk = true;           // note numbers and velocities 0..127
    int dropped = 0;
    std::map<std::pair<int, int>, int> open;   // (channel, note) -> times switched on and not yet off
    int maxOpen = 0;
};

struct Ctx
{
    Run* run;
    int lastOffset;
    int n;
};

static void onChunk(void* p, int offset)
{
    auto* c = static_cast<Ctx*>(p);
    Run& r = *c->run;
    ++r.chunks;
    if (offset < 0 || offset >= c->n || offset < c->lastOffset)
        r.offsetsOk = false;
    c->lastOffset = offset;
    for (int i = 0; i < amb1fx::count(); ++i)
    {
        const amb1fx::Event& e = amb1fx::at(i);
        const int ch = e.status & 0x0F, kind = e.status & 0xF0;
        if (kind == 0x90 && e.data2 > 0)
        {
            ++r.noteOns[ch];
            r.maxOpen = std::max(r.maxOpen, ++r.open[{ch, e.data1}]);
        }
        else if (kind == 0x80 || kind == 0x90)
        {
            ++r.noteOffs[ch];
            int& o = r.open[{ch, e.data1}];
            if (o > 0)
                --o;
        }
        else if (kind == 0xB0 && e.data1 == 123)
        {
            ++r.ccAllOff;
            for (auto& kv : r.open)
                if (kv.first.first == ch)
                    kv.second = 0;
        }
        if (e.data1 > 127 || e.data2 > 127)
            r.dataOk = false;
    }
    r.dropped += amb1fx::dropped();
    amb1fx::clear();
}

static Run play(AmbCore& core, const AmbControls& c, double hostRate, int block, double seconds)
{
    Run r;
    Ctx ctx{&r, 0, block};
    const long total = (long)(seconds * hostRate);
    long done = 0;
    while (done < total)
    {
        const int n = (int)std::min<long>(block, total - done);
        core.apply(c);
        ctx.n = n;
        ctx.lastOffset = 0;
        core.advance(n, onChunk, &ctx);
        done += n;
    }
    r.hostFrames = done;
    return r;
}

static long sum(const long (&a)[16])
{
    long s = 0;
    for (long v : a)
        s += v;
    return s;
}

int main()
{
    AmbCore core;
    core.acquire();

    printf("MIDI out off (the instrument build's setting): the engine sends no notes\n");
    {
        AmbControls c;
        core.prepare(48000.0, c);
        amb1fx::clear();
        const Run r = play(core, c, 48000.0, 512, 30.0);
        printf("  30 s: %ld note-ons, %ld note-offs, %ld all-notes-off\n", sum(r.noteOns), sum(r.noteOffs), r.ccAllOff);
        check(sum(r.noteOns) == 0 && sum(r.noteOffs) == 0, "no notes in 30 s with midiOut = false");
    }

    printf("MIDI out on: the garden's notes\n");
    {
        AmbControls c;
        c.midiOut = true;
        c.seed = 4321;
        core.prepare(48000.0, c);
        amb1fx::clear();
        const Run r = play(core, c, 48000.0, 512, 120.0);
        printf("  120 s at 48 kHz, blocks of 512: pad %ld, bells %ld, bass %ld, drums %ld note-ons; offs %ld/%ld/%ld/%ld; all-notes-off %ld; most open at once %d\n",
               r.noteOns[0], r.noteOns[1], r.noteOns[2], r.noteOns[9], r.noteOffs[0], r.noteOffs[1], r.noteOffs[2],
               r.noteOffs[9], r.ccAllOff, r.maxOpen);
        check(r.noteOns[0] > 0, "the pad plays (channel 1)");
        check(r.noteOns[1] > 0, "the bells play (channel 2)");
        check(r.noteOns[2] > 0, "the bass plays (channel 3)");
        check(r.dataOk, "note numbers and velocities are 0..127");
        check(r.offsetsOk, "every chunk's offset is inside its block and never goes backwards");
        check(r.dropped == 0, "no event was dropped");
        long other = 0;
        for (int ch : {3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15})
            other += r.noteOns[ch];
        check(other == 0, "nothing on any channel but 1, 2, 3 and 10");
    }

    printf("Time: the engine runs 44100 frames for every second of host time, at any host rate and block size\n");
    for (double rate : {44100.0, 48000.0, 96000.0})
        for (int block : {37, 128, 512, 1000, 4096})
        {
            AmbControls c;
            c.midiOut = true;
            core.prepare(rate, c);
            amb1fx::clear();
            const double secs = 60.0;
            const Run r = play(core, c, rate, block, secs);
            const double want = secs * 44100.0, got = (double)r.chunks * 128.0;
            char what[160];
            snprintf(what, sizeof what, "%.0f Hz, blocks of %d: %.0f engine frames for %.0f wanted (within one chunk)", rate, block, got, want);
            check(std::fabs(got - want) <= 128.0 && r.offsetsOk, what);
        }

    printf("Stopping: Play off ends the notes\n");
    {
        AmbControls c;
        c.midiOut = true;
        c.seed = 99;
        core.prepare(48000.0, c);
        amb1fx::clear();
        play(core, c, 48000.0, 512, 40.0);
        c.play = false;
        const Run r = play(core, c, 48000.0, 512, 5.0);
        int hung = 0;
        for (auto& kv : r.open)
            hung += kv.second > 0;
        printf("  after Play off: %ld all-notes-off, %d notes still open\n", r.ccAllOff, hung);
        check(r.ccAllOff > 0, "Play off sends all-notes-off");
        check(hung == 0, "no note is left on");
        const Run quiet = play(core, c, 48000.0, 512, 20.0);
        check(sum(quiet.noteOns) == 0, "nothing plays while Play is off (20 s)");
    }

    printf("%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
