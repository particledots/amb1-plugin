// SPDX-License-Identifier: GPL-3.0-only
// Headless test of the actual plugin object: parameter ids, audio through processBlock, the one-instance
// rule, programs (the eight pieces) and saved state.   ./plugin_test
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>
#include "PluginProcessor.h"
#include "GardenView.h"
#include "GardenState.h"
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
            "glide", "fifth", "swing", "hats", "play", "freeze", "next", "master", "sync", "scale", "thumplvl", "hatlvl", "openlvl"};
        std::vector<std::string> got;
        for (auto* prm : p.getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(prm))
                got.push_back(withId->paramID.toStdString());
        report("36 parameters with the expected ids, in order (saved projects depend on them)", got == expected,
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

    printf("random start\n");
    {
        AmbProcessor p;
        report("no random start before the engine has started", p.lastRandomStart() == 0u);
        bool inRange = true;
        std::set<uint32_t> seen;
        for (int i = 0; i < 20; ++i)
        {
            start(p, 48000.0, 512);
            const uint32_t s = p.lastRandomStart();
            inRange = inRange && s >= 1u && s <= 65535u;
            seen.insert(s);
        }
        report("every start draws a random start in 1..65535", inRange);
        report("and they differ from start to start (20 starts, more than one value)", seen.size() > 1u,
               "(" + std::to_string(seen.size()) + " different)");
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

        // The drum levels (v0.10): 100 % by default; they are saved with the project; and a project saved before they
        // existed (their entries taken out) loads at 100 %, even into an instance that has them turned down now.
        const char* drumIds[3] = {"thumplvl", "hatlvl", "openlvl"};
        bool defaults = true;
        AmbProcessor d;
        for (const char* id : drumIds)
            defaults = defaults && juce::roundToInt(getValue(d, id)) == 100;
        report("the three drum levels start at 100 %", defaults);
        AmbProcessor e;
        setValue(e, "thumplvl", 0.0f);
        setValue(e, "hatlvl", 50.0f);
        setValue(e, "openlvl", 150.0f);
        {   // v0.12: the hats reach 400 %, the thump stops at 150 %
            AmbProcessor g;
            setValue(g, "hatlvl", 400.0f);
            setValue(g, "openlvl", 400.0f);
            setValue(g, "thumplvl", 400.0f);
            report("the hat levels reach 400 % and the thump stops at 150 %", juce::roundToInt(getValue(g, "hatlvl")) == 400 &&
                   juce::roundToInt(getValue(g, "openlvl")) == 400 && juce::roundToInt(getValue(g, "thumplvl")) == 150);
        }
        juce::MemoryBlock drumSaved;
        e.getStateInformation(drumSaved);
        AmbProcessor f;
        f.setStateInformation(drumSaved.getData(), (int)drumSaved.getSize());
        report("saved drum levels come back (0, 50, 150 %)", juce::roundToInt(getValue(f, "thumplvl")) == 0 &&
               juce::roundToInt(getValue(f, "hatlvl")) == 50 && juce::roundToInt(getValue(f, "openlvl")) == 150);
        auto dx = juce::AudioProcessor::getXmlFromBinary(drumSaved.getData(), (int)drumSaved.getSize());
        int taken = 0;
        if (dx != nullptr)
            for (const char* id : drumIds)
                if (auto* child = dx->getChildByAttribute("id", id))
                {
                    dx->removeChildElement(child, true);
                    ++taken;
                }
        juce::MemoryBlock oldDrums;
        if (dx != nullptr)
            juce::AudioProcessor::copyXmlToBinary(*dx, oldDrums);
        AmbProcessor g;
        setValue(g, "thumplvl", 0.0f);   // turned down now
        setValue(g, "openlvl", 0.0f);
        g.setStateInformation(oldDrums.getData(), (int)oldDrums.getSize());
        bool back = taken == 3;
        for (const char* id : drumIds)
            back = back && juce::roundToInt(getValue(g, id)) == 100;
        report("a project saved before the drum levels existed loads them at 100 %", back);
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

    printf("the Scale menu\n");
    {
        using amb1scale::Toggle;
        AmbProcessor p;
        start(p, 48000.0, 512);
        p.setCurrentProgram(0);   // the Garden
        const int root = p.paramValue(P_ROOT), mode = p.paramValue(P_MODE);

        auto* scaleParam = dynamic_cast<juce::AudioParameterChoice*>(p.apvts.getParameter("scale"));
        const auto names = scaleParam != nullptr ? scaleParam->choices : juce::StringArray();
        report("the Scale parameter is a menu of 22 choices: Mode's own, 20 scales, Custom",
               names.size() == 22 && names[0] == "Mode's own" && names[1] == "Locrian" && names[20] == "Chromatic" &&
               names[21] == "Custom");
        report("it starts on \"Mode's own\" and nothing is in use", p.scaleChoice() == 0 && p.libraryScale() == 0 &&
               p.customScale() == 0);

        // a scale from the menu really sounds: every chord note is in it, whatever its size (3 .. 12 notes)
        auto chordsInScale = [&](uint32_t relPool) {
            const uint32_t absPool = amb1scale::toAbsolute(relPool, root);
            run(p, 3.0, 48000.0, 512);
            bool in = true;
            for (int i = 0; i < 40; ++i)
            {
                run(p, 0.25, 48000.0, 512);
                for (int k = 0; k < EN_NPAD; ++k)
                    in = in && (absPool >> (en_chord[k] % 12) & 1u) != 0u;
            }
            return in;
        };
        for (int choice : {1, 7, 12, 16, 17, 20})   // Locrian, Double Harm., Hirajoshi, Whole Tone, Octatonic WH, Chromatic
        {
            setValue(p, "scale", (float)choice);
            const Stats st = run(p, 5.0, 48000.0, 512);
            const bool in = chordsInScale(amb1scale::libraryPool(choice));
            char nm[64];
            snprintf(nm, sizeof nm, "%s: finite audio, not silent, every chord note in the scale", amb1scale::choiceName(choice));
            report(nm, p.libraryScale() == choice && st.nan == 0 && !st.allZero && st.peak < 1.0 && in, fmt(st));
        }

        // Root keeps it; another Mode, a piece, or "Mode's own" put the Mode's scale back
        setValue(p, "scale", 1.0f);
        setValue(p, "root", (float)((root + 3) % 12));
        report("changing Root keeps the menu's scale", p.libraryScale() == 1 && p.scaleChoice() == 1);
        setValue(p, "root", (float)root);
        setValue(p, "mode", (float)((mode + 1) % (int)EN_NMODES));
        report("another Mode: the Mode's scale sounds at once", p.libraryScale() == 0 && p.customScale() == 0);
        p.normaliseScaleParam();   // (the message thread does this soon after; done here by hand)
        report("and the menu goes back to \"Mode's own\"", p.scaleChoice() == 0);
        setValue(p, "mode", (float)mode);
        report("the old Mode again does not bring the scale back", p.libraryScale() == 0 && p.scaleChoice() == 0);
        setValue(p, "scale", 5.0f);
        p.setCurrentProgram(0);   // the same piece again
        report("choosing a piece (even the same one) goes back to \"Mode's own\"", p.scaleChoice() == 0 && p.libraryScale() == 0);
        setValue(p, "scale", 5.0f);
        p.useModeScale();
        report("the ring's middle (useModeScale) goes back to \"Mode's own\"", p.scaleChoice() == 0 && p.libraryScale() == 0);

        // editing by hand starts from the menu's scale
        const uint32_t locrian = amb1scale::libraryPool(1);
        setValue(p, "scale", 1.0f);
        int outRel = 1;
        while (locrian >> outRel & 1u)
            ++outRel;
        const int outPc = (root + outRel) % 12;
        report("a note outside the menu's scale goes in: the scale is yours, the menu says Custom",
               p.toggleScaleNote(outPc) == Toggle::Changed && p.customScale() == (locrian | 1u << outRel) &&
               p.scaleChoice() == amb1scale::kCustomChoice && p.libraryScale() == 0);
        report("the same note again takes it out: back on the menu's scale, nothing of your own kept",
               p.toggleScaleNote(outPc) == Toggle::Changed && p.customScale() == 0 && p.scaleChoice() == 1 && p.libraryScale() == 1);
        report("the root still cannot be taken out of a menu scale", p.toggleScaleNote(root) == Toggle::RootStays && p.scaleChoice() == 1);

        // a hand-edited scale is shown as Custom; picking a menu entry (the same one too) replaces it
        p.toggleScaleNote(outPc);
        const bool own = p.customScale() != 0 && p.scaleChoice() == amb1scale::kCustomChoice;
        setValue(p, "scale", 1.0f);
        report("choosing the menu's entry again replaces what you edited", own && p.customScale() == 0 && p.libraryScale() == 1);
        p.toggleScaleNote(outPc);
        setValue(p, "scale", 0.0f);
        report("choosing \"Mode's own\" replaces what you edited", p.customScale() == 0 && p.scaleChoice() == 0);

        // editing the Mode's own scale by hand into exactly a menu scale (Locrian) names it: the menu says Locrian
        {
            AmbProcessor q;
            start(q, 48000.0, 512);
            q.setCurrentProgram(0);
            const int qroot = q.paramValue(P_ROOT), qmode = q.paramValue(P_MODE);
            auto current = [&] {
                return q.customScale() != 0u ? q.customScale() : amb1scale::basePool(qmode, q.libraryScale());
            };
            bool allChanged = true;
            for (int pass = 0; pass < 2; ++pass)   // add first, then remove: never fewer than 3 notes on the way
                for (int rel = 1; rel < 12; ++rel)
                {
                    const bool want = (locrian >> rel & 1u) != 0u, has = (current() >> rel & 1u) != 0u;
                    if (want != has && want == (pass == 0))
                        allChanged = allChanged && q.toggleScaleNote((qroot + rel) % 12) == Toggle::Changed;
                }
            report("hand edits that end exactly on a menu scale are shown as that scale, not as Custom",
                   allChanged && q.customScale() == 0 && q.scaleChoice() == 1 && q.libraryScale() == 1);
            q.releaseResources();
        }

        // "Custom" picked by a host with nothing behind it is put right
        setValue(p, "scale", (float)amb1scale::kCustomChoice);
        p.normaliseScaleParam();
        report("\"Custom\" with nothing of your own behind it goes back to \"Mode's own\"", p.scaleChoice() == 0);

        // saved projects
        setValue(p, "scale", 7.0f);
        juce::MemoryBlock withMenu;
        p.getStateInformation(withMenu);
        {
            AmbProcessor q;
            q.setStateInformation(withMenu.getData(), (int)withMenu.getSize());
            report("a saved project brings the menu's scale back", q.scaleChoice() == 7 && q.libraryScale() == 7 &&
                   q.customScale() == 0 && q.paramValue(P_MODE) == mode);
        }
        {
            auto xml = juce::AudioProcessor::getXmlFromBinary(withMenu.getData(), (int)withMenu.getSize());
            if (xml != nullptr)
                if (auto* child = xml->getChildByAttribute("id", "scale"))
                    xml->removeChildElement(child, true);   // the format before v0.7: no Scale entry at all
            juce::MemoryBlock old;
            if (xml != nullptr)
                juce::AudioProcessor::copyXmlToBinary(*xml, old);
            AmbProcessor q;
            setValue(q, "scale", 9.0f);   // an instance that has a menu scale right now
            q.setStateInformation(old.getData(), (int)old.getSize());
            report("a project saved before the Scale menu existed loads on \"Mode's own\"", xml != nullptr &&
                   q.scaleChoice() == 0 && q.libraryScale() == 0);
        }
        // a scale you edited by hand, saved by v0.6 (no Scale entry, but the two extra entries): comes back, shown as Custom
        setValue(p, "scale", 0.0f);
        p.toggleScaleNote(outPc);   // outside the Mode's scale or inside it: either way a change; make sure it is yours
        if (p.customScale() == 0)
            p.toggleScaleNote(outPc);
        const uint32_t mine = p.customScale();
        juce::MemoryBlock withOwn;
        p.getStateInformation(withOwn);
        {
            AmbProcessor q;
            q.setStateInformation(withOwn.getData(), (int)withOwn.getSize());
            report("a saved project brings your own scale back, shown as Custom", mine != 0 && q.customScale() == mine &&
                   q.scaleChoice() == amb1scale::kCustomChoice);
        }
        {
            auto xml = juce::AudioProcessor::getXmlFromBinary(withOwn.getData(), (int)withOwn.getSize());
            bool removed = false;
            if (xml != nullptr)
                if (auto* child = xml->getChildByAttribute("id", "scale"))
                {
                    xml->removeChildElement(child, true);
                    removed = true;
                }
            juce::MemoryBlock v06;
            if (xml != nullptr)
                juce::AudioProcessor::copyXmlToBinary(*xml, v06);
            AmbProcessor q;
            q.setStateInformation(v06.getData(), (int)v06.getSize());
            report("a v0.6 project with a scale of your own (no Scale entry) still brings it back, shown as Custom",
                   removed && mine != 0 && q.customScale() == mine && q.scaleChoice() == amb1scale::kCustomChoice);
        }
        p.releaseResources();
    }
    {
        // the garden's picture follows the menu: the ring's scale, the name, and "Custom"
        const int lv[4] = {100, 100, 100, 100};
        const int root = 2, mode = MODE_IONIAN;
        const GardenState none = GardenState::capture(false, root, mode, lv);
        const GardenState menu = GardenState::capture(false, root, mode, lv, 0u, 5);
        const uint32_t mine = amb1scale::libraryPool(5) | 2u;
        const GardenState own = GardenState::capture(false, root, mode, lv, mine, 5);
        report("garden state: the Mode's name, the menu scale's name, \"Custom\"",
               std::string(none.scaleName) == "Ionian" && !none.resettable && none.library == 0 &&
               std::string(menu.scaleName) == "Hungarian Min" && menu.resettable && !menu.custom && menu.library == 5 &&
               menu.pool == amb1scale::toAbsolute(amb1scale::libraryPool(5), root) &&
               std::string(own.scaleName) == "Custom" && own.custom && own.resettable &&
               own.pool == amb1scale::toAbsolute(mine, root));
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
