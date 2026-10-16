// SPDX-License-Identifier: GPL-3.0-only
// The JUCE wrapper around AmbCore: parameters, programs (the eight pieces), state, and the audio callback.
#pragma once
#include <atomic>
#include <array>
#include <juce_audio_processors/juce_audio_processors.h>
#include "AmbCore.h"

class AmbProcessor : public juce::AudioProcessor
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

    // Host parameter id for an engine parameter (the engine's P_* enum); nullptr for P_MIDI (not exposed).
    static const char* engineParamId(int engineParam);

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void readControls(AmbControls& c) const;
    void setEngineParamFromValue(int engineParam, int value);

    AmbCore core_;
    std::array<std::atomic<float>*, P_NPARAMS> raw_{};
    std::atomic<float>* rawPlay_ = nullptr;
    std::atomic<float>* rawFreeze_ = nullptr;
    std::atomic<float>* rawNext_ = nullptr;
    std::atomic<float>* rawMaster_ = nullptr;
    std::atomic<float>* rawSync_ = nullptr;
    std::atomic<int> currentProgram_{0};
    std::atomic<bool> blocked_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmbProcessor)
};
