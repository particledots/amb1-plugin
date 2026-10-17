// SPDX-License-Identifier: GPL-3.0-only
// The MIDI effect build's en_midi_out(): the engine calls it from inside en_render() (audio thread) and from a few
// control calls; here it only appends to a fixed list. No allocation, no locks. See MidiFxHooks.h.
#include "MidiFxHooks.h"

extern "C" {
#include "amb.h"
}

namespace
{
amb1fx::Event g_events[amb1fx::kMaxEvents];
int g_count = 0;
int g_dropped = 0;
}

extern "C" void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (g_count >= amb1fx::kMaxEvents)
    {
        ++g_dropped;
        return;
    }
    g_events[g_count++] = {(uint8_t)(st & 0xFFu), (uint8_t)(d1 & 0x7Fu), (uint8_t)(d2 & 0x7Fu)};
}

namespace amb1fx
{
void clear()
{
    g_count = 0;
    g_dropped = 0;
}
int count() { return g_count; }
const Event& at(int i) { return g_events[i]; }
int dropped() { return g_dropped; }
} // namespace amb1fx
