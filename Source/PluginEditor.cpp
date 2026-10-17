// SPDX-License-Identifier: GPL-3.0-only
#include "PluginEditor.h"

namespace
{
constexpr int kDesignW = 860;   // the size the editor opens at; layout and type scale from it
constexpr int kDesignH = 780;   // = the old 600 plus the 160 px garden panel and its gap
} // namespace

//==============================================================================
AmbLookAndFeel::AmbLookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, background());
    setColour(juce::Label::textColourId, text());

    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff1d2530));
    setColour(juce::ComboBox::textColourId, text());
    setColour(juce::ComboBox::outlineColourId, cardEdge());
    setColour(juce::ComboBox::arrowColourId, dim());
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff1d2530));
    setColour(juce::PopupMenu::textColourId, text());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accent().darker(0.5f));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);

    setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1d2530));
    setColour(juce::TextButton::buttonOnColourId, accent().darker(0.45f));
    setColour(juce::TextButton::textColourOffId, text());
    setColour(juce::TextButton::textColourOnId, juce::Colours::white);

    setColour(juce::Slider::textBoxTextColourId, text());
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId, accent().darker(0.5f));
    setColour(juce::Slider::backgroundColourId, cardEdge());
    setColour(juce::Slider::trackColourId, accent());
    setColour(juce::Slider::thumbColourId, accent());
    setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff1d2530));
    setColour(juce::TextEditor::textColourId, text());
    setColour(juce::TextEditor::outlineColourId, cardEdge());

    setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xff1d2530));
    setColour(juce::TooltipWindow::textColourId, text());
    setColour(juce::TooltipWindow::outlineColourId, accent().darker(0.4f));
}

void AmbLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float pos,
                                      float startAngle, float endAngle, juce::Slider& slider)
{
    const auto area = juce::Rectangle<float>((float)x, (float)y, (float)width, (float)height).reduced(2.0f * scale_);
    const float d = juce::jmin(area.getWidth(), area.getHeight());
    if (d < 8.0f)
        return;
    const auto centre = area.getCentre();
    const float thick = juce::jmax(2.0f, d * 0.11f);
    const float radius = d * 0.5f - thick * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool on = slider.isEnabled();

    juce::Path track;
    track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour(cardEdge());
    g.strokePath(track, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (pos > 0.0f)
    {
        juce::Path value;
        value.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, angle, true);
        g.setColour(on ? accent() : dim());
        g.strokePath(value, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // the pointer: a short line from inside the ring toward the value
    const auto from = centre.getPointOnCircumference(radius * 0.25f, angle);
    const auto to = centre.getPointOnCircumference(radius * 0.68f, angle);
    g.setColour(text());
    g.drawLine(juce::Line<float>(from, to), juce::jmax(1.5f, thick * 0.45f));
}

juce::Font AmbLookAndFeel::getLabelFont(juce::Label& label)
{
    // the value text under a knob is a Label owned by the Slider; give it the scaled size
    if (dynamic_cast<juce::Slider*>(label.getParentComponent()) != nullptr)
        return juce::Font(juce::FontOptions(12.0f * scale_));
    return label.getFont();
}

juce::Font AmbLookAndFeel::getComboBoxFont(juce::ComboBox&) { return juce::Font(juce::FontOptions(13.0f * scale_)); }

juce::Font AmbLookAndFeel::getTextButtonFont(juce::TextButton&, int) { return juce::Font(juce::FontOptions(13.5f * scale_)); }

//==============================================================================
AmbEditor::AmbEditor(AmbProcessor& p) : juce::AudioProcessorEditor(p), proc_(p)
{
    setLookAndFeel(&laf_);

    // --- the red notice when another AMB-1 holds the engine (unchanged behaviour) ---
    banner_.setText("Another AMB-1 is already running in this session, so this one is silent. "
                    "(This version can sound only one AMB-1 at a time.)",
                    juce::dontSendNotification);
    banner_.setJustificationType(juce::Justification::centred);
    banner_.setColour(juce::Label::backgroundColourId, juce::Colours::darkred);
    banner_.setColour(juce::Label::textColourId, juce::Colours::white);
    banner_.setMinimumHorizontalScale(1.0f);
    addChildComponent(banner_);
    addAndMakeVisible(garden_);

    // --- header: piece menu, Play / Freeze / Next chord, Master ---
    pieceCaption_.setText("Piece", juce::dontSendNotification);
    pieceCaption_.setColour(juce::Label::textColourId, AmbLookAndFeel::dim());
    pieceCaption_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(pieceCaption_);

    for (int i = 0; i < proc_.getNumPrograms(); ++i)
        pieceMenu_.addItem(proc_.getProgramName(i), i + 1);
    pieceMenu_.setSelectedItemIndex(proc_.getCurrentProgram(), juce::dontSendNotification);
    pieceMenu_.onChange = [this] {
        const int i = pieceMenu_.getSelectedItemIndex();
        if (i >= 0)
            proc_.setCurrentProgram(i);   // sets the piece's parameters and regrows the garden
    };
    addAndMakeVisible(pieceMenu_);

    for (auto* b : {&playButton_, &freezeButton_, &nextButton_})
    {
        b->setClickingTogglesState(true);
        addAndMakeVisible(*b);
    }
    playAtt_ = std::make_unique<ButtonAtt>(proc_.apvts, "play", playButton_);
    freezeAtt_ = std::make_unique<ButtonAtt>(proc_.apvts, "freeze", freezeButton_);
    nextAtt_ = std::make_unique<ButtonAtt>(proc_.apvts, "next", nextButton_);
    nextButton_.setTooltip("Ask for the next chord. It arrives on the next bar (at once while resting) instead of "
                           "waiting for Drift. Each click asks once.");
    syncRaw_ = proc_.apvts.getRawParameterValue("sync");

    masterCaption_.setText("Master dB", juce::dontSendNotification);
    masterCaption_.setColour(juce::Label::textColourId, AmbLookAndFeel::dim());
    masterCaption_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(masterCaption_);
    masterSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    masterSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 46, 20);
    addAndMakeVisible(masterSlider_);
    masterAtt_ = std::make_unique<SliderAtt>(proc_.apvts, "master", masterSlider_);

    // --- the groups. Every engine parameter the plugin exposes appears exactly once. To regroup, edit here. ---
    addGroup("Layers", {"pad", "bells", "bass", "pulse"});
    addGroup("Motion", {"density", "evolve", "drift", "hits", "motion"});
    addGroup("Space", {"echo", "etime", "reverb", "size"});
    addGroup("Tone", {"bright", "swell", "btone", "padtone"});
    addGroup("Key & tempo", {"root", "mode", "scale", "tempo", "sync", "tune", "boct"});
    addGroup("Bass line", {"bassoct", "glide", "fifth"});
    addGroup("Rhythm", {"swing", "hats"});

    setResizeLimits(600, 500, 1600, 1250);
    setResizable(true, true);
    setSize(kDesignW, kDesignH);
    startTimerHz(10);
    timerCallback();
}

AmbEditor::~AmbEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void AmbEditor::addGroup(const char* title, std::initializer_list<const char*> ids)
{
    auto group = std::make_unique<Group>();
    group->title = title;

    for (const char* id : ids)
    {
        auto* param = proc_.apvts.getParameter(id);
        jassert(param != nullptr);   // an id that is not a parameter: fix the table above
        if (param == nullptr)
            continue;

        auto c = std::make_unique<Control>();
        c->id = id;
        c->caption.setText(param->getName(24), juce::dontSendNotification);
        c->caption.setJustificationType(juce::Justification::centred);
        c->caption.setColour(juce::Label::textColourId, AmbLookAndFeel::dim());
        addAndMakeVisible(c->caption);

        if (dynamic_cast<juce::AudioParameterChoice*>(param) != nullptr)
        {
            c->isChoice = true;
            // the attachment maps by index onto the items, it does not add them: fill the list first
            const auto choices = param->getAllValueStrings();
            c->menu.addItemList(choices, 1);
            if (juce::String(id) == "scale")
            {
                // "Custom" is shown while the scale is one you edited by hand on the ring; it cannot be picked
                c->menu.setItemEnabled(choices.size(), false);
                c->menu.setTooltip("Play this scale instead of the Mode's. Choosing another Mode or piece resets it.");
            }
            int longest = 0;
            for (const auto& choice : choices)
                longest = juce::jmax(longest, choice.length());
            c->weight = juce::jlimit(1.0f, 1.6f, 0.55f + 0.12f * (float)longest);   // "Pent maj" (8) needs room
            addAndMakeVisible(c->menu);
            c->menuAtt = std::make_unique<ComboAtt>(proc_.apvts, id, c->menu);
        }
        else if (dynamic_cast<juce::AudioParameterBool*>(param) != nullptr)
        {
            c->isButton = true;
            c->button.setClickingTogglesState(true);   // lit while on
            c->button.setTooltip("Follow the host's tempo instead of the Tempo knob (the knob is dimmed while this "
                                 "is on). The garden keeps playing whether or not the host is running.");
            addAndMakeVisible(c->button);
            c->buttonAtt = std::make_unique<ButtonAtt>(proc_.apvts, id, c->button);
        }
        else
        {
            c->knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            c->knob.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f,
                                        true);
            c->knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 16);
            addAndMakeVisible(c->knob);
            c->knobAtt = std::make_unique<SliderAtt>(proc_.apvts, id, c->knob);   // double-click: back to default
        }
        if (c->id == "tempo")
            tempoControl_ = c.get();   // (the Control lives on in the group; moving the pointer does not move it)
        group->controls.push_back(std::move(c));
    }
    groups_.push_back(std::move(group));
}

