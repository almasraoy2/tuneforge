#pragma once

#include <JuceHeader.h>
#include "PitchCorrector.h"

class TuneForgeProcessor : public juce::AudioProcessor,
                           private juce::AsyncUpdater
{
public:
    static constexpr int kMaxChannels = 2;

    TuneForgeProcessor();
    ~TuneForgeProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "TuneForge"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- shared with the editor ----
    juce::AudioProcessorValueTreeState apvts;

    std::array<bool, 12> effectiveMask() const;       // notes the engine is allowed to snap to
    void toggleNote (int pitchClass);                  // used by the on-screen keyboard
    const tf::PitchCorrector& displayEngine() const { return engines[0]; }
    int currentLatencySamples() const { return engines[0].getLatency(); }

private:
    void handleAsyncUpdate() override;
    void runBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&, bool forceBypass);
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::array<tf::PitchCorrector, kMaxChannels> engines;

    std::atomic<float>* pRetune = nullptr;
    std::atomic<float>* pHumanize = nullptr;
    std::atomic<float>* pFlex = nullptr;
    std::atomic<float>* pClassic = nullptr;
    std::atomic<float>* pRange = nullptr;
    std::atomic<float>* pKey = nullptr;
    std::atomic<float>* pScale = nullptr;
    std::atomic<float>* pLatencyMode = nullptr;
    std::atomic<float>* pTranspose = nullptr;
    std::atomic<float>* pFormant = nullptr;
    std::atomic<float>* pPreserve = nullptr;
    std::atomic<float>* pCutBoost = nullptr;
    std::atomic<float>* pTuning = nullptr;
    std::atomic<float>* pPower = nullptr;
    std::atomic<float>* pMidiPerform = nullptr;
    std::array<std::atomic<float>*, 12> pNote {};

    std::array<bool, 128> heldNotes {};
    int appliedRange = -1, appliedLatencyMode = -1;
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TuneForgeProcessor)
};
