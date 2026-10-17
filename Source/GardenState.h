// SPDX-License-Identifier: GPL-3.0-only
// GardenState: a plain copy of what the engine exports for display (the chord, the bell loops, the pulse
// patterns, the layer levels), taken in one go so the editor draws from a consistent set. No JUCE in it, so it
// can be tested headless. What it shows follows AMB-1's own screen (firmware/src/app/ui.c: draw_ring,
// draw_meters, draw_pulse) without the hardware-only parts (CPU, lights, MIDI out).
//
// Threads: the engine writes these values from the audio thread and nothing locks them (upstream's own screen
// reads them the same way). A read can catch a value half a frame old, or a loop slot mid-change; for a picture
// redrawn 20 times a second that is harmless. Nothing here is used for audio.
#pragma once
#include <array>
#include <cstdint>

extern "C" {
#include "amb.h"
}

struct GardenState
{
    bool live = false;     // false: this instance does not own the engine, so there is nothing true to show
    bool playing = false, frozen = false, external = false;
    int root = 0;          // the Root parameter (0 = C), the pitch class marked in the ring
    int mode = 0;          // the Mode parameter
    bool custom = false;   // the scale is one of your own, not the Mode's (the hardware shows "Custom")
    uint32_t pool = 0;     // the scale as absolute pitch classes, bit 0 = C
    uint32_t chord = 0;    // the chord's pitch classes, bit 0 = C
    char chordName[16] = "";
    float drift = 0.0f;    // 0..1 on the way to the next chord
    float padLevel = 0.0f, bassLevel = 0.0f;
    float levels[4] = {};  // Pad, Bells, Bass, Pulse: what the four meters show (0..1)
    uint32_t bars = 0;     // sixteenths played; every 4 the beat light changes
    bool pulseRest = false;
    std::array<int, EN_NLOOP> loopPos{};
    std::array<float, EN_NLOOP> loopGlow{};
    std::array<std::array<bool, EN_LOOP_MAX>, EN_NLOOP> loopHasNote{};   // a bell on this step (else a rest)
    std::array<int, EN_NDRUM> drumPos{};
    std::array<float, EN_NDRUM> drumGlow{};
    std::array<uint32_t, EN_NDRUM> drumPat{};

    // Copy the engine's state. `layerPercent` are the Pad, Bells, Bass and Pulse parameters (0..100); `root` and
    // `mode` the Root and Mode parameters. `live` says whether this instance owns the engine. `customScale` is
    // the scale of your own (relative to the root, see Scale.h), 0 = the Mode's.
    static GardenState capture(bool live, int root, int mode, const int (&layerPercent)[4], uint32_t customScale = 0);
};
