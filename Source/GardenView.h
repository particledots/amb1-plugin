// SPDX-License-Identifier: GPL-3.0-only
// The garden panel across the top of the editor: the AMB-1's own screen, redrawn here. In the middle the scale as a
// ring of twelve (C at the top) with the chord lit and its name inside, and the four bell loops going round it; at
// the left the levels of Pad, Bells, Bass and Pulse; at the right the three pulse patterns; along the top the key,
// the tempo, a slow beat light and the way to the next chord (or "held" while frozen).
// It only reads (see GardenState.h); it never changes the engine. The one thing it does: a click on a note of the
// ring adds it to the scale or takes it out (AmbProcessor::toggleScaleNote), and a click on "Custom" in the top line
// goes back to the Mode's scale.
#pragma once
#include <cstdint>
#include <juce_audio_processors/juce_audio_processors.h>
#include "GardenState.h"
#include "PluginProcessor.h"

class GardenView : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit GardenView(AmbProcessor& p);
    ~GardenView() override { stopTimer(); }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;

    // The ring's geometry, for clicks (and the tests): where pitch class `pc` (0 = C) sits in a panel of these
    // bounds, and which pitch class (if any) a point in the panel is on.
    static juce::Point<float> notePosition(juce::Rectangle<float> panel, int pc);
    static int noteAtPoint(juce::Rectangle<float> panel, juce::Point<float> where);

private:
    void timerCallback() override;
    static uint64_t signature(const GardenState&, int tempo, bool noticeOn);
    juce::Rectangle<float> keyLabelBounds() const;              // the "C Ionian" / "C Custom" text in the top line
    bool noticeActive() const;

    AmbProcessor& proc_;
    std::atomic<float>* raw_[4] = {};           // Pad, Bells, Bass, Pulse levels (0..100)
    std::atomic<float>* rawRoot_ = nullptr;
    std::atomic<float>* rawMode_ = nullptr;
    std::atomic<float>* rawTempo_ = nullptr;
    GardenState shown_;                         // what the last timer tick captured; paint() draws from it
    int tempo_ = 72;
    juce::String notice_;                       // why a click on the ring did nothing ("The root stays")
    juce::uint32 noticeUntil_ = 0;
    uint64_t lastSig_ = 0;
    bool haveSig_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GardenView)
};
