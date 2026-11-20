#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace tfui
{
inline const juce::Colour bgTop    { 0xff0a1316 };
inline const juce::Colour bgBottom { 0xff10202a };
inline const juce::Colour panel    { 0xff0d1a1f };
inline const juce::Colour field    { 0xff15262c };
inline const juce::Colour track    { 0xff1d3036 };
inline const juce::Colour line     { 0xff224046 };
inline const juce::Colour text     { 0xffe8f4f2 };
inline const juce::Colour dim      { 0xff7d9a9b };
inline const juce::Colour accent   { 0xff2de2c0 };
inline const juce::Colour purple   { 0xff8b6cff };
inline const juce::Colour yellow   { 0xffe9e24a };
}

//==============================================================================
class TuneLookAndFeel : public juce::LookAndFeel_V4
{
public:
    TuneLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool isDown, int bx, int by, int bw, int bh,
                       juce::ComboBox&) override;
};

//==============================================================================
// Big note readout, pitch meter and vibrato graph
class PitchView : public juce::Component
{
public:
    void update (bool voiced, int targetMidi, float cents, float vibSemis, float zoneCents, float tuningHz);
    void paint (juce::Graphics&) override;

private:
    static constexpr int kHistory = 140;
    std::array<float, kHistory> vibHistory {};
    int vibPos = 0;
    bool isVoiced = false;
    int targetNote = 60;
    float smoothCents = 0.f, zone = 0.f, tuning = 440.f;
};

//==============================================================================
// One-octave keyboard: click a note to allow / forbid it as a correction target
class NoteKeys : public juce::Component
{
public:
    explicit NoteKeys (TuneForgeProcessor& p) : proc (p) {}
    void setActiveNote (int pitchClass) { activePc = pitchClass; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> keyRect (int pitchClass) const;
    int keyAt (juce::Point<float>) const;

    TuneForgeProcessor& proc;
    int activePc = -1;
};

//==============================================================================
class TuneForgeEditor : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    explicit TuneForgeEditor (TuneForgeProcessor&);
    ~TuneForgeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setupKnob (juce::Slider&, juce::Colour colour, const juce::String& suffix, double defaultValue);

    TuneForgeProcessor& proc;
    TuneLookAndFeel lnf;

    PitchView pitchView;
    NoteKeys keys;

    juce::ComboBox rangeBox, keyBox, scaleBox, latencyBox;
    juce::Slider retune, humanize, flex, transpose, formant, cutBoost;
    juce::ToggleButton classicBtn, powerBtn, preserveBtn, midiBtn;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<ComboAtt>  rangeAtt, keyAtt, scaleAtt, latencyAtt;
    std::unique_ptr<SliderAtt> retuneAtt, humanizeAtt, flexAtt, transposeAtt, formantAtt, cutBoostAtt;
    std::unique_ptr<ButtonAtt> classicAtt, powerAtt, preserveAtt, midiAtt;

    juce::Rectangle<int> headerRect;
    int lastLatency = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TuneForgeEditor)
};
