// SPDX-License-Identifier: GPL-3.0-only
#include "AmbCore.h"
#include <algorithm>
#include <climits>

std::atomic<const AmbCore*> AmbCore::owner_{nullptr};

AmbControls::AmbControls()
{
    for (int i = 0; i < P_NPARAMS; ++i)
        par[(size_t)i] = en_param_info(i)->def;
}

AmbCore::~AmbCore() { release(); }

bool AmbCore::acquire()
{
    const AmbCore* expected = nullptr;
    if (owner_.compare_exchange_strong(expected, this))
        return true;
    return expected == this;
}

void AmbCore::release()
{
    const AmbCore* expected = this;
    owner_.compare_exchange_strong(expected, nullptr);
}

bool AmbCore::isActive() const { return owner_.load() == this; }

void AmbCore::prepare(double hostRate, const AmbControls& c)
{
    resampler_.prepare((double)EN_SR, hostRate);
    lastGain_ = 1.0f;
    regrow_.store(false);

    en_init();
    en_set(P_MIDI, 0);   // MIDI out is a no-op in the plugin; keep the engine from generating it

    last_.fill(INT_MIN);
    lastPlay_ = false;
    lastFreeze_ = false;
    lastNext_ = c.nextToggle;   // starting up is not a "next chord"

    AmbControls quiet = c;
    quiet.play = false;   // set everything first, grow the garden, then start it
    apply(quiet);
    en_regrow();
    if (c.play)
    {
        en_play(1);
        lastPlay_ = true;
    }
}

void AmbCore::apply(const AmbControls& c)
{
    for (int i = 0; i < P_NPARAMS; ++i)
    {
        if (i == P_MIDI)
            continue;
        const en_param_t* info = en_param_info(i);
        const int v = std::clamp(c.par[(size_t)i], (int)info->lo, (int)info->hi);
        if (v != last_[(size_t)i])
        {
            en_set(i, v);
            if (i == P_MODE)
                en_pool(EN_MODE_POOL[v]);   // the mode loads its scale, as on the FM-1
            last_[(size_t)i] = v;
        }
    }
    if (c.play != lastPlay_)
    {
        en_play(c.play ? 1 : 0);
        lastPlay_ = c.play;
    }
    if (c.freeze != lastFreeze_)
    {
        en_freeze(c.freeze ? 1 : 0);
        lastFreeze_ = c.freeze;
    }
    if (c.nextToggle != lastNext_)
    {
        en_next();
        lastNext_ = c.nextToggle;
    }
    if (regrow_.exchange(false, std::memory_order_acq_rel))
        en_regrow();
}

void AmbCore::engineChunk(float* l, float* r, int count)
{
    en_render(tmp_, (uint32_t)count, kEngineGainQ12);
    constexpr float k = 1.0f / 8388608.0f;   // 24-bit in int32
    for (int i = 0; i < count; ++i)
    {
        l[i] = (float)tmp_[2 * i] * k;
        r[i] = (float)tmp_[2 * i + 1] * k;
    }
}

void AmbCore::render(float* l, float* r, int n, float gain)
{
    resampler_.process([this](float* sl, float* sr, int count) { engineChunk(sl, sr, count); }, l, r, n);
    if (n <= 0)
        return;
    const float step = (gain - lastGain_) / (float)n;
    float g = lastGain_;
    for (int i = 0; i < n; ++i)
    {
        g += step;
        l[i] *= g;
        r[i] *= g;
    }
    lastGain_ = gain;
}
