// SPDX-License-Identifier: GPL-3.0-only
// AmbCore: everything between the plugin wrapper and the AMB-1 engine, with no JUCE in it, so it can be
// tested headless. It owns the engine (which has file-level static state: only one core can be active
// in a process), runs it at 44.1 kHz and resamples to the host rate.
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include "Resampler.h"

extern "C" {
#include "amb.h"
}

// What the host's parameters say right now. `par` is indexed by the engine's P_* enum (P_MIDI is ignored).
struct AmbControls
{
    std::array<int, P_NPARAMS> par{};
    bool play = true;
    bool freeze = false;
    bool nextToggle = false;   // every change of this flag asks for the next chord
    AmbControls();             // engine defaults
};

class AmbCore
{
public:
    AmbCore() = default;
    ~AmbCore();
    AmbCore(const AmbCore&) = delete;
    AmbCore& operator=(const AmbCore&) = delete;

    static constexpr uint32_t kEngineGainQ12 = 2400;   // the value the AMB-1 web app uses for MASTER

    // The engine is a process-wide singleton. acquire() returns true if this core now owns it (or already did).
    bool acquire();
    void release();
    bool isActive() const;

    // Start (or restart) the engine for a host rate. Only when active. Posts every parameter.
    void prepare(double hostRate, const AmbControls& c);
    // Post whatever changed since the last call. Audio thread, once per block, only when active.
    void apply(const AmbControls& c);
    // A new piece: fresh loops, the home chord. Safe from any thread; done at the next render().
    void requestRegrow() { regrow_.store(true, std::memory_order_release); }
    // Render n frames at the host rate, with a linear gain ramp from the previous block's gain.
    void render(float* l, float* r, int n, float gain);

private:
    void engineChunk(float* l, float* r, int count);

    Resampler resampler_;
    std::array<int, P_NPARAMS> last_{};
    bool lastPlay_ = false, lastFreeze_ = false, lastNext_ = false;
    std::atomic<bool> regrow_{false};
    float lastGain_ = 1.0f;
    int32_t tmp_[2 * Resampler::kChunk]{};

    static std::atomic<const AmbCore*> owner_;
};