void AmbEditor::timerCallback()
{
    const bool blocked = proc_.isBlockedByAnotherInstance();
    if (banner_.isVisible() != blocked)
    {
        banner_.setVisible(blocked);
        resized();
    }
    // while Tempo sync is on, the Tempo knob is ignored: show it
    if (tempoControl_ != nullptr && syncRaw_ != nullptr)
        tempoControl_->knob.setEnabled(syncRaw_->load() < 0.5f);
    // the host can change the piece (program change, a loaded project): follow it
    const int program = proc_.getCurrentProgram();
    if (pieceMenu_.getSelectedItemIndex() != program)
        pieceMenu_.setSelectedItemIndex(program, juce::dontSendNotification);
}

void AmbEditor::paint(juce::Graphics& g)
{
    g.fillAll(AmbLookAndFeel::background());
    const float s = scale_;

    g.setColour(AmbLookAndFeel::accent());
    g.setFont(juce::Font(juce::FontOptions(26.0f * s).withStyle("Bold")));
    auto title = titleArea_;   // removeFromTop changes its receiver: work on a copy
    g.drawText("AMB-1", title.removeFromTop(title.getHeight() * 0.65f), juce::Justification::bottomLeft);
    g.setColour(AmbLookAndFeel::dim());
    g.setFont(juce::Font(juce::FontOptions(10.5f * s)));
    g.drawText("unofficial port", title, juce::Justification::topLeft);

    for (auto& group : groups_)
    {
        g.setColour(AmbLookAndFeel::card());
        g.fillRoundedRectangle(group->card, 8.0f * s);
        g.setColour(AmbLookAndFeel::cardEdge());
        g.drawRoundedRectangle(group->card.reduced(0.5f), 8.0f * s, 1.0f);
        g.setColour(AmbLookAndFeel::accent().withAlpha(0.85f));
        g.setFont(juce::Font(juce::FontOptions(11.0f * s).withStyle("Bold")));
        g.drawText(group->title.toUpperCase(), group->titleArea, juce::Justification::centredLeft);
    }

    g.setColour(AmbLookAndFeel::dim().withAlpha(0.8f));
    g.setFont(juce::Font(juce::FontOptions(10.0f * s)));
    g.drawText("Unofficial port of AMB-1 by Charles Vestal  |  GPL-3.0  |  not affiliated with the original",
               footerArea_, juce::Justification::centred);
}

