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
    bool syncToHost = false;   // follow the host's tempo (see AmbCore::setHostTempo) instead of the Tempo parameter
    uint32_t seed = 0;         // the random start, used by prepare() only: 0 = none (the engine's own state); else en_seed(seed)
    uint32_t scale = 0;        // a scale of your own (see Scale.h: relative to the root); 0 = the Mode's scale
    bool midiOut = false;      // prepare() only: let the engine send its notes to en_midi_out (the MIDI effect build); false = none
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
    // The host's tempo in beats per minute, from the audio thread before apply(). A value that is not a plausible
    // tempo (0 = the host did not say) is ignored, so the last good one stays in use.
    void setHostTempo(double bpm);
    // Render n frames at the host rate, with a linear gain ramp from the previous block's gain.
    void render(float* l, float* r, int n, float gain);
    // The MIDI effect build: run the engine for as long as n host frames take, with no audio out. The engine runs
    // in chunks of Resampler::kChunk (128) engine frames; after each chunk, onChunk(ctx, hostOffset) is called so
    // the caller can collect the notes that chunk sent to en_midi_out. hostOffset (0..n-1) is where the chunk
    // starts inside this host block, so a note is placed at most one chunk (2.9 ms) from where the engine made it.
    // The audio thread, after apply(), only when active.
    using ChunkFn = void (*)(void* ctx, int hostOffset);
    void advance(int n, ChunkFn onChunk, void* ctx);

private:
    void engineChunk(float* l, float* r, int count);   // l and r may be null: run the engine, keep no audio

    Resampler resampler_;
    std::array<int, P_NPARAMS> last_{};
    bool lastPlay_ = false, lastFreeze_ = false, lastNext_ = false;
    int lastPool_ = 0;   // the scale last posted to the engine (INT_MIN: none yet)
    std::atomic<bool> regrow_{false};
    float lastGain_ = 1.0f;
    // Tempo sync: while on, the engine's clock is driven by MIDI-clock-style ticks (24 per beat) made from the
    // host's tempo. Ticks are counted in engine frames (44.1 kHz), not host frames.
    bool sync_ = false;
    double hostBpm_ = 0.0;         // last plausible host tempo; 0 = never heard one
    double tickCountdown_ = 0.0;   // engine frames until the next tick
    double hostRate_ = 0.0;        // set by prepare(); advance() turns host frames into engine frames with it
    double engineDue_ = 0.0;       // engine frames owed by advance() (always below one chunk after it returns)
    int32_t tmp_[2 * Resampler::kChunk]{};

    static std::atomic<const AmbCore*> owner_;
};
