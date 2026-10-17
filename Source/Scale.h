// SPDX-License-Identifier: GPL-3.0-only
// The rules for a scale of your own, as AMB-1's hardware has them (firmware/src/app/ui.c: toggle_note, and
// project.c: pool_ok). No JUCE in it, so the rules can be tested headless.
//
// A scale ("pool") is a 12-bit mask RELATIVE to the root: bit i means "root + i semitones", and bit 0 is always set.
// A "custom" scale is stored as such a mask; 0 means "none: use the Mode's scale".
#pragma once
#include <cstdint>

extern "C" {
#include "amb.h"
}

namespace amb1scale
{
inline int count(uint32_t pool)
{
    int n = 0;
    for (int i = 0; i < 12; ++i)
        n += (int)(pool >> i & 1u);
    return n;
}

// A pool the engine's own project loader would accept: the root, nothing above the 12th bit, at least 3 notes.
inline bool valid(uint32_t pool) { return (pool & 1u) != 0u && (pool >> 12) == 0u && count(pool) >= 3; }

inline uint32_t modePool(int mode) { return EN_MODE_POOL[mode < 0 ? 0 : mode >= (int)EN_NMODES ? (int)EN_NMODES - 1 : mode]; }

// The scale that sounds: the custom one if there is a valid one, else the Mode's.
inline uint32_t effective(uint32_t custom, int mode) { return valid(custom) ? custom : modePool(mode); }

// True when the scale that sounds is not the Mode's (the hardware shows "Custom").
inline bool isCustom(uint32_t custom, int mode) { return valid(custom) && custom != modePool(mode); }

// A relative pool turned to absolute pitch classes (bit 0 = C) for this root.
inline uint32_t toAbsolute(uint32_t rel, int root)
{
    const uint32_t r = (uint32_t)(root < 0 ? 0 : root > 11 ? 11 : root);
    return ((rel << r) | (rel >> (12u - r))) & 0xFFFu;
}

enum class Toggle { Changed, RootStays, TooFew };

// Flip pitch class `pc` (0 = C) in `pool` (relative to `root`). Like the hardware: the root cannot be taken out
// ("THE ROOT STAYS") and the scale cannot drop below 3 notes ("3 NOTES AT LEAST"). On Changed, `out` is the new pool.
inline Toggle toggle(uint32_t pool, int root, int pc, uint32_t& out)
{
    const int rel = ((pc - root) % 12 + 12) % 12;
    if (rel == 0)
        return Toggle::RootStays;
    const uint32_t p = pool ^ (1u << rel);
    if (count(p) < 3)
        return Toggle::TooFew;
    out = p;
    return Toggle::Changed;
}
} // namespace amb1scale
