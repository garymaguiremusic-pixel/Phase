#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace pc
{

namespace
{
    constexpr int maxChunk = 2048;
    const juce::Identifier uiScaleId ("uiScale");
}

juce::AudioProcessorValueTreeState::ParameterLayout PhaseCheckProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ParamID::invert, 1 }, "Invert polarity", false,
        AudioParameterBoolAttributes().withStringFromValueFunction ([] (bool v, int) { return v ? "On" : "Off"; })));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::delayMs, 1 }, "Kick delay",
        NormalisableRange<float> (0.0f, 20.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("ms")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " ms"; })));

    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ParamID::delayOn, 1 }, "Delay enabled", false,
        AudioParameterBoolAttributes().withStringFromValueFunction ([] (bool v, int) { return v ? "On" : "Off"; })));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::rotate, 1 }, "Phase rotate",
        NormalisableRange<float> (0.0f, 180.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withLabel (String::fromUTF8 ("\xC2\xB0"))
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + String::fromUTF8 ("\xC2\xB0"); })));

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::mode, 1 }, "Mode",
        StringArray { "Hit", "Overlap" }, 0));

    return { p.begin(), p.end() };
}

PhaseCheckProcessor::PhaseCheckProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PhaseCheck", createLayout())
{
    pInvert  = apvts.getRawParameterValue (ParamID::invert);
    pDelayMs = apvts.getRawParameterValue (ParamID::delayMs);
    pDelayOn = apvts.getRawParameterValue (ParamID::delayOn);
    pRotate  = apvts.getRawParameterValue (ParamID::rotate);
    pMode    = apvts.getRawParameterValue (ParamID::mode);

    apvts.addParameterListener (ParamID::delayOn, this);
    apvts.state.setProperty (uiScaleId, 1.0, nullptr);
}

PhaseCheckProcessor::~PhaseCheckProcessor()
{
    apvts.removeParameterListener (ParamID::delayOn, this);
    cancelPendingUpdate();
    engine.stop();
}

//==============================================================================
bool PhaseCheckProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void PhaseCheckProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate = sampleRate > 0 ? sampleRate : 44100.0;
    juce::ignoreUnused (samplesPerBlock);

    rawMono.assign (maxChunk, 0.0f);
    procMono.assign (maxChunk, 0.0f);
    scMono.assign (maxChunk, 0.0f);

    kick.prepare (currentRate, std::max (1, getMainBusNumOutputChannels()));
    kick.setTargets (pInvert->load() > 0.5f,
                     (pDelayOn->load() > 0.5f && pMode->load() < 0.5f) ? (double) pDelayMs->load() : 0.0,
                     (double) pRotate->load());
    kick.reset();

    engine.prepare (currentRate);
    setLatencySamples (0);   // the IIR rotator has no reported latency; the delay is intentional
}

void PhaseCheckProcessor::releaseResources()
{
    engine.stop();
}

void PhaseCheckProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();

    const int numSamples = buffer.getNumSamples();
    auto main = getBusBuffer (buffer, true, 0);

    // clear any output channels without a matching input
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    juce::AudioBuffer<float> scBuffer;
    const juce::AudioBuffer<float>* sc = nullptr;
    int scChannels = 0;
    if (getBusCount (true) > 1)
    {
        if (auto* bus = getBus (true, 1); bus != nullptr && bus->isEnabled())
        {
            scBuffer = getBusBuffer (buffer, true, 1);   // refers to existing channel data, no allocation
            scChannels = scBuffer.getNumChannels();
            if (scChannels > 0) sc = &scBuffer;
        }
    }

    KickState state;
    state.invert = pInvert->load() > 0.5f;
    state.delayOn = pDelayOn->load() > 0.5f;
    state.delayMs = (double) pDelayMs->load();
    state.rotateDeg = (double) pRotate->load();
    state.mode = pMode->load() > 0.5f ? Mode::overlap : Mode::hit;
    state.scChannels = scChannels;

    kick.setTargets (state.invert, state.effectiveDelayMs(), state.rotateDeg);

    TransportInfo transport;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            transport.playing = pos->getIsPlaying();
            if (auto bpm = pos->getBpm()) { transport.hasTempo = *bpm > 0; transport.bpm = *bpm; }
            if (auto ppq = pos->getPpqPosition()) { transport.hasPpq = true; transport.ppq = *ppq; }
            if (auto bar = pos->getPpqPositionOfLastBarStart()) { transport.hasBarStart = true; transport.ppqBarStart = *bar; }
            if (auto ts = pos->getTimeSignature()) { transport.numerator = ts->numerator; transport.denominator = ts->denominator; }
        }
    }

    for (int start = 0; start < numSamples; start += maxChunk)
    {
        const int n = std::min (maxChunk, numSamples - start);
        TransportInfo t = transport;
        if (t.hasPpq && t.hasTempo && start > 0)
            t.ppq += (double) start / (currentRate * 60.0 / t.bpm);
        processChunk (main, sc, start, n, t, state);
    }
}

