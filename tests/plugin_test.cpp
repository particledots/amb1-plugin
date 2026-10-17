// SPDX-License-Identifier: GPL-3.0-only
// Headless test of the actual plugin object: parameter ids, audio through processBlock, the one-instance
// rule, programs (the eight pieces) and saved state.   ./plugin_test
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "PluginProcessor.h"
#include "GardenView.h"
#include "Scale.h"

static bool gOk = true;
static void report(const char* what, bool ok, const std::string& detail = "")
{
    printf("  %-64s %s %s\n", what, ok ? "ok" : "FAIL", detail.c_str());
    gOk = gOk && ok;
}

static float getValue(AmbProcessor& p, const char* id) { return p.apvts.getRawParameterValue(id)->load(); }
static void setValue(AmbProcessor& p, const char* id, float real)   // real = the parameter's own value (a choice: its index)
{
    auto* prm = p.apvts.getParameter(id);
    prm->setValueNotifyingHost(prm->convertTo0to1(real));
}

struct Stats { double peak = 0, rms = 0; long nan = 0; bool allZero = true; };
static Stats run(AmbProcessor& p, double seconds, double sr, int block)
{
    Stats s;
    double sumsq = 0;
    long n = 0;
    juce::AudioBuffer<float> buf(2, block);
    juce::MidiBuffer midi;
    const long total = (long)(seconds * sr);
    while (n < total)
    {
        buf.clear();
        p.processBlock(buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < block; ++i)
            {
                const float x = buf.getSample(c, i);
                if (x != x) { ++s.nan; continue; }
                if (x != 0.0f) s.allZero = false;
                s.peak = std::max(s.peak, (double)std::fabs(x));
                sumsq += (double)x * x;
            }
        n += block;
    }
    s.rms = std::sqrt(sumsq / (2.0 * (double)n));
    return s;
}
static std::string fmt(const Stats& s)
{
    char b[96];
    snprintf(b, sizeof b, "(peak %.3f, rms %.4f)", s.peak, s.rms);
    return b;
}
static void start(AmbProcessor& p, double sr, int block)
{
    p.setRateAndBufferSizeDetails(sr, block);
    p.prepareToPlay(sr, block);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    printf("parameters\n");
    {
        AmbProcessor p;
        const std::vector<std::string> expected = {
            "pad", "bells", "bass", "pulse", "density", "evolve", "drift", "hits", "echo", "etime", "reverb", "size",
            "bright", "swell", "btone", "motion", "tune", "tempo", "root", "mode", "boct", "padtone", "bassoct",
            "glide", "fifth", "swing", "hats", "play", "freeze", "next", "master", "sync"};
        std::vector<std::string> got;
        for (auto* prm : p.getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(prm))
                got.push_back(withId->paramID.toStdString());
        report("32 parameters with the expected ids, in order (saved projects depend on them)", got == expected,
               "(" + std::to_string(got.size()) + " found)");
        report("eight programs, named after the pieces", p.getNumPrograms() == 8 &&
               p.getProgramName(0) == "Garden" && p.getProgramName(7) == "Tides");
        report("a mono output layout is refused", [&] {
            juce::AudioProcessor::BusesLayout l = p.getBusesLayout();
            l.getChannelSet(false, 0) = juce::AudioChannelSet::mono();   // (getMainOutputChannelSet() returns a copy)
            return l.getMainOutputChannelSet() == juce::AudioChannelSet::mono() && !p.isBusesLayoutSupported(l);
        }());
        report("the editor can be created", [&] {
            std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
            return ed != nullptr && ed->getWidth() > 0 && ed->getHeight() > 0;
        }());
    }

    printf("audio\n");
    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        AmbProcessor p;
        const int block = sr == 96000.0 ? 1024 : 512;
        start(p, sr, block);
        const Stats s = run(p, 20.0, sr, block);
        char what[96];
        snprintf(what, sizeof what, "Garden for 20 s at %.1f kHz: finite, audible, no clipping", sr / 1000.0);
        report(what, s.nan == 0 && s.rms > 0.005 && s.peak < 0.95, fmt(s));
    }
    {
        AmbProcessor p;
        start(p, 48000.0, 512);
        setValue(p, "master", -48.0f);
        run(p, 1.0, 48000.0, 512);   // let the gain ramp settle
        const Stats quiet = run(p, 2.0, 48000.0, 512);
        report("master at -48 dB is silent", quiet.peak < 1e-6, fmt(quiet));
    }

    printf("one instance at a time\n");
    {
        AmbProcessor a, b;
        start(a, 48000.0, 512);
        start(b, 48000.0, 512);
        const Stats sa = run(a, 3.0, 48000.0, 512), sb = run(b, 3.0, 48000.0, 512);
        report("the first instance sounds", sa.rms > 0.005 && !a.isBlockedByAnotherInstance(), fmt(sa));
        report("the second is silent and says it is blocked", sb.allZero && b.isBlockedByAnotherInstance());
        report("only the first owns the engine (so only it has a garden to show)", a.ownsEngine() && !b.ownsEngine());
        a.releaseResources();
        start(b, 48000.0, 512);
        const Stats sb2 = run(b, 3.0, 48000.0, 512);
        report("after the first lets go, the second sounds", sb2.rms > 0.005 && !b.isBlockedByAnotherInstance(), fmt(sb2));
        report("and now the second owns the engine, the first does not", b.ownsEngine() && !a.ownsEngine());
    }

    printf("programs and state\n");
    {
        AmbProcessor p;
        start(p, 48000.0, 512);
        setValue(p, "tune", 7.0f);
        p.setCurrentProgram(2);   // Voices: F Aeolian, 56 bpm, sung pad
        report("program 2 (Voices) loads its recipe", getValue(p, "padtone") == 1.0f && getValue(p, "tempo") == 56.0f &&
               getValue(p, "root") == 5.0f && getValue(p, "mode") == 5.0f && p.getCurrentProgram() == 2);
        report("a program leaves Tune alone (it is the player's)", getValue(p, "tune") == 7.0f);
        const Stats s = run(p, 10.0, 48000.0, 512);
        report("Voices sounds after the switch", s.nan == 0 && s.rms > 0.005 && s.peak < 0.95, fmt(s));
        for (int i = 0; i < 8; ++i)
        {
            p.setCurrentProgram(i);
            const Stats t = run(p, 2.0, 48000.0, 512);
            if (t.nan != 0 || t.peak > 0.95)
                report("every piece renders cleanly after a switch", false, std::to_string(i));
        }
        p.setCurrentProgram(4);
        setValue(p, "tempo", 100.0f);
        setValue(p, "master", -12.0f);
        setValue(p, "echo", 77.0f);
        juce::MemoryBlock saved;
        p.getStateInformation(saved);
        p.releaseResources();

        AmbProcessor q;
        q.setStateInformation(saved.getData(), (int)saved.getSize());
        report("saved state restores parameters and the program", getValue(q, "tempo") == 100.0f &&
               std::fabs(getValue(q, "master") + 12.0f) < 0.01f && getValue(q, "echo") == 77.0f && q.getCurrentProgram() == 4);
        report("garbage state is ignored without harm", [&] {
            const char junk[] = "not a state";
            q.setStateInformation(junk, (int)sizeof junk);
            return getValue(q, "tempo") == 100.0f;
        }());
    }

    printf("tempo sync\n");
    {
        // A stand-in for the host's playhead that reports a fixed tempo.
        struct FixedHead : juce::AudioPlayHead
        {
            double bpm = 120.0;
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo info;
                info.setBpm(bpm);
                return info;
            }
        } head;

        AmbProcessor p;
        start(p, 48000.0, 512);
        p.setPlayHead(&head);
        report("tempo sync is off by default", getValue(p, "sync") == 0.0f);

        // sixteenths the engine plays in 20 s (the Garden is 72 bpm: about 96; at the host's 120 bpm: about 160)
        auto sixteenths = [&] {
            run(p, 2.0, 48000.0, 512);   // settle
            const uint32_t before = en_bars;
            run(p, 20.0, 48000.0, 512);
            return (int)(en_bars - before);
        };
        const int free = sixteenths();
        setValue(p, "sync", 1.0f);
        const int synced = sixteenths();
        setValue(p, "sync", 0.0f);
        const int back = sixteenths();
        char d[96];
        snprintf(d, sizeof d, "(%d / %d / %d sixteenths in 20 s; want 96 / 160 / 96)", free, synced, back);
        report("off: its own tempo; on: the host's 120 bpm; off again: its own", std::abs(free - 96) <= 3 &&
               std::abs(synced - 160) <= 3 && std::abs(back - 96) <= 3, d);
        p.releaseResources();
    }
    {
        AmbProcessor a;
        setValue(a, "sync", 1.0f);
        juce::MemoryBlock saved;
        a.getStateInformation(saved);
        AmbProcessor b;
        b.setStateInformation(saved.getData(), (int)saved.getSize());
        report("a saved state keeps Tempo sync on", getValue(b, "sync") == 1.0f);

        // A state written before Tempo sync existed: the same state with the "sync" entry taken out. It must load
        // with sync off, even into an instance that has it on right now.
        AmbProcessor fresh;
        juce::MemoryBlock blob;
        fresh.getStateInformation(blob);
        auto xml = juce::AudioProcessor::getXmlFromBinary(blob.getData(), (int)blob.getSize());
        bool removed = false;
        if (xml != nullptr)
            if (auto* child = xml->getChildByAttribute("id", "sync"))
            {
                xml->removeChildElement(child, true);
                removed = true;
            }
        juce::MemoryBlock old;
        if (xml != nullptr)
            juce::AudioProcessor::copyXmlToBinary(*xml, old);
        b.setStateInformation(old.getData(), (int)old.getSize());
        report("a project saved before Tempo sync existed loads with it off", removed && getValue(b, "sync") == 0.0f);
    }


    printf("a scale of your own\n");
    {
        using amb1scale::Toggle;
        AmbProcessor p;
        start(p, 48000.0, 512);
        p.setCurrentProgram(0);   // the Garden
        const int root = p.paramValue(P_ROOT), mode = p.paramValue(P_MODE);
        const uint32_t modeScale = amb1scale::modePool(mode);
        report("no scale of your own at first", p.customScale() == 0);

        // a pitch class outside the Mode's scale, and its distance from the root
        int outRel = 1;
        while (modeScale >> outRel & 1u)
            ++outRel;
        const int outPc = (root + outRel) % 12;
        report("the root cannot be toggled out", p.toggleScaleNote(root) == Toggle::RootStays && p.customScale() == 0);
        report("a note outside the scale goes in; the scale is now your own",
               p.toggleScaleNote(outPc) == Toggle::Changed && p.customScale() == (modeScale | 1u << outRel));
        report("the same note again takes it out: back on the Mode's scale (nothing of your own kept)",
               p.toggleScaleNote(outPc) == Toggle::Changed && p.customScale() == 0);

        // down to three notes: the root and the first two others of the Mode's scale
        int kept = 0;
        for (int rel = 1; rel < 12; ++rel)
            if (modeScale >> rel & 1u)
            {
                if (kept < 2)
                    ++kept;
                else
                    p.toggleScaleNote((root + rel) % 12);
            }
        const uint32_t three = p.customScale();
        report("taken down to three notes", amb1scale::count(three) == 3 && (three & 1u) != 0u);
        int lastRel = 11;
        while (!(three >> lastRel & 1u))
            --lastRel;
        report("a fourth removal is refused (3 notes at least)",
               p.toggleScaleNote((root + lastRel) % 12) == Toggle::TooFew && p.customScale() == three);

        // the sound: after a couple of seconds every chord note is in this scale
        const uint32_t absThree = amb1scale::toAbsolute(three, root);
        run(p, 3.0, 48000.0, 512);
        bool inScale = true;
        for (int i = 0; i < 40; ++i)
        {
            run(p, 0.25, 48000.0, 512);
            for (int k = 0; k < EN_NPAD; ++k)
                inScale = inScale && (absThree >> (en_chord[k] % 12) & 1u) != 0u;
        }
        report("playing: every chord note is in the 3-note scale", inScale);

        // Root keeps it (the scale is relative to the root); Mode, program and a re-choice of the same piece do not
        setValue(p, "root", (float)((root + 3) % 12));
        report("changing Root keeps your scale", p.customScale() == three);
        setValue(p, "root", (float)root);

        juce::MemoryBlock saved;
        p.getStateInformation(saved);
        {
            AmbProcessor q;
            q.setStateInformation(saved.getData(), (int)saved.getSize());
            report("a saved project brings your scale back", q.customScale() == three && q.paramValue(P_MODE) == mode);
        }
        {
            auto xml = juce::AudioProcessor::getXmlFromBinary(saved.getData(), (int)saved.getSize());
            const bool has = xml != nullptr && xml->hasAttribute("scale") && xml->hasAttribute("scaleMode");
            report("the state holds it as two extra entries, not as a parameter", has);
            if (xml != nullptr)   // saved under another Mode than the one in the state: not trusted
            {
                xml->setAttribute("scaleMode", (mode + 1) % (int)EN_NMODES);
                juce::MemoryBlock odd;
                juce::AudioProcessor::copyXmlToBinary(*xml, odd);
                AmbProcessor q;
                q.setStateInformation(odd.getData(), (int)odd.getSize());
                report("a scale saved under a different Mode than the state's is not used", q.customScale() == 0);
            }
        }
        {
            AmbProcessor fresh;   // a state with no scale in it, the format before v0.6
            juce::MemoryBlock blob;
            fresh.getStateInformation(blob);
            auto xml = juce::AudioProcessor::getXmlFromBinary(blob.getData(), (int)blob.getSize());
            report("a state with no custom scale has no scale entries", xml != nullptr && !xml->hasAttribute("scale") &&
                   !xml->hasAttribute("scaleMode"));
            p.setStateInformation(blob.getData(), (int)blob.getSize());
            report("loading such a project into an instance with a scale of its own gives the Mode's scale", p.customScale() == 0);
        }

        // Mode, program: the Mode's / the piece's scale again
        p.setCurrentProgram(0);
        p.toggleScaleNote(outPc);   // a note outside the Mode's scale: yours now
        const bool haveOwn = p.customScale() != 0;
        setValue(p, "mode", (float)((mode + 1) % (int)EN_NMODES));
        report("choosing another Mode goes back to that Mode's scale", haveOwn && p.customScale() == 0);
        setValue(p, "mode", (float)mode);
        report("and choosing the old Mode again does not bring yours back", p.customScale() == 0);

        p.setCurrentProgram(0);
        p.toggleScaleNote(outPc);
        const bool own2 = p.customScale() != 0;
        p.setCurrentProgram(0);   // the same piece again
        report("choosing a piece (even the same one) goes back to its scale", own2 && p.customScale() == 0);

        p.toggleScaleNote(outPc);
        const bool own3 = p.customScale() != 0;
        p.useModeScale();
        report("\"Custom\" clicked: back to the Mode's scale", own3 && p.customScale() == 0);
        p.releaseResources();
    }
    {
        // the ring's click zones (the geometry the garden panel draws with)
        bool ok = true;
        for (auto panel : {juce::Rectangle<float>(0, 0, 820, 160), juce::Rectangle<float>(0, 0, 1500, 300), juce::Rectangle<float>(0, 0, 560, 100)})
        {
            for (int pc = 0; pc < 12; ++pc)
                ok = ok && GardenView::noteAtPoint(panel, GardenView::notePosition(panel, pc)) == pc;
            for (int pc = 0; pc < 12; ++pc)   // the scale ring is inside the panel and left of the garden ring
            {
                const auto q = GardenView::notePosition(panel, pc);
                ok = ok && panel.reduced(2.0f).contains(q) && q.x < panel.getCentreX() - 20.0f;
            }
            ok = ok && GardenView::onScaleCentre(panel, GardenView::scaleCentre(panel)) &&
                 GardenView::noteAtPoint(panel, GardenView::scaleCentre(panel)) == -1 &&
                 !GardenView::onScaleCentre(panel, GardenView::notePosition(panel, 0));
            const juce::Point<float> middle(panel.getCentreX(), panel.getCentreY());
            ok = ok && GardenView::noteAtPoint(panel, middle) == -1 &&
                 GardenView::noteAtPoint(panel, juce::Point<float>(2.0f, 2.0f)) == -1 &&
                 GardenView::noteAtPoint(panel, juce::Point<float>(panel.getRight() - 2.0f, panel.getBottom() - 2.0f)) == -1;
            // halfway between two neighbours is on one of them or neither, never a third
            const auto a = GardenView::notePosition(panel, 0), b = GardenView::notePosition(panel, 1);
            const int mid = GardenView::noteAtPoint(panel, (a + b) * 0.5f);
            ok = ok && (mid == 0 || mid == 1 || mid == -1);
        }
        report("a click on a note of the scale ring finds that note; the middle resets; elsewhere finds nothing", ok);
    }

    printf(gOk ? "PASS\n" : "FAIL\n");
    return gOk ? 0 : 1;
}
