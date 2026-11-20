#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
struct RangeInfo { const char* name; float minHz, maxHz; };
const RangeInfo kRanges[] = {
    { "Soprano",     140.f, 1400.f },
    { "Alto/Tenor",   80.f,  800.f },
    { "Low Male",     55.f,  500.f },
    { "Instrument",   55.f, 1600.f },
};

const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

// scale degrees (semitones above the key)
const std::vector<std::vector<int>> kScaleDegrees = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },   // Chromatic
    { 0, 2, 4, 5, 7, 9, 11 },                   // Major
    { 0, 2, 3, 5, 7, 8, 10 },                   // Minor
    { 0, 2, 4, 7, 9 },                          // Major Pentatonic
    { 0, 3, 5, 7, 10 },                         // Minor Pentatonic
};
constexpr int kCustomScale = 5;
}

juce::AudioProcessorValueTreeState::ParameterLayout TuneForgeProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "retune", 1 }, "Retune Speed",
                    NormalisableRange<float> (0.f, 400.f, 0.1f, 0.45f), 30.f,
                    AudioParameterFloatAttributes().withLabel ("ms")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "humanize", 1 }, "Humanize",
                    NormalisableRange<float> (0.f, 100.f, 0.1f), 50.f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "flex", 1 }, "Flex Tune",
                    NormalisableRange<float> (0.f, 100.f, 0.1f), 30.f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "classic", 1 }, "Classic Mode", false));

    StringArray ranges;
    for (auto& r : kRanges) ranges.add (r.name);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "range", 1 }, "Vocal Range", ranges, 1));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, "Key",
                    StringArray (kNoteNames, 12), 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Scale",
                    StringArray { "Chromatic", "Major", "Minor", "Major Pentatonic", "Minor Pentatonic", "Custom" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "latency", 1 }, "Latency Mode",
                    StringArray { "Live", "Studio" }, 0));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "transpose", 1 }, "Transpose",
                    NormalisableRange<float> (-12.f, 12.f, 1.f), 0.f,
                    AudioParameterFloatAttributes().withLabel ("st")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "formant", 1 }, "Formant",
                    NormalisableRange<float> (-tf::kFormantMaxSt, tf::kFormantMaxSt, 0.1f), 0.f,
                    AudioParameterFloatAttributes().withLabel ("st")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "preserve", 1 }, "Vibrato Preserve", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "cutboost", 1 }, "Vibrato Cut/Boost",
                    NormalisableRange<float> (-100.f, 100.f, 1.f), 0.f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "tuning", 1 }, "Reference Tuning",
                    NormalisableRange<float> (415.f, 466.f, 0.1f), 440.f,
                    AudioParameterFloatAttributes().withLabel ("Hz")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "power", 1 }, "Power", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "midiperform", 1 }, "MIDI Perform", false));

    for (int i = 0; i < 12; ++i)
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "note" + String (i), 1 },
                                                          String ("Note ") + kNoteNames[i], true));
    return layout;
}

TuneForgeProcessor::TuneForgeProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "TuneForgeState", createLayout())
{
    pRetune = apvts.getRawParameterValue ("retune");
    pHumanize = apvts.getRawParameterValue ("humanize");
    pFlex = apvts.getRawParameterValue ("flex");
    pClassic = apvts.getRawParameterValue ("classic");
    pRange = apvts.getRawParameterValue ("range");
    pKey = apvts.getRawParameterValue ("key");
    pScale = apvts.getRawParameterValue ("scale");
    pLatencyMode = apvts.getRawParameterValue ("latency");
    pTranspose = apvts.getRawParameterValue ("transpose");
    pFormant = apvts.getRawParameterValue ("formant");
    pPreserve = apvts.getRawParameterValue ("preserve");
    pCutBoost = apvts.getRawParameterValue ("cutboost");
    pTuning = apvts.getRawParameterValue ("tuning");
    pPower = apvts.getRawParameterValue ("power");
    pMidiPerform = apvts.getRawParameterValue ("midiperform");
    for (int i = 0; i < 12; ++i)
        pNote[(size_t) i] = apvts.getRawParameterValue ("note" + juce::String (i));
}

bool TuneForgeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

