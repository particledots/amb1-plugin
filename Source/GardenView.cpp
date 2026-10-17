// SPDX-License-Identifier: GPL-3.0-only
// What is drawn follows AMB-1's screen (firmware/src/app/ui.c: draw_ring, draw_meters, draw_pulse, draw_header),
// in this editor's dark colours and scaled to the panel's size. Sizes below are the hardware's pixel sizes times
// a scale factor, so the proportions are the original's.
#include "GardenView.h"
#include <cmath>
#include "PluginEditor.h"

namespace
{
using juce::Colour;
using juce::Point;

Colour padColour() { return Colour(0xff93c47d); }
Colour bellColour() { return Colour(0xff5cc3c8); }
Colour bassColour() { return Colour(0xffd9aa4f); }
Colour pulseColour() { return Colour(0xffe6806e); }
Colour frostColour() { return Colour(0xff8ccfff); }
Colour tint(Colour c) { return c.interpolatedWith(AmbLookAndFeel::card(), 0.75f); }   // the quiet version of a colour
Colour mix(Colour a, Colour b, float t) { return a.interpolatedWith(b, juce::jlimit(0.0f, 1.0f, t)); }

// step i of n, clockwise from the top
Point<float> polar(Point<float> c, float r, int i, int n)
{
    const float a = (float)i * juce::MathConstants<float>::twoPi / (float)n;
    return {c.x + std::sin(a) * r, c.y - std::cos(a) * r};
}
void disc(juce::Graphics& g, Point<float> p, float r, Colour c)
{
    g.setColour(c);
    g.fillEllipse(p.x - r, p.y - r, 2.0f * r, 2.0f * r);
}
void square(juce::Graphics& g, Point<float> p, float half, Colour c)
{
    g.setColour(c);
    g.fillRect(p.x - half, p.y - half, 2.0f * half, 2.0f * half);
}
constexpr float kLoopRadius[EN_NLOOP] = {36.0f, 43.0f, 50.0f, 57.0f};   // the hardware's pixels
constexpr float kDesignHeight = 160.0f;   // the panel's height at scale 1

// Where the garden ring sits in the panel, shared by paint() and by clicks. u: one hardware pixel.
struct RingGeometry
{
    Point<float> c;
    float u;
    float s;
    float areaTop;
    float areaH;
};
RingGeometry ringGeometry(juce::Rectangle<float> b)
{
    const float s = b.getHeight() / kDesignHeight;
    const float strip = 24.0f * s, margin = 7.0f * s;
    const float areaTop = strip + margin * 0.5f, areaBottom = b.getBottom() - margin;
    const float areaH = areaBottom - areaTop;
    return {Point<float>(b.getCentreX(), areaTop + areaH * 0.5f), areaH / 122.0f, s, areaTop, areaH};   // 122 = the outer loop's diameter plus a disc
}

// The big scale ring (v0.6.1): the place you edit the scale. It sits in the free space between the level bars and the
// garden ring, as large as that space and the panel's height allow. Notes are at radius R, each a disc of dotR.
struct ScaleRing
{
    Point<float> c;
    float R, dotR, hitR, centreR;
};
ScaleRing scaleRing(juce::Rectangle<float> b)
{
    const RingGeometry g = ringGeometry(b);
    const float left = b.getX() + (126.0f + 18.0f) * g.s;                  // after the four level bars
    const float right = g.c.x - 61.0f * g.u - 14.0f * g.s;                 // before the garden ring's outer loop
    const float availW = juce::jmax(1.0f, right - left);
    const float half = juce::jmax(10.0f * g.s, juce::jmin(g.areaH, availW) * 0.5f);
    const float R = half * (51.0f / 60.5f);
    return {Point<float>(left + availW * 0.5f, g.areaTop + g.areaH * 0.5f), R, half * (9.5f / 60.5f),
            R * 0.245f,   // click zone: just under half the distance between neighbours (0.2588 R), so zones never overlap
            R * 0.6f};
}
constexpr float kScaleRadius = 24.0f;   // the garden ring's own small scale ring, in hardware pixels
} // namespace

GardenView::GardenView(AmbProcessor& p) : proc_(p)
{
    static const char* const layerIds[4] = {"pad", "bells", "bass", "pulse"};
    for (int i = 0; i < 4; ++i)
        raw_[i] = proc_.apvts.getRawParameterValue(layerIds[i]);
    rawRoot_ = proc_.apvts.getRawParameterValue("root");
    rawMode_ = proc_.apvts.getRawParameterValue("mode");
    rawTempo_ = proc_.apvts.getRawParameterValue("tempo");
    setTooltip("Click a note in the big scale ring to add it or take it out. Click the middle to go back to the Mode's scale.");
    setOpaque(false);
    timerCallback();
    startTimerHz(20);
}