void AmbEditor::resized()
{
    scale_ = juce::jlimit(0.6f, 2.0f, juce::jmin((float)getWidth() / (float)kDesignW, (float)getHeight() / (float)kDesignH));
    const float s = scale_;
    laf_.setScale(s);

    auto area = getLocalBounds().toFloat().reduced(10.0f * s);

    if (banner_.isVisible())
    {
        banner_.setBounds(area.removeFromTop(40.0f * s).toNearestInt());
        area.removeFromTop(6.0f * s);
    }

    // --- header ---
    auto header = area.removeFromTop(48.0f * s);
    titleArea_ = header.removeFromLeft(112.0f * s);
    const float rowH = 28.0f * s;
    const float gap = 8.0f * s;

    pieceCaption_.setBounds(header.removeFromLeft(40.0f * s).withSizeKeepingCentre(40.0f * s, rowH).toNearestInt());
    header.removeFromLeft(gap);
    pieceMenu_.setBounds(header.removeFromLeft(160.0f * s).withSizeKeepingCentre(160.0f * s, rowH).toNearestInt());
    header.removeFromLeft(gap * 2.0f);
    for (auto* b : {&playButton_, &freezeButton_, &nextButton_})
    {
        const float w = (b == &nextButton_ ? 104.0f : 76.0f) * s;
        b->setBounds(header.removeFromLeft(w).withSizeKeepingCentre(w, rowH).toNearestInt());
        header.removeFromLeft(gap);
    }
    // Master takes what is left on the right
    if (header.getWidth() > 150.0f * s)
    {
        masterCaption_.setBounds(header.removeFromLeft(62.0f * s).withSizeKeepingCentre(62.0f * s, rowH).toNearestInt());
        header.removeFromLeft(gap);
        masterSlider_.setBounds(header.withSizeKeepingCentre(header.getWidth(), rowH).toNearestInt());
        masterCaption_.setVisible(true);
        masterSlider_.setVisible(true);
    }
    else
    {
        masterCaption_.setVisible(false);
        masterSlider_.setVisible(false);
    }

    footerArea_ = area.removeFromBottom(18.0f * s);
    area.removeFromTop(8.0f * s);

    // --- the garden panel, full width, under the header ---
    garden_.setBounds(area.removeFromTop(160.0f * s).toNearestInt());
    area.removeFromTop(8.0f * s);

    // --- the groups: three rows; in a row, a group's width follows its number of controls ---
    static const std::vector<std::vector<size_t>> rows = {{0, 1}, {2, 3}, {4, 5, 6}};
    const float rowHeight = (area.getHeight() - 2.0f * gap) / (float)rows.size();
    float y = area.getY();
    for (const auto& row : rows)
    {
        auto groupWeight = [this](size_t gi) {
            float sum = 0.0f;
            for (const auto& c : groups_[gi]->controls)
                sum += c->weight;
            return sum;
        };
        float total = 0.0f;
        for (size_t gi : row)
            if (gi < groups_.size())
                total += groupWeight(gi);
        const float usable = area.getWidth() - gap * (float)(row.size() - 1);
        float x = area.getX();
        for (size_t gi : row)
        {
            if (gi >= groups_.size() || total <= 0.0f)
                continue;
            auto& group = *groups_[gi];
            const float gw = groupWeight(gi);
            const float w = usable * gw / total;
            group.card = juce::Rectangle<float>(x, y, w, rowHeight);
            x += w + gap;

            auto content = group.card.reduced(8.0f * s);
            group.titleArea = content.removeFromTop(20.0f * s);
            float cellX = content.getX();
            for (size_t ci = 0; ci < group.controls.size(); ++ci)
            {
                auto& c = *group.controls[ci];
                const float cellW = content.getWidth() * c.weight / gw;
                auto cell = juce::Rectangle<float>(cellX, content.getY(), cellW, content.getHeight());
                cellX += cellW;
                c.caption.setBounds(cell.removeFromTop(18.0f * s).toNearestInt());
                c.caption.setFont(juce::Font(juce::FontOptions(12.0f * s)));
                if (c.isChoice || c.isButton)
                {
                    const float mw = juce::jmax(40.0f, cell.getWidth() - 8.0f * s);
                    const auto box = cell.withSizeKeepingCentre(mw, 28.0f * s).toNearestInt();
                    if (c.isButton)
                        c.button.setBounds(box);
                    else
                        c.menu.setBounds(box);
                }
                else
                {
                    c.knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false,
                                           juce::jmax(30, (int)(cell.getWidth() - 4.0f * s)), (int)(18.0f * s));
                    c.knob.setBounds(cell.toNearestInt());
                }
            }
        }
        y += rowHeight + gap;
    }
    repaint();
}
