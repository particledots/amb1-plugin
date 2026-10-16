// SPDX-License-Identifier: GPL-3.0-only
// The engine's one outward hook. The FM-1 sends the garden's notes out as MIDI through this; the plugin
// does not (MIDI out is a later step), so it is a no-op. It lives in the engine library so that the
// engine links on its own, without the rest of the plugin.
extern "C" {
#include "amb.h"
}

extern "C" void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    (void)st;
    (void)d1;
    (void)d2;
}