std::array<bool, 12> TuneForgeProcessor::effectiveMask() const
{
    std::array<bool, 12> mask {};
    const int scaleIdx = (int) pScale->load();

    if (scaleIdx >= kCustomScale)
    {
        for (size_t i = 0; i < 12; ++i)
            mask[i] = pNote[i]->load() > 0.5f;
    }
    else
    {
        const int root = (int) pKey->load();
        for (int d : kScaleDegrees[(size_t) scaleIdx])
            mask[(size_t) ((root + d) % 12)] = true;
    }
    return mask;
}

void TuneForgeProcessor::toggleNote (int pc)
{
    auto current = effectiveMask();
    if (pScale->load() < (float) kCustomScale)
    {
        // switching to a custom scale: start from what is currently active
        for (int i = 0; i < 12; ++i)
            if (auto* p = apvts.getParameter ("note" + juce::String (i)))
                p->setValueNotifyingHost (current[(size_t) i] ? 1.f : 0.f);
        if (auto* s = apvts.getParameter ("scale"))
            s->setValueNotifyingHost (s->convertTo0to1 ((float) kCustomScale));
    }
    if (auto* p = apvts.getParameter ("note" + juce::String (pc)))
        p->setValueNotifyingHost (current[(size_t) pc] ? 0.f : 1.f);
}

void TuneForgeProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    appliedRange = (int) pRange->load();
    appliedLatencyMode = (int) pLatencyMode->load();
    const auto& r = kRanges[(size_t) juce::jlimit (0, 3, appliedRange)];
    const int extra = appliedLatencyMode == 1 ? 1024 : 0;

    for (auto& e : engines)
    {
        e.prepare (sampleRate);
        e.configure (r.minHz, r.maxHz, extra);
        e.reset();
    }
    heldNotes.fill (false);
    setLatencySamples (engines[0].getLatency());
}

void TuneForgeProcessor::handleAsyncUpdate()
{
    setLatencySamples (engines[0].getLatency());
}

void TuneForgeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    runBlock (buffer, midi, false);
}

void TuneForgeProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    runBlock (buffer, midi, true);   // keeps the audio delayed by the same latency
}

void TuneForgeProcessor::runBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool forceBypass)
{
    juce::ScopedNoDenormals noDenormals;

    // --- MIDI perform: held notes become the only allowed targets ---
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())       heldNotes[(size_t) msg.getNoteNumber()] = true;
        else if (msg.isNoteOff()) heldNotes[(size_t) msg.getNoteNumber()] = false;
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) heldNotes.fill (false);
    }

    // --- range / latency changes ---
    const int range = (int) pRange->load();
    const int latMode = (int) pLatencyMode->load();
    if (range != appliedRange || latMode != appliedLatencyMode)
    {
        appliedRange = range;
        appliedLatencyMode = latMode;
        const auto& r = kRanges[(size_t) juce::jlimit (0, 3, range)];
        for (auto& e : engines)
        {
            e.configure (r.minHz, r.maxHz, latMode == 1 ? 1024 : 0);
            e.reset();
        }
        triggerAsyncUpdate();   // tell the host about the new latency
    }

    // --- parameters -> engine ---
    tf::Params p;
    p.retuneMs = pRetune->load();
    p.humanize = pHumanize->load();
    p.flex = pFlex->load();
    p.classic = pClassic->load() > 0.5f;
    p.transposeSt = pTranspose->load();
    p.formantSt = pFormant->load();
    p.vibratoPreserve = pPreserve->load() > 0.5f;
    p.vibratoCutBoost = pCutBoost->load();
    p.tuningHz = pTuning->load();
    p.bypass = forceBypass || pPower->load() < 0.5f;
    p.scale = effectiveMask();

    if (pMidiPerform->load() > 0.5f)
    {
        std::array<bool, 12> held {};
        bool any = false;
        for (size_t i = 0; i < 128; ++i)
            if (heldNotes[i]) { held[i % 12] = true; any = true; }
        if (any) p.scale = held;
    }

    const int numCh = juce::jmin (buffer.getNumChannels(), kMaxChannels);
    for (int ch = 0; ch < numCh; ++ch)
    {
        engines[(size_t) ch].setParams (p);
        auto* data = buffer.getWritePointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            data[i] = engines[(size_t) ch].process (data[i]);
    }

    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
}

juce::AudioProcessorEditor* TuneForgeProcessor::createEditor()
{
    return new TuneForgeEditor (*this);
}

void TuneForgeProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void TuneForgeProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TuneForgeProcessor();
}
