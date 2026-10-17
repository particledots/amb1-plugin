// SPDX-License-Identifier: GPL-3.0-only
#include "PluginProcessor.h"
#include "PluginEditor.h"
#if AMB1_MIDI_FX
#include "MidiFxHooks.h"
#endif

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
    // Added in v0.7, appended again: the Scale menu. 0 = "Mode's own" (the default: the Mode's scale, exactly as before),
    // 1..20 = a scale from ScaleLibrary.h, 21 = "Custom" (a scale edited by hand on the ring; not pickable by hand).
    juce::StringArray scaleChoices;
    for (int i = 0; i < amb1scale::kChoiceCount; ++i)
        scaleChoices.add(amb1scale::choiceName(i));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"scale", 1}, "Scale", scaleChoices, 0));
    return layout;
}

AmbProcessor::AmbProcessor()
#if AMB1_MIDI_FX
    : juce::AudioProcessor(BusesProperties()),   // a MIDI effect: no audio buses
#else
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
#endif
      apvts(*this, nullptr, "AMB1", createLayout())
{
    for (int i = 0; i < P_NPARAMS; ++i)
        raw_[(size_t)i] = kEngineIds[i] ? apvts.getRawParameterValue(kEngineIds[i]) : nullptr;
    rawPlay_ = apvts.getRawParameterValue("play");
    rawFreeze_ = apvts.getRawParameterValue("freeze");
    rawNext_ = apvts.getRawParameterValue("next");
    rawMaster_ = apvts.getRawParameterValue("master");
    rawSync_ = apvts.getRawParameterValue("sync");
    rawScale_ = apvts.getRawParameterValue("scale");
    apvts.addParameterListener("mode", this);
    apvts.addParameterListener("scale", this);
}

AmbProcessor::~AmbProcessor()
{
    cancelPendingUpdate();
    apvts.removeParameterListener("scale", this);
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

int AmbProcessor::scaleChoice() const
{
    return juce::jlimit(0, amb1scale::kChoiceCount - 1, juce::roundToInt(rawScale_->load()));
}

// The menu's scale counts only while the Mode is still the one it was chosen under (as for a scale of your own).
int AmbProcessor::libraryScale() const
{
    const int c = scaleChoice();
    return (c >= 1 && c < amb1scale::kLibraryCount && libraryMode_.load() == paramValue(P_MODE)) ? c : 0;
}

void AmbProcessor::setScaleChoice(int choice)
{
    if (auto* prm = apvts.getParameter("scale"))
        prm->setValueNotifyingHost(prm->convertTo0to1((float)choice));
}

void AmbProcessor::normaliseScaleParam()
{
    const int c = scaleChoice();
    if (c >= 1 && c < amb1scale::kLibraryCount && libraryMode_.load() != paramValue(P_MODE))
        setScaleChoice(0);   // the Mode moved on: the menu goes back to "Mode's own"
    else if (c == amb1scale::kCustomChoice && customScale() == 0)
        setScaleChoice(0);   // "Custom" with nothing of your own behind it
}

amb1scale::Toggle AmbProcessor::toggleScaleNote(int pitchClass)
{
    const int mode = paramValue(P_MODE);
    const uint32_t base = amb1scale::basePool(mode, libraryScale());   // where you start: the menu's scale or the Mode's
    const uint32_t own = customScale();
    uint32_t out = 0;
    const auto r = amb1scale::toggle(own != 0u ? own : base, paramValue(P_ROOT), pitchClass, out);
    if (r != amb1scale::Toggle::Changed)
        return r;
    if (out == amb1scale::modePool(mode))   // back on the Mode's own scale: nothing of your own to keep
    {
        customPacked_.store(0u);
        setScaleChoice(0);
    }
    else if (const int k = amb1scale::libraryFind(out); k > 0)   // exactly a scale from the menu: say so
    {
        customPacked_.store(0u);
        libraryMode_.store(mode);
        setScaleChoice(k);
    }
    else
    {
        customPacked_.store(packScale(mode, out));
        setScaleChoice(amb1scale::kCustomChoice);   // the menu shows "Custom"
    }
    return r;
}

void AmbProcessor::useModeScale()
{
    customPacked_.store(0u);
    setScaleChoice(0);
}

// Any change of Mode (the menu, the host, automation) puts the Mode's own scale back; so does choosing a Scale entry.
void AmbProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (parameterID == "scale")
    {
        if (juce::roundToInt(newValue) == amb1scale::kCustomChoice)
        {
            triggerAsyncUpdate();         // normally set by toggleScaleNote, which has just stored the mask; if a host
            return;                       // set it with no mask behind it, normaliseScaleParam puts "Mode's own" back
        }
        customPacked_.store(0u);          // any other choice replaces what you edited by hand
        libraryMode_.store(paramValue(P_MODE));
        return;
    }
    if (parameterID != "mode")
        return;
    uint32_t packed = customPacked_.load();
    if ((packed & 0xFFFFu) != 0u && (int)(packed >> 16) != juce::roundToInt(newValue) + (int)en_param_info(P_MODE)->lo)
        customPacked_.compare_exchange_strong(packed, 0u);
    if (scaleChoice() != 0)
        triggerAsyncUpdate();   // the Scale menu is reset on the message thread (normaliseScaleParam)
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
    const uint32_t own = customScale();
    c.scale = own != 0u ? own : amb1scale::libraryPool(libraryScale());   // yours, else the menu's, else (0) the Mode's
}

