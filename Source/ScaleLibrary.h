// SPDX-License-Identifier: GPL-3.0-only
// The Scale menu's list (v0.7). No JUCE in it, so it can be tested headless.
//
// Entry 0 is "Mode's own" (the Mode's scale is used, as always). Entries 1..20 are the scales that the engine's eight
// Modes do not have, as relative pools (bit i = root + i semitones, bit 0 always set; the same form the engine's
// en_pool takes and Scale.h uses). They come from the list of 28 scales in the Logic Scripter file
// baroque_quartet_generator_v4.js; its other eight are the engine's own Modes. The last choice, "Custom", is not a
// scale: the Scale parameter shows it while the scale is one you edited by hand on the ring (it cannot be chosen
// from the menu).
//
// Names are at most 13 characters (the menu and the garden's top line are narrow).
#pragma once
#include <cstdint>

namespace amb1scale
{
constexpr int kLibraryCount = 21;    // "Mode's own" + 20 scales
constexpr int kCustomChoice = 21;    // the Scale parameter's last choice: your own, edited by hand
constexpr int kChoiceCount = 22;

struct LibraryEntry
{
    const char* name;
    uint16_t pool;
};

inline const LibraryEntry* libraryTable()
{
    static const LibraryEntry table[kLibraryCount] = {
        {"Mode's own", 0},
        {"Locrian", 0x56B},        {"Harmonic Min", 0x9AD},   {"Melodic Min", 0xAAD},  {"Harmonic Maj", 0x9B5},
        {"Hungarian Min", 0x9CD},  {"Phrygian Dom.", 0x5B3},  {"Double Harm.", 0x9B3}, {"Neapol. Minor", 0x9AB},
        {"Neapol. Major", 0xAAB},  {"Enigmatic", 0xD53},      {"Blues", 0x4E9},        {"Hirajoshi", 0x18D},
        {"In Sen", 0x4A3},         {"Kumoi", 0x28D},          {"Iwato", 0x463},        {"Whole Tone", 0x555},
        {"Octatonic WH", 0xB6D},   {"Octatonic HW", 0x6DB},   {"Prometheus", 0x655},   {"Chromatic", 0xFFF}};
    return table;
}

// The name of a Scale choice (0..21); "Custom" for the last one.
inline const char* choiceName(int choice)
{
    if (choice == kCustomChoice)
        return "Custom";
    return libraryTable()[choice < 0 || choice >= kLibraryCount ? 0 : choice].name;
}

// The relative pool of library entry 1..20; 0 for "Mode's own", "Custom" or anything out of range.
inline uint32_t libraryPool(int index) { return (index > 0 && index < kLibraryCount) ? libraryTable()[index].pool : 0u; }

// Which library entry (1..20) has exactly this relative pool; 0 if none.
inline int libraryFind(uint32_t pool)
{
    for (int i = 1; i < kLibraryCount; ++i)
        if (libraryTable()[i].pool == pool)
            return i;
    return 0;
}
} // namespace amb1scale