void PhaseCheckProcessor::processChunk (juce::AudioBuffer<float>& main, const juce::AudioBuffer<float>* sc,
                                        int start, int n, const TransportInfo& transport, const KickState& state)
{
    const int numCh = main.getNumChannels();
    if (numCh == 0 || rawMono.size() < (size_t) n)
        return;

    auto monoSum = [n, start] (const juce::AudioBuffer<float>& b, float* dest)
    {
        const int chs = b.getNumChannels();
        const float g = chs > 1 ? 0.5f : 1.0f;
        for (int i = 0; i < n; ++i)
        {
            float s = 0.0f;
            for (int ch = 0; ch < std::min (chs, 2); ++ch) s += b.getReadPointer (ch)[start + i];
            dest[i] = s * g;
        }
    };

    monoSum (main, rawMono.data());

    if (sc != nullptr) monoSum (*sc, scMono.data());
    else std::fill (scMono.begin(), scMono.begin() + n, 0.0f);

    float* chans[KickProcessor::maxChannels] = {};
    const int kch = std::min (numCh, KickProcessor::maxChannels);
    for (int ch = 0; ch < kch; ++ch) chans[ch] = main.getWritePointer (ch) + start;
    kick.process (chans, kch, n);

    monoSum (main, procMono.data());

    engine.push (procMono.data(), rawMono.data(), scMono.data(), n, transport, state);
}

//==============================================================================
void PhaseCheckProcessor::parameterChanged (const juce::String& id, float newValue)
{
    if (id == ParamID::delayOn && newValue > 0.5f && ! restoringState.load())
    {
        delayOnTurnedOn = true;
        triggerAsyncUpdate();
    }
}

void PhaseCheckProcessor::handleAsyncUpdate()
{
    if (! delayOnTurnedOn.exchange (false))
        return;

    // "Delay kick X ms": when switched on, set the delay to the measured offset
    // (only if it is clear and the sidechain is later than the kick).
    const auto snap = getSnapshot();
    if (snap == nullptr || snap->hit.stale || ! snap->hit.predictedDelayValid)
        return;
    if (snap->state.mode != Mode::hit)
        return;

    if (auto* p = apvts.getParameter (ParamID::delayMs))
    {
        const float ms = (float) juce::jlimit (0.0, 20.0, snap->hit.predictedDelayMs);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (ms));
        p->endChangeGesture();
    }
}

void PhaseCheckProcessor::resetAll()
{
    engine.requestReset();
    for (auto* id : { ParamID::invert, ParamID::delayOn, ParamID::delayMs, ParamID::rotate })
    {
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
        }
    }
}

double PhaseCheckProcessor::getUiScale() const
{
    return juce::jlimit (0.5, 2.0, (double) apvts.state.getProperty (uiScaleId, 1.0));
}

void PhaseCheckProcessor::setUiScale (double s)
{
    apvts.state.setProperty (uiScaleId, juce::jlimit (0.5, 2.0, s), nullptr);
}

//==============================================================================
void PhaseCheckProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PhaseCheckProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            restoringState = true;
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            if (! apvts.state.hasProperty (uiScaleId))
                apvts.state.setProperty (uiScaleId, 1.0, nullptr);
            restoringState = false;
        }
    }
}

juce::AudioProcessorEditor* PhaseCheckProcessor::createEditor()
{
    return new PhaseCheckEditor (*this);
}

} // namespace pc

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new pc::PhaseCheckProcessor();
}