void AmbProcessor::prepareToPlay(double sampleRate, int)
{
    if (core_.acquire())
    {
        blocked_.store(false);
        AmbControls c;
        readControls(c);
        // A different random start every time the engine starts (1..65535: 0 would mean none).
        c.seed = 1u + (uint32_t)juce::Random::getSystemRandom().nextInt(65535);
        randomStart_.store(c.seed);
#if AMB1_MIDI_FX
        c.midiOut = true;   // the notes are this build's output
        fxOut_.ensureSize(8192);
#endif
        core_.prepare(sampleRate, c);
#if AMB1_MIDI_FX
        amb1fx::clear();    // whatever prepare() made the engine say is not part of the music
#endif
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
#if AMB1_MIDI_FX
    return layouts.getMainOutputChannelSet().isDisabled() && layouts.getMainInputChannelSet().isDisabled();
#else
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
#endif
}

#if AMB1_MIDI_FX
namespace
{
// Called by AmbCore::advance() after each engine chunk: move the notes the chunk made into the block's MIDI.
void collectChunk(void* ctx, int hostOffset)
{
    auto* out = static_cast<juce::MidiBuffer*>(ctx);
    for (int i = 0; i < amb1fx::count(); ++i)
    {
        const amb1fx::Event& e = amb1fx::at(i);
        out->addEvent(juce::MidiMessage((int)e.status, (int)e.data1, (int)e.data2), hostOffset);
    }
    amb1fx::clear();
}
} // namespace

// The MIDI effect build: no audio. The garden runs in step with the host's time and its notes go out as MIDI at their
// positions in the block; everything that came in (the keyboard, controllers) passes through unchanged.
void AmbProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const int n = buffer.getNumSamples();
    if (!core_.isActive() || n <= 0)
        return;   // a blocked second instance: the MIDI that came in goes on untouched
    double bpm = 0.0;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
            if (const auto hostBpm = position->getBpm())
                bpm = *hostBpm;
    core_.setHostTempo(bpm);

    AmbControls c;
    readControls(c);
    core_.apply(c);

    fxOut_.clear();
    fxOut_.addEvents(midi, 0, n, 0);
    core_.advance(n, collectChunk, &fxOut_);
    midi.swapWith(fxOut_);
}
#else
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
#endif

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
    setScaleChoice(0);         // and the Scale menu goes back to "Mode's own"
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
            const bool savedScaleMenu = tree.getChildWithProperty("id", "scale").isValid();
            apvts.replaceState(tree);
            if (!savedSync)
                if (auto* prm = apvts.getParameter("sync"))
                    prm->setValueNotifyingHost(prm->getDefaultValue());
            // The same for the Scale menu (v0.7): a project saved before it existed comes back on "Mode's own".
            if (!savedScaleMenu)
                setScaleChoice(0);
            // The scale of your own comes back only if the state has it and it was made under the Mode the state
            // holds; a project saved without one (or before v0.6) gets the Mode's scale, as it always did.
            const auto modeNode = tree.getChildWithProperty("id", "mode");
            const int savedMode = modeNode.isValid() ? (int)modeNode.getProperty("value", -1) + (int)en_param_info(P_MODE)->lo : -1;
            const int savedScale = tree.hasProperty("scale") ? (int)tree.getProperty("scale") : 0;
            const bool keep = savedScale > 0 && amb1scale::valid((uint32_t)savedScale) && savedMode >= 0 &&
                              (int)tree.getProperty("scaleMode", -1) == savedMode;
            customPacked_.store(keep ? packScale(savedMode, (uint32_t)savedScale) : 0u);
            libraryMode_.store(paramValue(P_MODE));   // a menu scale saved with the state is for the Mode saved with it
            if (keep)
                setScaleChoice(amb1scale::kCustomChoice);   // (a v0.6 project's own scale is shown as "Custom" now)
            else if (scaleChoice() == amb1scale::kCustomChoice)
                setScaleChoice(0);                          // "Custom" saved with nothing behind it
        }
    }
}

// This is the factory the plugin formats call to make an instance.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AmbProcessor(); }
