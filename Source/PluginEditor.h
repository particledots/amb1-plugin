// SPDX-License-Identifier: GPL-3.0-only
// A plain parameter editor (JUCE's generic one), with a notice when another AMB-1 holds the engine.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class AmbEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AmbEditor(AmbProcessor& p);
    ~AmbEditor() override;
    void resized() override;

private:
    void timerCallback() override;

    AmbProcessor& proc_;
    juce::Label banner_;
    juce::GenericAudioProcessorEditor generic_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmbEditor)
};
