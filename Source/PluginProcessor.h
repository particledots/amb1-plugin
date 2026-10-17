// SPDX-License-Identifier: GPL-3.0-only
// The JUCE wrapper around AmbCore: parameters, programs (the eight pieces), state, and the audio callback.
#pragma once
#include <atomic>
#include <array>
#include <juce_audio_processors/juce_audio_processors.h>
#include "AmbCore.h"
#include "Scale.h"

class AmbProcessor : public juce::AudioProcessor,
                     private juce::AudioProcessorValueTreeState::Listener,
                     private juce::AsyncUpdater
{
public:
    AmbProcessor();
    ~AmbProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    const juce::String getName() const override { return "AMB-1"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // The engine is a process-wide singleton, so only one AMB-1 can sound at a time in this version.
    // True when this instance asked for the engine and another instance already had it.
    bool isBlockedByAnotherInstance() const noexcept { return blocked_.load(); }
    // True when this instance owns the engine right now, so the engine's state (the garden display) is its own.
    bool ownsEngine() const noexcept { return core_.isActive(); }

    // A scale of your own (v0.6). The mask is not a host parameter: it is kept in the plugin state next to the
    // parameters, so no parameter id changes and a project saved before it opens exactly as it was. It belongs to the
    // Mode it was made under: choosing another Mode, or another piece, goes back to that Mode's scale, as on the
    // hardware. Root changes keep it (the scale is relative to the root). Message thread for the edits; any thread to read.
    uint32_t customScale() const;                       // relative to the root; 0 = none (the menu's or the Mode's scale is in use)
    amb1scale::Toggle toggleScaleNote(int pitchClass);  // flip a note (0 = C) in or out, with the hardware's rules
    void useModeScale();                                // forget the scale of your own and the menu's: the Mode's scale
    int paramValue(int engineParam) const;              // the engine's value of a host parameter (a choice: its value, not index)

    // The Scale menu (v0.7): the host parameter "scale" (appended after "sync"). Choice 0 = "Mode's own", 1..20 = a
    // scale from ScaleLibrary.h, 21 = "Custom" (shown while the scale is one you edited by hand; it cannot be picked).
    // A menu scale belongs to the Mode it was chosen under, like a scale of your own: another Mode or piece resets it.
    int scaleChoice() const;                            // the Scale parameter's choice, 0..21
    int libraryScale() const;                           // the menu's scale in use (1..20), else 0
    // Put the Scale parameter right when the Mode moved on from under it, or "Custom" was chosen with nothing of your
    // own behind it. Done on the message thread soon after such a change; public so a test can do it at once.
    void normaliseScaleParam();

    // Host parameter id for an engine parameter (the engine's P_* enum); nullptr for P_MIDI (not exposed).
    static const char* engineParamId(int engineParam);

    // The random start (1..65535) given to the engine at its last start; 0 before the first start. Each start of the
    // engine (prepareToPlay) draws a new one, so a garden does not begin the same way every time.
    uint32_t lastRandomStart() const { return randomStart_.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void readControls(AmbControls& c) const;
    void setEngineParamFromValue(int engineParam, int value);
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override { normaliseScaleParam(); }
    void setScaleChoice(int choice);   // set the Scale parameter (and tell the host)
    static uint32_t packScale(int mode, uint32_t mask) { return (uint32_t)mode << 16 | (mask & 0xFFFFu); }

    AmbCore core_;
    std::array<std::atomic<float>*, P_NPARAMS> raw_{};
    std::atomic<float>* rawPlay_ = nullptr;
    std::atomic<float>* rawFreeze_ = nullptr;
    std::atomic<float>* rawNext_ = nullptr;
    std::atomic<float>* rawMaster_ = nullptr;
    std::atomic<float>* rawSync_ = nullptr;
    std::atomic<float>* rawScale_ = nullptr;
    std::atomic<int> currentProgram_{0};
    std::atomic<uint32_t> customPacked_{0};   // Mode in the high 16 bits, the scale mask in the low 16 (0 = none)
    std::atomic<int> libraryMode_{0};         // the Mode that was set when the Scale menu's choice was made
    std::atomic<bool> blocked_{false};
    std::atomic<uint32_t> randomStart_{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmbProcessor)
};
