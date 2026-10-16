// SPDX-License-Identifier: GPL-3.0-only
// The AMB-1 editor: grouped controls, a piece (preset) menu, Play / Freeze / Next chord, Master. Resizable.
// Every control is attached to a host parameter by id, so automation and saved state are untouched.
#pragma once
#include <atomic>
#include <memory>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// Dark, flat look. Knobs are drawn here; everything else is JUCE's V4 look with colours and fonts set.
class AmbLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AmbLookAndFeel();
    void setScale(float s) noexcept { scale_ = s; }

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                          float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    juce::Font getLabelFont(juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;

    static juce::Colour background() { return juce::Colour(0xff0d1117); }
    static juce::Colour card() { return juce::Colour(0xff161c24); }
    static juce::Colour cardEdge() { return juce::Colour(0xff252f3c); }
    static juce::Colour text() { return juce::Colour(0xffd5dbe3); }
    static juce::Colour dim() { return juce::Colour(0xff8793a3); }
    static juce::Colour accent() { return juce::Colour(0xff86c3b0); }

private:
    float scale_ = 1.0f;
};

class AmbEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AmbEditor(AmbProcessor& p);
    ~AmbEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // One parameter: a caption and a knob, or (for a list of named choices) a menu, or (for an on/off one) a button.
    // The attachments are declared last so they are destroyed before the components they drive.
    struct Control
    {
        juce::String id;
        bool isChoice = false;
        bool isButton = false;
        float weight = 1.0f;   // its share of the group's width: a menu with longer choices gets more
        juce::Label caption;
        juce::Slider knob;
        juce::ComboBox menu;
        juce::TextButton button{"Host"};
        std::unique_ptr<SliderAtt> knobAtt;
        std::unique_ptr<ComboAtt> menuAtt;
        std::unique_ptr<ButtonAtt> buttonAtt;
    };

    struct Group
    {
        juce::String title;
        std::vector<std::unique_ptr<Control>> controls;
        juce::Rectangle<float> card, titleArea;   // set by resized(), used by paint()
    };

    void addGroup(const char* title, std::initializer_list<const char*> ids);
    void timerCallback() override;

    AmbProcessor& proc_;
    AmbLookAndFeel laf_;   // first member: it must outlive every component that uses it
    float scale_ = 1.0f;
    juce::TooltipWindow tooltips_{this, 600};   // shows the text set with setTooltip() after the mouse rests
    std::atomic<float>* syncRaw_ = nullptr;     // the Tempo sync parameter's value
    Control* tempoControl_ = nullptr;           // the Tempo knob: dimmed while synced, since it is ignored then

    juce::Label banner_;
    juce::Label pieceCaption_;
    juce::ComboBox pieceMenu_;
    juce::TextButton playButton_{"Play"}, freezeButton_{"Freeze"}, nextButton_{"Next chord"};
    juce::Label masterCaption_;
    juce::Slider masterSlider_;
    std::vector<std::unique_ptr<Group>> groups_;
    juce::Rectangle<float> titleArea_, footerArea_;

    std::unique_ptr<ButtonAtt> playAtt_, freezeAtt_, nextAtt_;
    std::unique_ptr<SliderAtt> masterAtt_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmbEditor)
};
