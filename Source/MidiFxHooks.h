// SPDX-License-Identifier: GPL-3.0-only
// The MIDI effect build's side of the engine's one outward hook, en_midi_out(): it keeps what the engine sends in a
// small list, which the audio thread empties after each engine chunk. JUCE-free, so it can be tested headless.
// Not linked into the instrument build (that one uses the no-op hook in EngineHooks.cpp).
#pragma once
#include <cstdint>

namespace amb1fx
{
struct Event
{
    uint8_t status, data1, data2;   // as the engine sent them (channel in the low nibble of status)
};

constexpr int kMaxEvents = 512;     // per chunk: far more than the engine ever sends in 128 frames

void clear();                       // forget everything held, and the count of dropped events
int count();                        // events held now
const Event& at(int i);
int dropped();                      // events lost because the list was full, since clear()
} // namespace amb1fx
