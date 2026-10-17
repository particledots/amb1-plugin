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
    // Added after the first release: appended, so every earlier id keeps its place. Off = the Tempo parameter rules.
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"sync", 1}, "Tempo sync", false));
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
    rawSync_ = apvts.getRawParameterValue("sync");
    apvts.addParameterListener("mode", this);
}

AmbProcessor::~AmbProcessor()
{
    apvts.removeParameterListener("mode", this);
    core_.release();
}

int AmbProcessor::paramValue(int p) const
{
    if (p < 0 || p >= P_NPARAMS || raw_[(size_t)p] == nullptr)
        return 0;
    const int v = juce::roundToInt(raw_[(size_t)p]->load());
    return isChoice(p) ? v + en_param_info(p)->lo : v;
}

uint32_t AmbProcessor::customScale() const
{
    const uint32_t packed = customPacked_.load();
    const uint32_t mask = packed & 0xFFFFu;
    return (amb1scale::valid(mask) && (int)(packed >> 16) == paramValue(P_MODE)) ? mask : 0u;
}

amb1scale::Toggle AmbProcessor::toggleScaleNote(int pitchClass)
{
    const int mode = paramValue(P_MODE);
    uint32_t out = 0;
    const auto r = amb1scale::toggle(amb1scale::effective(customScale(), mode), paramValue(P_ROOT), pitchClass, out);
    if (r == amb1scale::Toggle::Changed)   // back on the Mode's own scale: nothing of your own to keep
        customPacked_.store(out == amb1scale::modePool(mode) ? 0u : packScale(mode, out));
    return r;
}

void AmbProcessor::useModeScale() { customPacked_.store(0u); }

// Any change of Mode (the menu, the host, automation) puts the Mode's own scale back.
void AmbProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (parameterID != "mode")
        return;
    uint32_t packed = customPacked_.load();
    if ((packed & 0xFFFFu) != 0u && (int)(packed >> 16) != juce::roundToInt(newValue) + (int)en_param_info(P_MODE)->lo)
        customPacked_.compare_exchange_strong(packed, 0u);
}

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
    c.syncToHost = rawSync_->load() > 0.5f;
    c.scale = customScale();
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
    // The host's tempo, if it says (it can be unknown, and some hosts only give it while playing: the core
    // keeps the last good value). Read every block; used only while Tempo sync is on.
    double bpm = 0.0;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
            if (const auto hostBpm = position->getBpm())
                bpm = *hostBpm;
    core_.setHostTempo(bpm);

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
    customPacked_.store(0u);   // a piece brings its own scale (its Mode's), as on the hardware
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
    // A scale of your own is saved as two extra entries; with none, they are not there (the format of v0.5 and before).
    if (const uint32_t mask = customScale(); mask != 0u)
    {
        state.setProperty("scale", (int)mask, nullptr);
        state.setProperty("scaleMode", paramValue(P_MODE), nullptr);
    }
    else
    {
        state.removeProperty("scale", nullptr);
        state.removeProperty("scaleMode", nullptr);
    }
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
            // A project saved before Tempo sync existed has no entry for it. JUCE would leave the parameter at
            // whatever it holds now; such a project must come back exactly as it was saved: sync off.
            const bool savedSync = tree.getChildWithProperty("id", "sync").isValid();
            apvts.replaceState(tree);
            if (!savedSync)
                if (auto* prm = apvts.getParameter("sync"))
                    prm->setValueNotifyingHost(prm->getDefaultValue());
            // The scale of your own comes back only if the state has it and it was made under the Mode the state
            // holds; a project saved without one (or before v0.6) gets the Mode's scale, as it always did.
            const auto modeNode = tree.getChildWithProperty("id", "mode");
            const int savedMode = modeNode.isValid() ? (int)modeNode.getProperty("value", -1) + (int)en_param_info(P_MODE)->lo : -1;
            const int savedScale = tree.hasProperty("scale") ? (int)tree.getProperty("scale") : 0;
            const bool keep = savedScale > 0 && amb1scale::valid((uint32_t)savedScale) && savedMode >= 0 &&
                              (int)tree.getProperty("scaleMode", -1) == savedMode;
            customPacked_.store(keep ? packScale(savedMode, (uint32_t)savedScale) : 0u);
        }
    }
}

// This is the factory the plugin formats call to make an instance.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AmbProcessor(); }
