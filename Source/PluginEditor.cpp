// SPDX-License-Identifier: GPL-3.0-only
#include "PluginEditor.h"

AmbEditor::AmbEditor(AmbProcessor& p) : juce::AudioProcessorEditor(p), proc_(p), generic_(p)
{
    banner_.setText("Another AMB-1 is already running in this session, so this one is silent. "
                    "(This version can sound only one AMB-1 at a time.)",
                    juce::dontSendNotification);
    banner_.setJustificationType(juce::Justification::centred);
    banner_.setColour(juce::Label::backgroundColourId, juce::Colours::darkred);
    banner_.setColour(juce::Label::textColourId, juce::Colours::white);
    banner_.setMinimumHorizontalScale(1.0f);
    addChildComponent(banner_);
    addAndMakeVisible(generic_);
    setSize(460, 640);
    startTimerHz(4);
    timerCallback();
}

AmbEditor::~AmbEditor() { stopTimer(); }

void AmbEditor::timerCallback()
{
    const bool blocked = proc_.isBlockedByAnotherInstance();
    if (banner_.isVisible() != blocked)
    {
        banner_.setVisible(blocked);
        resized();
    }
}

void AmbEditor::resized()
{
    auto area = getLocalBounds();
    if (banner_.isVisible())
        banner_.setBounds(area.removeFromTop(56));
    generic_.setBounds(area);
}
