// SPDX-License-Identifier: GPL-3.0-only
// The JUCE wrapper around AmbCore: parameters, programs (the eight pieces), state, and the audio callback.
#pragma once
#include <atomic>
#include <array>
#include <juce_audio_processors/juce_audio_processors.h>
#include "AmbCore.h"
#include "Scale.h"

class AmbProcessor : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener
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

    // A scale of your own (v0.6). Not a host parameter: it is kept in the plugin state next to the parameters, so no
    // parameter id changes and a project saved before it opens exactly as it was. It belongs to the Mode it was
    // made under: choosing another Mode, or another piece, goes back to that Mode's scale, as on the hardware.
    // Root changes keep it (the scale is relative to the root). Message thread for the edits; any thread to read.
    uint32_t customScale() const;                       // relative to the root; 0 = the Mode's scale is in use
    amb1scale::Toggle toggleScaleNote(int pitchClass);  // flip a note (0 = C) in or out, with the hardware's rules
    void useModeScale();                                // forget the scale of your own
    int paramValue(int engineParam) const;              // the engine's value of a host parameter (a choice: its value, not index)

    // Host parameter id for an engine parameter (the engine's P_* enum); nullptr for P_MIDI (not exposed).
    static const char* engineParamId(int engineParam);

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void readControls(AmbControls& c) const;
    void setEngineParamFromValue(int engineParam, int value);
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    static uint32_t packScale(int mode, uint32_t mask) { return (uint32_t)mode << 16 | (mask & 0xFFFFu); }

    AmbCore core_;
    std::array<std::atomic<float>*, P_NPARAMS> raw_{};
    std::atomic<float>* rawPlay_ = nullptr;
    std::atomic<float>* rawFreeze_ = nullptr;
    std::atomic<float>* rawNext_ = nullptr;
    std::atomic<float>* rawMaster_ = nullptr;
    std::atomic<float>* rawSync_ = nullptr;
    std::atomic<int> currentProgram_{0};
    std::atomic<uint32_t> customPacked_{0};   // Mode in the high 16 bits, the scale mask in the low 16 (0 = none)
    std::atomic<bool> blocked_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmbProcessor)
};
