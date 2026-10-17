// SPDX-License-Identifier: GPL-3.0-only
#include "GardenState.h"
#include "Scale.h"
#include <algorithm>

namespace
{
float clamp01(float x) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }
}

GardenState GardenState::capture(bool live, int root, int mode, const int (&layerPercent)[4], uint32_t customScale)
{
    GardenState s;
    s.live = live;
    s.root = std::clamp(root, 0, 11);
    s.mode = std::clamp(mode, 0, (int)EN_NMODES - 1);

    // the scale: the mode's pool (or your own) is relative to the root; rotate it to absolute pitch classes (as AMB-1 does)
    s.custom = amb1scale::isCustom(customScale, s.mode);
    s.pool = amb1scale::toAbsolute(amb1scale::effective(customScale, s.mode), s.root);

    if (!live)
        return s;   // another instance owns the engine: its state is not ours to show

    s.playing = en_playing != 0;
    s.frozen = en_frozen != 0;
    s.external = en_ext != 0;
    s.drift = clamp01(en_drift);
    s.padLevel = clamp01(en_pad_level);
    s.bassLevel = clamp01(en_bass_level);
    s.bars = en_bars;
    s.pulseRest = en_pulse_rest != 0;

    int pcs[EN_NPAD + 1];
    pcs[0] = en_chord_root;
    for (int k = 0; k < EN_NPAD; ++k)
    {
        const int note = en_chord[k];
        pcs[k + 1] = note % 12;
        s.chord |= 1u << (note % 12);
    }
    en_chord_name(pcs, EN_NPAD + 1, s.chordName);

    float bellMax = 0.0f, drumMax = 0.0f;
    for (int l = 0; l < EN_NLOOP; ++l)
    {
        const int len = EN_LOOP_LEN[l];
        s.loopPos[(size_t)l] = std::min<int>(en_loop_pos[l], len - 1);
        s.loopGlow[(size_t)l] = clamp01(en_loop_glow[l]);
        bellMax = std::max(bellMax, s.loopGlow[(size_t)l]);
        for (int i = 0; i < len; ++i)
            s.loopHasNote[(size_t)l][(size_t)i] = en_loop_note[l][i] >= 0;
    }
    for (int d = 0; d < EN_NDRUM; ++d)
    {
        s.drumPos[(size_t)d] = std::min<int>(en_drum_pos[d], EN_DRUM_N[d] - 1);
        s.drumGlow[(size_t)d] = clamp01(en_drum_glow[d]);
        s.drumPat[(size_t)d] = en_drum_pat[d];
        drumMax = std::max(drumMax, s.drumGlow[(size_t)d]);
    }

    // the four meters, as on the hardware: each layer's level times its Level parameter
    const float pct[4] = {(float)layerPercent[0], (float)layerPercent[1], (float)layerPercent[2], (float)layerPercent[3]};
    s.levels[0] = clamp01(s.padLevel * pct[0] * 0.01f);
    s.levels[1] = clamp01(bellMax * pct[1] * 0.01f);
    s.levels[2] = clamp01(s.bassLevel * pct[2] * 0.01f);
    s.levels[3] = clamp01(drumMax * pct[3] * 0.01f);
    return s;
}
