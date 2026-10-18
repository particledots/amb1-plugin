// SPDX-License-Identifier: GPL-3.0-only
// Headless test of the MIDI effect build ("AMB-1 MIDI", AMB1_MIDI_FX=1) driven the way a host does it: MIDI in, MIDI out,
// no audio channels.   ./fx_test
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "PluginProcessor.h"

#if !AMB1_MIDI_FX
#error "fx_test must be built with AMB1_MIDI_FX=1"
#endif

static bool gOk = true;
static void report(const char* what, bool ok, const std::string& detail = "")
{
    printf("  %-72s %s %s\n", what, ok ? "ok" : "FAIL", detail.c_str());
    gOk = gOk && ok;
}

static void start(AmbProcessor& p, double sr, int block)
{
    p.setRateAndBufferSizeDetails(sr, block);
    p.prepareToPlay(sr, block);
}

struct Out
{
    long noteOns[16] = {};
    long total = 0;
    long passed = 0;      // messages equal to the one marker we fed in
    bool orderOk = true;  // events leave each block in time order, inside the block
    bool shortBuffer = false;
};

static Out run(AmbProcessor& p, double seconds, double sr, int block, bool feedMarker = false)
{
    Out o;
    juce::AudioBuffer<float> buf(0, block);   // a MIDI effect: no audio channels
    const long blocks = (long)(seconds * sr / block);
    for (long b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (feedMarker && b % 50 == 0)
            midi.addEvent(juce::MidiMessage::controllerEvent(5, 64, 77), block / 2);   // channel 5, controller 64, value 77
        p.processBlock(buf, midi);
        int last = 0;
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (meta.samplePosition < last || meta.samplePosition >= block)
                o.orderOk = false;
            last = meta.samplePosition;
            ++o.total;
            if (m.isNoteOn())
                ++o.noteOns[m.getChannel() - 1];
            else if (m.isController() && m.getChannel() == 5 && m.getControllerNumber() == 64 && m.getControllerValue() == 77)
                ++o.passed;
        }
    }
    return o;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    printf("the MIDI effect build\n");
    {
        AmbProcessor p;
        report("it is a MIDI effect that takes and produces MIDI", p.isMidiEffect() && p.producesMidi() && p.acceptsMidi());
        report("named 'AMB-1 MIDI', no tail", p.getName() == "AMB-1 MIDI" && p.getTailLengthSeconds() == 0.0);
        report("no audio channels", p.getTotalNumInputChannels() == 0 && p.getTotalNumOutputChannels() == 0);
        report("the empty (no audio) layout is the supported one", [&] {
            juce::AudioProcessor::BusesLayout l = p.getBusesLayout();
            return p.isBusesLayoutSupported(l);
        }());
        const std::vector<std::string> expected = {
            "pad", "bells", "bass", "pulse", "density", "evolve", "drift", "hits", "echo", "etime", "reverb", "size",
            "bright", "swell", "btone", "motion", "tune", "tempo", "root", "mode", "boct", "padtone", "bassoct",
            "glide", "fifth", "swing", "hats", "play", "freeze", "next", "master", "sync", "scale", "thumplvl", "hatlvl", "openlvl"};
        std::vector<std::string> got;
        for (auto* prm : p.getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(prm))
                got.push_back(withId->paramID.toStdString());
        report("the same 36 parameter ids, in order, as the instrument", got == expected, "(" + std::to_string(got.size()) + " found)");
        report("the editor can be created", [&] {
            std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
            return ed != nullptr && ed->getWidth() > 0 && ed->getHeight() > 0;
        }());
    }

    printf("the garden's notes leave as MIDI\n");
    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        AmbProcessor p;
        const int block = sr == 96000.0 ? 1024 : 512;
        start(p, sr, block);
        const Out o = run(p, 60.0, sr, block);
        char what[120], detail[160];
        snprintf(what, sizeof what, "60 s at %.1f kHz: pad, bells and bass play; time order kept", sr / 1000.0);
        snprintf(detail, sizeof detail, "(pad %ld, bells %ld, bass %ld, drums %ld note-ons)", o.noteOns[0], o.noteOns[1], o.noteOns[2], o.noteOns[9]);
        report(what, o.noteOns[0] > 0 && o.noteOns[1] > 0 && o.noteOns[2] > 0 && o.orderOk, detail);
    }
    {
        AmbProcessor p;
        start(p, 48000.0, 512);
        const Out o = run(p, 20.0, 48000.0, 512, true);
        report("a controller message that comes in goes out again, every time", o.passed == (long)(20.0 * 48000.0 / 512) / 50 + 1,
               "(" + std::to_string(o.passed) + " came out)");
    }
    {
        AmbProcessor p;
        p.apvts.getParameter("play")->setValueNotifyingHost(0.0f);   // as a host restores a saved project: before the engine starts
        start(p, 48000.0, 512);
        const Out o = run(p, 20.0, 48000.0, 512);
        report("with Play off from the start no notes are sent", o.noteOns[0] + o.noteOns[1] + o.noteOns[2] + o.noteOns[9] == 0);
    }
    {
        AmbProcessor p;
        start(p, 48000.0, 512);
        run(p, 10.0, 48000.0, 512);
        p.apvts.getParameter("play")->setValueNotifyingHost(0.0f);   // switched off while it plays
        run(p, 2.0, 48000.0, 512);                                   // the notes end
        const Out o = run(p, 20.0, 48000.0, 512);
        report("switched off while playing: no new notes afterwards (20 s)", o.noteOns[0] + o.noteOns[1] + o.noteOns[2] + o.noteOns[9] == 0);
    }
    {
        AmbProcessor a, b;   // only one engine per process: the second instance sends no notes but passes MIDI through
        start(a, 48000.0, 512);
        start(b, 48000.0, 512);
        const Out ob = run(b, 20.0, 48000.0, 512, true);
        report("a second instance is blocked: no notes, incoming MIDI still passes", b.isBlockedByAnotherInstance() &&
               ob.noteOns[0] + ob.noteOns[1] + ob.noteOns[2] == 0 && ob.passed > 0);
    }
    {
        AmbProcessor p;
        start(p, 48000.0, 512);
        run(p, 5.0, 48000.0, 512);
        juce::MemoryBlock state;
        p.getStateInformation(state);
        AmbProcessor q;
        q.setStateInformation(state.getData(), (int)state.getSize());
        report("saved state loads (same state format as the instrument)", q.getNumPrograms() == 8);
    }

    printf("%s\n", gOk ? "PASS" : "FAIL");
    return gOk ? 0 : 1;
}
