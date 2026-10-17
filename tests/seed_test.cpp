// SPDX-License-Identifier: GPL-3.0-only
// The random start (AmbControls::seed -> en_seed), in FRESH processes. The engine's random generator is file-static and
// en_init() does not reset it, so a repeat can only be shown in a new process: this test runs itself as a child
// process for every case and compares what the engine played (every pitched note-on, stamped with the engine's
// sixteenth counter, hashed).
//   ./seed_test                      the checks (exit 0 = all passed)
//   ./seed_test --child <seed> <s>   one fresh start with that seed for <s> seconds; prints "notes N events HASH"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "AmbCore.h"

static uint64_t gHash = 1469598103934665603ull;
static uint32_t gNotes = 0;
static void mix(uint32_t v) { gHash = (gHash ^ v) * 1099511628211ull; }

// The plugin has no MIDI out; here the hook records what the engine plays (pad 0x90, bells 0x91, bass 0x92, drums 0x99).
extern "C" void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    const uint32_t s = st & 0xFF;
    if ((s == 0x90 || s == 0x91 || s == 0x92 || s == 0x99) && d2 > 0)
    {
        mix(en_bars);
        mix(s);
        mix(d1);
        ++gNotes;
    }
}

static int child(uint32_t seed, double secs)
{
    AmbCore core;
    core.acquire();
    AmbControls c;
    c.seed = seed;
    core.prepare(48000.0, c);
    en_set(P_MIDI, 1);   // after prepare(), which switches it off
    std::vector<float> l(512), r(512);
    long done = 0;
    const long total = (long)(secs * 48000.0);
    while (done < total)
    {
        core.apply(c);
        core.render(l.data(), r.data(), 512, 1.0f);
        done += 512;
    }
    printf("notes %u events %016llx\n", gNotes, (unsigned long long)gHash);
    return 0;
}

static std::string runChild(const char* self, uint32_t seed, double secs)
{
    const std::string cmd = std::string("\"") + self + "\" --child " + std::to_string(seed) + " " + std::to_string(secs);
    FILE* p = popen(cmd.c_str(), "r");
    if (!p)
        return "";
    char buf[128] = {};
    std::string out;
    while (fgets(buf, sizeof buf, p))
        out += buf;
    pclose(p);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
        out.pop_back();
    return out;
}

static bool gOk = true;
static void report(const char* what, bool ok, const std::string& detail = "")
{
    printf("  %-3s %s %s\n", ok ? "ok" : "FAIL", what, detail.c_str());
    gOk = gOk && ok;
}

int main(int argc, char** argv)
{
    if (argc >= 4 && !strcmp(argv[1], "--child"))
        return child((uint32_t)strtoul(argv[2], nullptr, 10), atof(argv[3]));

    const char* self = argv[0];
    const double secs = 30.0;
    printf("random start (fresh processes, %.0f s each)\n", secs);
    const std::string none = runChild(self, 0, secs);
    const std::string a1 = runChild(self, 12345, secs), a2 = runChild(self, 12345, secs);
    const std::string b = runChild(self, 12346, secs);
    printf("  no seed: %s\n  12345:   %s\n  12346:   %s\n", none.c_str(), a1.c_str(), b.c_str());
    report("the children print a result", !none.empty() && !a1.empty() && !b.empty());
    report("the same seed in two fresh processes plays the same notes", !a1.empty() && a1 == a2);
    report("another seed plays different notes", a1 != b);
    report("a seed changes the garden from the unseeded start", a1 != none);
    report("seed 65536 is seed 0 (only 16 bits count): same as no seed", runChild(self, 65536, secs) == none);
    report("seed 65537 is seed 1", runChild(self, 65537, secs) == runChild(self, 1, secs));

    std::set<std::string> distinct;
    const int n = 40;
    for (int s = 1; s <= n; ++s)
        distinct.insert(runChild(self, (uint32_t)s, secs));
    report("seeds 1..40 each play a different garden", (int)distinct.size() == n,
           "(" + std::to_string(distinct.size()) + " different of " + std::to_string(n) + ")");

    printf(gOk ? "PASS\n" : "FAIL\n");
    return gOk ? 0 : 1;
}