uint64_t GardenView::signature(const GardenState& s, int tempo, bool noticeOn)
{
    uint64_t h = 1469598103934665603ull;
    auto in = [&h](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    in((uint64_t)s.live | (uint64_t)s.playing << 1 | (uint64_t)s.frozen << 2 | (uint64_t)s.external << 3 |
       (uint64_t)s.pulseRest << 4 | (uint64_t)s.custom << 5 | (uint64_t)noticeOn << 6);
    in((uint64_t)s.root | (uint64_t)s.mode << 4 | (uint64_t)tempo << 8);
    in(s.pool | (uint64_t)s.chord << 12);
    for (const char* c = s.chordName; *c != 0; ++c)
        in((uint64_t)(unsigned char)*c);
    in((uint64_t)(s.drift * 100.0f));
    in((uint64_t)(s.padLevel * 32.0f) | (uint64_t)(s.bassLevel * 32.0f) << 8);
    for (float v : s.levels)
        in((uint64_t)(v * 64.0f));
    in(s.playing ? ((s.bars / 4u) & 1u) : 2u);
    for (int l = 0; l < EN_NLOOP; ++l)
    {
        in((uint64_t)s.loopPos[(size_t)l] | (uint64_t)(s.loopGlow[(size_t)l] * 16.0f) << 8);
        uint32_t notes = 0;
        for (int i = 0; i < EN_LOOP_LEN[l]; ++i)
            notes |= (uint32_t)s.loopHasNote[(size_t)l][(size_t)i] << i;
        in(notes);
    }
    for (int d = 0; d < EN_NDRUM; ++d)
        in((uint64_t)s.drumPos[(size_t)d] | (uint64_t)(s.drumGlow[(size_t)d] * 16.0f) << 8 | (uint64_t)s.drumPat[(size_t)d] << 16);
    return h;
}

void GardenView::timerCallback()
{
    if (!isShowing() && haveSig_)
        return;   // nobody is looking
    const int levels[4] = {(int)raw_[0]->load(), (int)raw_[1]->load(), (int)raw_[2]->load(), (int)raw_[3]->load()};
    const int tempo = (int)rawTempo_->load();
    GardenState now = GardenState::capture(proc_.ownsEngine(), (int)rawRoot_->load(), (int)rawMode_->load(), levels,
                                           proc_.customScale());
    const uint64_t sig = signature(now, tempo, noticeActive());
    if (haveSig_ && sig == lastSig_)
        return;   // nothing a viewer could see has changed: skip the repaint
    haveSig_ = true;
    lastSig_ = sig;
    shown_ = now;
    tempo_ = tempo;
    repaint();
}

void GardenView::paint(juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float s = b.getHeight() / kDesignHeight;
    const GardenState& st = shown_;

    g.setColour(AmbLookAndFeel::card());
    g.fillRoundedRectangle(b, 8.0f * s);
    g.setColour(AmbLookAndFeel::cardEdge());
    g.drawRoundedRectangle(b.reduced(0.5f), 8.0f * s, 1.0f);

    const float strip = 24.0f * s, margin = 7.0f * s;
    const float stripY = 4.0f * s, stripH = 16.0f * s;

    // --- the top line: key, tempo, beat light; on the right the way to the next chord ---
    {
        const float x = b.getX() + 14.0f * s;
        g.setFont(juce::Font(juce::FontOptions(12.5f * s).withStyle("Bold")));
        g.setColour(st.playing ? AmbLookAndFeel::text() : AmbLookAndFeel::dim());
        const juce::String key = juce::String(EN_NOTE_NAME[st.root]) + " " + (st.custom ? "Custom" : EN_MODE_NAME[st.mode]);
        g.drawText(key, juce::Rectangle<float>(x, stripY, 112.0f * s, stripH), juce::Justification::centredLeft);
        g.setFont(juce::Font(juce::FontOptions(11.5f * s)));
        g.setColour(AmbLookAndFeel::dim());
        const juce::String tempo = st.external ? juce::String("host tempo") : juce::String(tempo_) + " bpm";
        g.drawText(tempo, juce::Rectangle<float>(x + 112.0f * s, stripY, 84.0f * s, stripH), juce::Justification::centredLeft);

        const Point<float> light(x + 112.0f * s + 84.0f * s + 10.0f * s, stripY + stripH * 0.5f);
        if (st.playing)
            disc(g, light, 5.0f * s, ((st.bars / 4u) & 1u) == 0u ? padColour() : tint(padColour()));   // alternates every 4 sixteenths
        else
        {
            juce::Path tri;   // resting: a play triangle
            tri.addTriangle(light.x - 3.5f * s, light.y - 5.0f * s, light.x - 3.5f * s, light.y + 5.0f * s,
                            light.x + 5.0f * s, light.y);
            g.setColour(AmbLookAndFeel::dim());
            g.fillPath(tri);
        }

        const float right = b.getRight() - 14.0f * s;
        if (st.frozen)
        {
            g.setFont(juce::Font(juce::FontOptions(12.0f * s).withStyle("Bold")));
            g.setColour(frostColour());
            g.drawText("held", juce::Rectangle<float>(right - 80.0f * s, stripY, 80.0f * s, stripH), juce::Justification::centredRight);
        }
        else
        {
            const float w = 80.0f * s, h = 6.0f * s, y = stripY + (stripH - h) * 0.5f;
            const juce::Rectangle<float> track(right - w, y, w, h);
            g.setColour(AmbLookAndFeel::cardEdge());
            g.fillRoundedRectangle(track, h * 0.5f);
            if (st.drift > 0.0f)
            {
                g.setColour(padColour());
                g.fillRoundedRectangle(track.withWidth(juce::jmax(h, w * st.drift)), h * 0.5f);
            }
            g.setFont(juce::Font(juce::FontOptions(10.5f * s)));
            g.setColour(AmbLookAndFeel::dim());
            g.drawText("next chord", juce::Rectangle<float>(track.getX() - 76.0f * s, stripY, 70.0f * s, stripH),
                       juce::Justification::centredRight);
        }
    }

    if (noticeActive())   // why the last click on the ring did nothing, in the middle of the top line
    {
        g.setFont(juce::Font(juce::FontOptions(11.5f * s).withStyle("Bold")));
        g.setColour(pulseColour());
        g.drawText(notice_, juce::Rectangle<float>(b.getCentreX() - 130.0f * s, stripY, 260.0f * s, stripH), juce::Justification::centred);
    }

    const float areaTop = strip + margin * 0.5f, areaBottom = b.getBottom() - margin;
    const float areaH = areaBottom - areaTop;

    // --- the ring: the scale, the chord, the four bell loops ---
    {
        const RingGeometry ring = ringGeometry(b);
        const float u = ring.u;
        const Point<float> c = ring.c;

        for (int l = 0; l < EN_NLOOP; ++l)
        {
            const int len = EN_LOOP_LEN[l], pos = st.loopPos[(size_t)l];
            const float glow = st.loopGlow[(size_t)l];
            for (int i = 0; i < len; ++i)
            {
                const auto p = polar(c, kLoopRadius[l] * u, i, len);
                if (st.loopHasNote[(size_t)l][(size_t)i])
                    disc(g, p, 2.0f * u,
                         (i == pos && st.playing) ? mix(bellColour(), frostColour(), glow * 0.5f)
                                                  : mix(tint(bellColour()), bellColour(), 0.75f));
                else
                    square(g, p, 1.0f * u, AmbLookAndFeel::cardEdge());
            }
            if (st.playing)   // the playhead
                disc(g, polar(c, kLoopRadius[l] * u, pos, len), (1.0f + 2.0f * glow) * u, mix(AmbLookAndFeel::dim(), bellColour(), glow));
        }

        for (int i = 0; i < 12; ++i)   // the scale, C at the top
        {
            const auto p = polar(c, kScaleRadius * u, i, 12);
            const bool inScale = (st.pool >> i & 1u) != 0u, inChord = (st.chord >> i & 1u) != 0u;
            if (i == st.root)
                disc(g, p, 5.0f * u, AmbLookAndFeel::text());   // the root: ringed in ink
            if (inChord)
                disc(g, p, 4.0f * u, mix(tint(padColour()), padColour(), 0.375f + 0.625f * st.padLevel));
            else if (inScale)
                disc(g, p, 3.0f * u, mix(tint(padColour()), padColour(), 0.44f));
            else
                disc(g, p, 1.0f * u, AmbLookAndFeel::cardEdge());
        }

        g.setColour(st.frozen ? frostColour() : AmbLookAndFeel::text());
        g.setFont(juce::Font(juce::FontOptions(13.0f * u).withStyle("Bold")));
        g.drawFittedText(st.chordName, juce::Rectangle<float>(c.x - 19.0f * u, c.y - 8.0f * u, 38.0f * u, 16.0f * u).toNearestInt(),
                         juce::Justification::centred, 1, 0.6f);
    }

    // --- the scale ring you edit (v0.6.1): 12 labelled notes, C at the top ---
    {
        const ScaleRing sr = scaleRing(b);
        const float fs = sr.dotR * 1.05f;
        g.setFont(juce::Font(juce::FontOptions(fs).withStyle("Bold")));
        for (int i = 0; i < 12; ++i)
        {
            const auto p = polar(sr.c, sr.R, i, 12);
            const bool inScale = (st.pool >> i & 1u) != 0u, inChord = (st.chord >> i & 1u) != 0u;
            if (inChord)
                disc(g, p, sr.dotR, mix(tint(padColour()), padColour(), 0.5f + 0.5f * st.padLevel));
            else if (inScale)
                disc(g, p, sr.dotR, mix(tint(padColour()), padColour(), 0.62f));
            else
            {
                g.setColour(AmbLookAndFeel::cardEdge());
                g.drawEllipse(p.x - sr.dotR, p.y - sr.dotR, 2.0f * sr.dotR, 2.0f * sr.dotR, 1.2f * s);
            }
            if (i == st.root)   // the root: ringed in ink
            {
                g.setColour(AmbLookAndFeel::text());
                g.drawEllipse(p.x - sr.dotR - 2.0f * s, p.y - sr.dotR - 2.0f * s, 2.0f * (sr.dotR + 2.0f * s),
                              2.0f * (sr.dotR + 2.0f * s), 1.8f * s);
            }
            g.setColour(inScale ? AmbLookAndFeel::card() : AmbLookAndFeel::dim());
            g.drawText(EN_NOTE_NAME[i], juce::Rectangle<float>(p.x - sr.dotR, p.y - sr.dotR, 2.0f * sr.dotR, 2.0f * sr.dotR),
                       juce::Justification::centred);
        }
        // the middle: the scale's name; when it is your own, a button back to the Mode's
        g.setColour(AmbLookAndFeel::dim());
        g.setFont(juce::Font(juce::FontOptions(sr.R * 0.19f)));
        g.drawText("scale", juce::Rectangle<float>(sr.c.x - sr.centreR, sr.c.y - sr.R * 0.5f, 2.0f * sr.centreR, sr.R * 0.25f),
                   juce::Justification::centred);
        g.setColour(st.custom ? padColour() : AmbLookAndFeel::text());
        g.setFont(juce::Font(juce::FontOptions(sr.R * 0.26f).withStyle("Bold")));
        g.drawFittedText(st.custom ? "Custom" : EN_MODE_NAME[st.mode],
                         juce::Rectangle<float>(sr.c.x - sr.centreR, sr.c.y - sr.R * 0.22f, 2.0f * sr.centreR, sr.R * 0.34f).toNearestInt(),
                         juce::Justification::centred, 1, 0.6f);
        if (st.custom)
        {
            g.setColour(AmbLookAndFeel::dim());
            g.setFont(juce::Font(juce::FontOptions(sr.R * 0.17f)));
            g.drawText("tap to reset", juce::Rectangle<float>(sr.c.x - sr.centreR, sr.c.y + sr.R * 0.16f, 2.0f * sr.centreR, sr.R * 0.25f),
                       juce::Justification::centred);
        }
    }

    // --- the four levels, at the left ---
    {
        static const char* const names[4] = {"Pad", "Bells", "Bass", "Pulse"};
        const Colour cols[4] = {padColour(), bellColour(), bassColour(), pulseColour()};
        const float x0 = b.getX() + 16.0f * s, bw = 14.0f * s, pitch = 32.0f * s, labelH = 12.0f * s;
        const float top = areaTop, bottom = areaBottom - labelH;
        const float full = bottom - top;
        for (int i = 0; i < 4; ++i)
        {
            const float x = x0 + (float)i * pitch;
            g.setColour(tint(cols[i]));
            g.fillRoundedRectangle(x, top, bw, full, 3.0f * s);
            const float h = st.levels[i] * full;
            if (h > 0.0f)
            {
                g.setColour(cols[i]);
                const float fh = juce::jmin(full, h + 3.0f * s);
                g.fillRoundedRectangle(x, bottom - fh, bw, fh, 3.0f * s);
            }
            g.setColour(AmbLookAndFeel::dim());
            g.setFont(juce::Font(juce::FontOptions(9.5f * s)));
            g.drawText(names[i], juce::Rectangle<float>(x - 9.0f * s, bottom + 1.0f * s, bw + 18.0f * s, labelH),
                       juce::Justification::centred);
        }
    }

    // --- the three pulse patterns, at the right ---
    {
        static const char* const names[EN_NDRUM] = {"Thump", "Hat", "Open hat"};
        const float spacing = areaH / (float)EN_NDRUM;
        const float rr = spacing * 0.38f, k = rr / 13.0f;   // k: the hardware's pixels
        const float labelW = 44.0f * s;
        const float cx = b.getRight() - 16.0f * s - labelW - rr - 4.0f * s;
        for (int d = 0; d < EN_NDRUM; ++d)
        {
            const Point<float> c(cx, areaTop + spacing * ((float)d + 0.5f));
            const int n = EN_DRUM_N[d];
            const float glow = st.drumGlow[(size_t)d];
            for (int i = 0; i < n; ++i)
            {
                const auto p = polar(c, rr, i, n);
                const bool hit = (st.drumPat[(size_t)d] >> i & 1u) != 0u;
                const bool cur = st.playing && i == st.drumPos[(size_t)d];
                if (hit && st.pulseRest)   // sitting a few bars out: faded
                    disc(g, p, 2.0f * k, AmbLookAndFeel::cardEdge());
                else if (hit)
                    disc(g, p, (cur ? 3.0f : 2.0f) * k, cur ? pulseColour() : mix(tint(pulseColour()), pulseColour(), 0.625f));
                else if (cur)
                    disc(g, p, 1.0f * k, AmbLookAndFeel::dim());
                else
                    square(g, p, 1.0f * k, AmbLookAndFeel::cardEdge());
            }
            if (glow > 0.03f)
                disc(g, c, (1.0f + 4.0f * glow) * k, mix(AmbLookAndFeel::card(), pulseColour(), glow));
            g.setColour(AmbLookAndFeel::dim());
            g.setFont(juce::Font(juce::FontOptions(9.5f * s)));
            g.drawText(names[d], juce::Rectangle<float>(c.x + rr + 8.0f * s, c.y - 7.0f * s, labelW + 8.0f * s, 14.0f * s),
                       juce::Justification::centredLeft);
        }
    }
}

bool GardenView::noticeActive() const { return notice_.isNotEmpty() && juce::Time::getMillisecondCounter() < noticeUntil_; }

juce::Rectangle<float> GardenView::keyLabelBounds() const
{
    const auto b = getLocalBounds().toFloat();
    const float s = b.getHeight() / kDesignHeight;
    return {b.getX() + 14.0f * s, 4.0f * s, 112.0f * s, 16.0f * s};
}

juce::Point<float> GardenView::notePosition(juce::Rectangle<float> panel, int pc)
{
    const ScaleRing sr = scaleRing(panel);
    return polar(sr.c, sr.R, pc, 12);
}

int GardenView::noteAtPoint(juce::Rectangle<float> panel, juce::Point<float> where)
{
    const ScaleRing sr = scaleRing(panel);
    for (int i = 0; i < 12; ++i)
        if (polar(sr.c, sr.R, i, 12).getDistanceFrom(where) <= sr.hitR)
            return i;
    return -1;
}

juce::Point<float> GardenView::scaleCentre(juce::Rectangle<float> panel) { return scaleRing(panel).c; }

bool GardenView::onScaleCentre(juce::Rectangle<float> panel, juce::Point<float> where)
{
    const ScaleRing sr = scaleRing(panel);
    return sr.c.getDistanceFrom(where) <= sr.centreR;
}

void GardenView::mouseMove(const juce::MouseEvent& e)
{
    const auto p = e.position;
    const bool overLabel = shown_.custom && (keyLabelBounds().contains(p) || onScaleCentre(getLocalBounds().toFloat(), p));
    setMouseCursor(overLabel || noteAtPoint(getLocalBounds().toFloat(), p) >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void GardenView::mouseDown(const juce::MouseEvent& e)
{
    if (shown_.custom && (keyLabelBounds().contains(e.position) || onScaleCentre(getLocalBounds().toFloat(), e.position)))
    {
        proc_.useModeScale();
        timerCallback();
        return;
    }
    const int pc = noteAtPoint(getLocalBounds().toFloat(), e.position);
    if (pc < 0)
        return;
    const auto result = proc_.toggleScaleNote(pc);
    if (result == amb1scale::Toggle::RootStays)
        notice_ = "The root stays (change it with Root)";
    else if (result == amb1scale::Toggle::TooFew)
        notice_ = "A scale needs at least 3 notes";
    else
        notice_ = juce::String();
    noticeUntil_ = juce::Time::getMillisecondCounter() + 2000u;
    timerCallback();
}
