// SPDX-License-Identifier: GPL-3.0-only
#include "AmbCore.h"
#include "Scale.h"
#include <algorithm>
#include <climits>
#include <cmath>

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
    sync_ = false;
    tickCountdown_ = 0.0;
    hostRate_ = hostRate;
    engineDue_ = 0.0;

    en_init();
    // en_init() leaves the engine's playing and frozen flags as they were, and the engine is a process-wide
    // singleton, so a re-prepare (sample-rate change, audio stop/start) would otherwise come back frozen or
    // playing against the buttons. Clear both; the lines below then set them from the controls.
    en_freeze(0);
    en_play(0);
    // A random start: en_seed() XORs a value made from the 16-bit seed into the engine's generator. It is posted
    // here, before the regrow below, so it shapes the new garden's loops. 0 = none = the engine as it was.
    if (c.seed != 0u)
        en_seed(c.seed);
    // MIDI out is off unless this is the MIDI effect build (c.midiOut): then the engine sends its notes to en_midi_out.
    en_set(P_MIDI, c.midiOut ? 1 : 0);

    last_.fill(INT_MIN);
    lastPool_ = INT_MIN;
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

void AmbCore::setHostTempo(double bpm)
{
    if (bpm >= 20.0 && bpm <= 400.0)
        hostBpm_ = bpm;
}

void AmbCore::apply(const AmbControls& c)
{
    if (c.syncToHost && !sync_)
        tickCountdown_ = 0.0;   // turning sync on: the first tick comes at once
    sync_ = c.syncToHost;

    // The scale that sounds: a valid scale of your own, else the Mode's (Scale.h).
    auto poolFor = [&c](int mode) { return (int)amb1scale::effective(c.scale, mode); };

    for (int i = 0; i < P_NPARAMS; ++i)
    {
        if (i == P_MIDI)
            continue;
        const en_param_t* info = en_param_info(i);
        int want = c.par[(size_t)i];
        // While synced, the engine's Tempo follows the host (clamped to the engine's own 30..120 range): the
        // clock ticks set the grid, but the echo times and the swing are worked out from this value.
        if (i == P_TEMPO && sync_ && hostBpm_ > 0.0)
            want = (int)std::lround(hostBpm_);
        const int v = std::clamp(want, (int)info->lo, (int)info->hi);
        if (v != last_[(size_t)i])
        {
            en_set(i, v);
            if (i == P_MODE)
            {
                lastPool_ = poolFor(v);   // the mode loads its scale, as on the FM-1 (or your own, if you made one)
                en_pool((uint32_t)lastPool_);
            }
            last_[(size_t)i] = v;
        }
    }
    // A change of the scale alone (the Mode did not change): post it. With no scale of your own this never
    // fires, so everything behaves exactly as before.
    {
        const en_param_t* info = en_param_info(P_MODE);
        const int pool = poolFor(std::clamp(c.par[(size_t)P_MODE], (int)info->lo, (int)info->hi));
        if (pool != lastPool_)
        {
            lastPool_ = pool;
            en_pool((uint32_t)pool);
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
    if (sync_ && hostBpm_ > 0.0)
    {
        // 24 ticks per beat, as a MIDI clock. The engine takes its commands at the start of each render call,
        // so a tick lands at the start of the chunk it falls in: at most one chunk (128 frames, 2.9 ms) early.
        // While ticks keep coming the engine follows them; a few chunks after they stop it goes back to its own tempo.
        const double framesPerTick = (double)EN_SR * 60.0 / (hostBpm_ * 24.0);
        while (tickCountdown_ < (double)count)
        {
            en_clock(EN_CLK_TICK);
            tickCountdown_ += framesPerTick;
        }
        tickCountdown_ -= (double)count;
    }
    en_render(tmp_, (uint32_t)count, kEngineGainQ12);
    if (l == nullptr || r == nullptr)
        return;
    constexpr float k = 1.0f / 8388608.0f;   // 24-bit in int32
    for (int i = 0; i < count; ++i)
    {
        l[i] = (float)tmp_[2 * i] * k;
        r[i] = (float)tmp_[2 * i + 1] * k;
    }
}

void AmbCore::advance(int n, ChunkFn onChunk, void* ctx)
{
    if (n <= 0 || hostRate_ <= 0.0)
        return;
    engineDue_ += (double)n * (double)EN_SR / hostRate_;
    const double hostPerEngine = hostRate_ / (double)EN_SR;
    int k = 0;   // chunks run in this block so far
    while (engineDue_ >= (double)Resampler::kChunk)
    {
        engineChunk(nullptr, nullptr, Resampler::kChunk);
        engineDue_ -= (double)Resampler::kChunk;
        const int offset = std::clamp((int)std::lround((double)k * Resampler::kChunk * hostPerEngine), 0, n - 1);
        ++k;
        if (onChunk != nullptr)
            onChunk(ctx, offset);
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
