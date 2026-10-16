// SPDX-License-Identifier: GPL-3.0-only
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
// Host parameter ids, in the engine's P_* order. These are part of saved projects and automation: never
// rename or reorder them (add new ones instead). nullptr: not exposed.
const char* const kEngineIds[P_NPARAMS] = {
    "pad",   "bells",   "bass",  "pulse",   "density", "evolve", "drift",   "hits",   //
    "echo",  "etime",   "reverb", "size",   "bright",  "swell",  "btone",   "motion", //
    "tune",  nullptr,   "tempo", "root",    "mode",    "boct",   "padtone", "bassoct", //
    "glide", "fifth",   "swing", "hats"};
static_assert(P_NPARAMS == 28, "the engine's parameter list changed: update kEngineIds");

// A parameter that is a list of named choices in the host (the others are plain integers).
bool isChoice(int p) { return p == P_ETIME || p == P_ROOT || p == P_MODE || p == P_PADTONE; }

juce::String engineText(int p, int v)
{
    char buf[16] = {0};
    en_param_text(p, v, buf);
    return juce::String(buf);
}
} // namespace

const char* AmbProcessor::engineParamId(int p) { return (p >= 0 && p < P_NPARAMS) ? kEngineIds[p] : nullptr; }

juce::AudioProcessorValueTreeState::ParameterLayout AmbProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int i = 0; i < P_NPARAMS; ++i)
    {
        if (kEngineIds[i] == nullptr)
            continue;
        const en_param_t* info = en_param_info(i);
        const juce::ParameterID id{kEngineIds[i], 1};
        if (isChoice(i))
        {
            juce::StringArray choices;
            for (int v = info->lo; v <= info->hi; ++v)
                choices.add(engineText(i, v));
            layout.add(std::make_unique<juce::AudioParameterChoice>(id, info->name, choices, info->def - info->lo));
        }
        else
        {
            auto attrs = juce::AudioParameterIntAttributes().withStringFromValueFunction(
                [i](int v, int) { return engineText(i, v); });
            layout.add(std::make_unique<juce::AudioParameterInt>(id, info->name, info->lo, info->hi, info->def, attrs));
        }
    }

    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"play", 1}, "Play (grow / rest)", true));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"freeze", 1}, "Freeze", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"next", 1}, "Next chord (toggle)", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"master", 1}, "Master", juce::NormalisableRange<float>(-48.0f, 6.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));
    return layout;
}

AmbProcessor::AmbProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "AMB1", createLayout())
{
    for (int i = 0; i < P_NPARAMS; ++i)
        raw_[(size_t)i] = kEngineIds[i] ? apvts.getRawParameterValue(kEngineIds[i]) : nullptr;
    rawPlay_ = apvts.getRawParameterValue("play");
    rawFreeze_ = apvts.getRawParameterValue("freeze");
    rawNext_ = apvts.getRawParameterValue("next");
    rawMaster_ = apvts.getRawParameterValue("master");
}

AmbProcessor::~AmbProcessor() { core_.release(); }

void AmbProcessor::readControls(AmbControls& c) const
{
    for (int i = 0; i < P_NPARAMS; ++i)
    {
        if (raw_[(size_t)i] == nullptr)
            continue;
        int v = juce::roundToInt(raw_[(size_t)i]->load());
        if (isChoice(i))
            v += en_param_info(i)->lo;   // a choice's value is its index
        c.par[(size_t)i] = v;
    }
    c.play = rawPlay_->load() > 0.5f;
    c.freeze = rawFreeze_->load() > 0.5f;
    c.nextToggle = rawNext_->load() > 0.5f;
}

void AmbProcessor::prepareToPlay(double sampleRate, int)
{
    if (core_.acquire())
    {
        blocked_.store(false);
        AmbControls c;
        readControls(c);
        core_.prepare(sampleRate, c);
    }
    else
    {
        blocked_.store(true);
    }
}

void AmbProcessor::releaseResources()
{
    core_.release();
    blocked_.store(false);
}

bool AmbProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void AmbProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (!core_.isActive() || buffer.getNumChannels() < 2)
    {
        buffer.clear();
        return;
    }
    AmbControls c;
    readControls(c);
    core_.apply(c);
    const float gain = juce::Decibels::decibelsToGain(rawMaster_->load(), -48.0f);
    core_.render(buffer.getWritePointer(0), buffer.getWritePointer(1), n, gain);
}

juce::AudioProcessorEditor* AmbProcessor::createEditor() { return new AmbEditor(*this); }

int AmbProcessor::getNumPrograms() { return EN_NPIECES; }
int AmbProcessor::getCurrentProgram() { return currentProgram_.load(); }
const juce::String AmbProcessor::getProgramName(int index)
{
    return (index >= 0 && index < EN_NPIECES) ? juce::String(en_piece_name(index)) : juce::String();
}

void AmbProcessor::setEngineParamFromValue(int p, int value)
{
    if (kEngineIds[p] == nullptr)
        return;
    if (auto* prm = apvts.getParameter(kEngineIds[p]))
    {
        const float v = (float)(isChoice(p) ? value - en_param_info(p)->lo : value);
        prm->setValueNotifyingHost(prm->convertTo0to1(v));
    }
}

void AmbProcessor::setCurrentProgram(int index)
{
    if (index < 0 || index >= EN_NPIECES)
        return;
    currentProgram_.store(index);
    int16_t par[P_NPARAMS];
    uint16_t pool;
    en_piece(index, par, &pool);
    for (int p = 0; p < P_NPARAMS; ++p)
        if (p != P_TUNE && p != P_MIDI)   // Tune is the player's, not the piece's
            setEngineParamFromValue(p, par[p]);
    core_.requestRegrow();   // a new piece: fresh loops, the home chord
}

void AmbProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty("program", currentProgram_.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void AmbProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        if (xml->hasTagName(apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml(*xml);
            currentProgram_.store(juce::jlimit(0, EN_NPIECES - 1, (int)tree.getProperty("program", 0)));
            apvts.replaceState(tree);
        }
    }
}

// This is the factory the plugin formats call to make an instance.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AmbProcessor(); }
