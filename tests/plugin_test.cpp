// SPDX-License-Identifier: GPL-3.0-only
// Headless test of the actual plugin object: parameter ids, audio through processBlock, the one-instance
// rule, programs (the eight pieces) and saved state.   ./plugin_test
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "PluginProcessor.h"

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
            "glide", "fifth", "swing", "hats", "play", "freeze", "next", "master"};
        std::vector<std::string> got;
        for (auto* prm : p.getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(prm))
                got.push_back(withId->paramID.toStdString());
        report("31 parameters with the expected ids, in order (saved projects depend on them)", got == expected,
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
        a.releaseResources();
        start(b, 48000.0, 512);
        const Stats sb2 = run(b, 3.0, 48000.0, 512);
        report("after the first lets go, the second sounds", sb2.rms > 0.005 && !b.isBlockedByAnotherInstance(), fmt(sb2));
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

    printf(gOk ? "PASS\n" : "FAIL\n");
    return gOk ? 0 : 1;
}
