#include "PluginEditor.h"

namespace
{
const char* kNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

void setFont (juce::Graphics& g, float height, bool bold = false)
{
    auto f = g.getCurrentFont().withHeight (height);
    g.setFont (bold ? f.boldened() : f);
}

float textWidth (const juce::Font& f, const juce::String& s)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (f, s, 0.f, 0.f);
    return ga.getBoundingBox (0, -1, true).getWidth();
}

void caption (juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r,
              juce::Justification j = juce::Justification::centred)
{
    g.setColour (tfui::dim);
    setFont (g, 12.5f, true);
    g.drawText (s, r, j, false);
}
}

//==============================================================================
TuneLookAndFeel::TuneLookAndFeel()
{
    setColour (juce::ComboBox::textColourId, tfui::text);
    setColour (juce::PopupMenu::backgroundColourId, tfui::field);
    setColour (juce::PopupMenu::textColourId, tfui::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, tfui::accent.darker (0.5f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::Slider::textBoxTextColourId, tfui::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, tfui::text);
    setColour (juce::TextEditor::highlightColourId, tfui::accent.withAlpha (0.4f));
}

void TuneLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                        float startAngle, float endAngle, juce::Slider& s)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto colour = s.findColour (juce::Slider::rotarySliderFillColourId);
    const float arcW = juce::jmax (3.f, radius * 0.085f);
    const float arcR = radius - arcW;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const juce::PathStrokeType thin (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    const juce::PathStrokeType wide (arcW * 2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path trackPath;
    trackPath.addCentredArc (centre.x, centre.y, arcR, arcR, 0.f, startAngle, endAngle, true);
    g.setColour (tfui::track);
    g.strokePath (trackPath, thin);

    if (pos > 0.002f)
    {
        juce::Path valuePath;
        valuePath.addCentredArc (centre.x, centre.y, arcR, arcR, 0.f, startAngle, angle, true);
        g.setColour (colour.withAlpha (0.22f));
        g.strokePath (valuePath, wide);
        g.setColour (colour);
        g.strokePath (valuePath, thin);
    }

    const float bodyR = radius - arcW * 2.6f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff46555a), centre.x, centre.y - bodyR,
                                             juce::Colour (0xff10171a), centre.x, centre.y + bodyR, false));
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.f, bodyR * 2.f);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.f, bodyR * 2.f, 1.f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.7f, -bodyR + 4.f, 3.4f, bodyR * 0.34f, 1.7f);
    g.setColour (juce::Colours::white.withAlpha (0.92f));
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
}

void TuneLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.f);
    const auto id = b.getComponentID();
    const bool on = b.getToggleState();

    if (id == "power")
    {
        const float d = juce::jmin (r.getWidth(), r.getHeight());
        auto c = r.withSizeKeepingCentre (d, d);
        g.setColour (on ? tfui::accent : tfui::field);
        g.fillEllipse (c);
        g.setColour (on ? tfui::accent.brighter (0.3f) : tfui::line);
        g.drawEllipse (c, 1.2f);
        if (on)
        {
            g.setColour (tfui::accent.withAlpha (0.25f));
            g.drawEllipse (c.expanded (3.f), 2.f);
        }
        const float rr = d * 0.22f;
        juce::Path p;
        p.addCentredArc (c.getCentreX(), c.getCentreY(), rr, rr, 0.f, 0.65f,
                         juce::MathConstants<float>::twoPi - 0.65f, true);
        p.startNewSubPath (c.getCentreX(), c.getCentreY() - rr * 1.15f);
        p.lineTo (c.getCentreX(), c.getCentreY() - rr * 0.1f);
        g.setColour (on ? juce::Colour (0xff062a25) : tfui::dim);
        g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else if (id == "mode")   // Modern | Classic
    {
        g.setColour (tfui::field);
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (tfui::line);
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.f);

        auto half = r.withWidth (r.getWidth() * 0.5f);
        auto sel = on ? half.withX (r.getCentreX()) : half;
        g.setColour (tfui::accent.withAlpha (0.22f));
        g.fillRoundedRectangle (sel.reduced (2.f), (r.getHeight() - 4.f) * 0.5f);
        setFont (g, 13.f, true);
        g.setColour (on ? tfui::dim : tfui::accent);
        g.drawText ("Modern", half, juce::Justification::centred, false);
        g.setColour (on ? tfui::accent : tfui::dim);
        g.drawText ("Classic", half.withX (r.getCentreX()), juce::Justification::centred, false);
    }
    else   // pill with LED
    {
        g.setColour (highlighted ? tfui::track : tfui::field);
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (on ? tfui::accent.withAlpha (0.7f) : tfui::line);
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.f);
        const float led = 8.f;
        auto ledR = juce::Rectangle<float> (r.getX() + 12.f, r.getCentreY() - led * 0.5f, led, led);
        if (on)
        {
            g.setColour (tfui::accent.withAlpha (0.3f));
            g.fillEllipse (ledR.expanded (3.f));
        }
        g.setColour (on ? tfui::accent : tfui::dim.withAlpha (0.5f));
        g.fillEllipse (ledR);
        setFont (g, 13.f, true);
        g.setColour (on ? tfui::text : tfui::dim);
        g.drawText (b.getButtonText(), r.withTrimmedLeft (28.f), juce::Justification::centredLeft, false);
    }
}

void TuneLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int,
                                    juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0.f, 0.f, (float) width, (float) height).reduced (0.5f);
    g.setColour (tfui::field);
    g.fillRoundedRectangle (r, 8.f);
    g.setColour (box.hasKeyboardFocus (true) ? tfui::accent : tfui::line);
    g.drawRoundedRectangle (r, 8.f, 1.f);

    juce::Path arrow;
    const float ax = (float) width - 15.f, ay = (float) height * 0.5f;
    arrow.startNewSubPath (ax - 4.f, ay - 2.f);
    arrow.lineTo (ax, ay + 2.5f);
    arrow.lineTo (ax + 4.f, ay - 2.f);
    g.setColour (tfui::dim);
    g.strokePath (arrow, juce::PathStrokeType (1.6f));
}

//==============================================================================
void PitchView::update (bool voiced, int targetMidi, float cents, float vibSemis, float zoneCents, float tuningHz)
{
    isVoiced = voiced;
    if (voiced) targetNote = targetMidi;
    smoothCents += 0.35f * ((voiced ? juce::jlimit (-100.f, 100.f, cents) : 0.f) - smoothCents);
    zone = zoneCents;
    tuning = tuningHz;
    vibHistory[(size_t) vibPos] = voiced ? vibSemis : 0.f;
    vibPos = (vibPos + 1) % kHistory;
    repaint();
}

void PitchView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    // --- detune read-out (top left)
    g.setColour (tfui::accent.withAlpha (isVoiced ? 1.f : 0.3f));
    g.fillEllipse (4.f, 8.f, 7.f, 7.f);
    setFont (g, 12.5f, true);
    g.setColour (tfui::dim);
    g.drawText ("Pitch", juce::Rectangle<float> (16.f, 3.f, 80.f, 18.f), juce::Justification::centredLeft, false);
    g.setColour (tfui::text);
    setFont (g, 13.f, false);
    g.drawText ("A4 = " + juce::String (tuning, 1) + " Hz", juce::Rectangle<float> (4.f, 56.f, 130.f, 18.f),
                juce::Justification::centredLeft, false);

    // --- vibrato graph (top right)
    auto plot = juce::Rectangle<float> (b.getRight() - 150.f, 26.f, 140.f, 46.f);
    g.setColour (tfui::dim);
    setFont (g, 12.f, true);
    g.drawText ("Vibrato Graph", plot.withY (4.f).withHeight (18.f), juce::Justification::centredRight, false);
    g.setColour (tfui::line);
    g.drawLine (plot.getX(), plot.getCentreY(), plot.getRight(), plot.getCentreY(), 1.f);

    juce::Path wave;
    for (int i = 0; i < kHistory; ++i)
    {
        const float v = vibHistory[(size_t) ((vibPos + i) % kHistory)];
        const float x = plot.getX() + plot.getWidth() * (float) i / (float) (kHistory - 1);
        const float y = plot.getCentreY() - juce::jlimit (-0.8f, 0.8f, v) / 0.8f * plot.getHeight() * 0.5f;
        if (i == 0) wave.startNewSubPath (x, y); else wave.lineTo (x, y);
    }
    g.setColour (tfui::accent.withAlpha (0.9f));
    g.strokePath (wave, juce::PathStrokeType (1.4f));

    // --- big note
    const int pc = ((targetNote % 12) + 12) % 12;
    const juce::String name = isVoiced ? juce::String (kNames[pc]).substring (0, 1) : juce::String ("-");
    const bool sharp = isVoiced && juce::String (kNames[pc]).length() > 1;

    g.setFont (g.getCurrentFont().withHeight (112.f));
    const float letterW = textWidth (g.getCurrentFont(), name);
    const float totalW = letterW + (sharp ? 40.f : 0.f);
    const float lx = b.getCentreX() - totalW * 0.5f;
    g.setColour (isVoiced ? tfui::text : tfui::dim.withAlpha (0.5f));
    g.drawText (name, juce::Rectangle<float> (lx, 8.f, letterW + 4.f, 124.f), juce::Justification::centred, false);
    if (sharp)
    {
        setFont (g, 52.f);
        g.drawText ("#", juce::Rectangle<float> (lx + letterW - 2.f, 8.f, 46.f, 60.f), juce::Justification::centred, false);
    }

    // --- cents meter
    auto track = juce::Rectangle<float> (4.f, 138.f, b.getWidth() - 8.f, 20.f);
    g.setColour (juce::Colour (0xff09131a));
    g.fillRoundedRectangle (track, 10.f);
    g.setColour (tfui::line);
    g.drawRoundedRectangle (track, 10.f, 1.f);

    const float l = track.getX() + 14.f, r = track.getRight() - 14.f;
    auto xFor = [l, r] (float c) { return juce::jmap (c, -100.f, 100.f, l, r); };

    for (float c : { -100.f, -50.f, 0.f, 50.f, 100.f })
    {
        g.setColour (tfui::line);
        g.drawLine (xFor (c), track.getBottom() + 3.f, xFor (c), track.getBottom() + 8.f, 1.f);
        g.setColour (tfui::dim);
        setFont (g, 11.f);
        const juce::String t = c > 0 ? "+" + juce::String ((int) c) : juce::String ((int) c);
        g.drawText (t, juce::Rectangle<float> (xFor (c) - 20.f, track.getBottom() + 8.f, 40.f, 14.f),
                    juce::Justification::centred, false);
    }

    if (zone > 0.5f)   // Flex-Tune window
    {
        g.setColour (tfui::yellow);
        for (float s : { -1.f, 1.f })
        {
            const float x = xFor (s * zone);
            g.drawLine (x, track.getY() + 3.f, x, track.getBottom() - 3.f, 2.f);
        }
    }

    const float mx = xFor (smoothCents);
    auto marker = juce::Rectangle<float> (mx - 7.f, track.getY() + 2.f, 14.f, track.getHeight() - 4.f);
    g.setColour (tfui::accent.withAlpha (isVoiced ? 0.28f : 0.08f));
    g.fillRoundedRectangle (marker.expanded (3.f, 1.f), 6.f);
    g.setColour (isVoiced ? tfui::accent : tfui::dim.withAlpha (0.4f));
    g.fillRoundedRectangle (marker, 5.f);
}

//==============================================================================
juce::Rectangle<float> NoteKeys::keyRect (int pc) const
{
    static const int whitePcs[7] = { 0, 2, 4, 5, 7, 9, 11 };
    const float ww = (float) getWidth() / 7.f;
    const float h = (float) getHeight();

    for (int i = 0; i < 7; ++i)
        if (whitePcs[i] == pc)
            return { ww * (float) i + 1.5f, 0.f, ww - 3.f, h };

    // black keys sit on the boundary after white key i
    static const int blackAfter[12] = { -1, 0, -1, 1, -1, -1, 3, -1, 4, -1, 5, -1 };
    const int i = blackAfter[pc];
    const float bw = ww * 0.58f;
    return { ww * (float) (i + 1) - bw * 0.5f, 0.f, bw, h * 0.6f };
}

int NoteKeys::keyAt (juce::Point<float> p) const
{
    static const int blackPcs[5] = { 1, 3, 6, 8, 10 };
    for (int pc : blackPcs)
        if (keyRect (pc).contains (p)) return pc;
    for (int pc : { 0, 2, 4, 5, 7, 9, 11 })
        if (keyRect (pc).contains (p)) return pc;
    return -1;
}

void NoteKeys::paint (juce::Graphics& g)
{
    const auto mask = proc.effectiveMask();

    auto drawKey = [&] (int pc, bool black)
    {
        auto r = keyRect (pc);
        const bool on = mask[(size_t) pc];
        const bool active = pc == activePc;

        juce::Colour fill = black ? (on ? juce::Colour (0xff0f5f58) : juce::Colour (0xff0a1316))
                                  : (on ? juce::Colour (0xff12403f) : juce::Colour (0xff18262b));
        if (active) fill = tfui::accent;

        g.setColour (fill);
        g.fillRoundedRectangle (r, 7.f);
        g.setColour (on ? tfui::accent.withAlpha (active ? 1.f : 0.75f) : tfui::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 7.f, on ? 1.6f : 1.f);

        setFont (g, black ? 11.f : 12.5f, true);
        g.setColour (active ? juce::Colour (0xff062a25) : (on ? tfui::text : tfui::dim.withAlpha (0.7f)));
        g.drawText (kNames[pc], r.withTrimmedBottom (black ? 6.f : 8.f), juce::Justification::centredBottom, false);
    };

    for (int pc : { 0, 2, 4, 5, 7, 9, 11 }) drawKey (pc, false);
    for (int pc : { 1, 3, 6, 8, 10 })       drawKey (pc, true);
}

void NoteKeys::mouseDown (const juce::MouseEvent& e)
{
    const int pc = keyAt (e.position);
    if (pc >= 0)
    {
        proc.toggleNote (pc);
        repaint();
    }
}

//==============================================================================
TuneForgeEditor::TuneForgeEditor (TuneForgeProcessor& p)
    : AudioProcessorEditor (&p), proc (p), keys (p)
{
    setLookAndFeel (&lnf);
    setSize (780, 520);

    addAndMakeVisible (pitchView);
    addAndMakeVisible (keys);

    auto fillCombo = [&] (juce::ComboBox& box, const char* paramId)
    {
        box.addItemList (proc.apvts.getParameter (paramId)->getAllValueStrings(), 1);
        box.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (box);
    };
    fillCombo (rangeBox, "range");
    fillCombo (keyBox, "key");
    fillCombo (scaleBox, "scale");
    fillCombo (latencyBox, "latency");

    setupKnob (retune, tfui::accent, " ms", 30.0);
    setupKnob (humanize, tfui::purple, "", 50.0);
    setupKnob (flex, tfui::yellow, "", 30.0);
    setupKnob (transpose, tfui::accent, " st", 0.0);
    setupKnob (formant, tfui::accent, " st", 0.0);
    setupKnob (cutBoost, tfui::accent, "", 0.0);
    retune.setNumDecimalPlacesToDisplay (0);
    humanize.setNumDecimalPlacesToDisplay (0);
    flex.setNumDecimalPlacesToDisplay (0);
    transpose.setNumDecimalPlacesToDisplay (0);
    formant.setNumDecimalPlacesToDisplay (1);
    cutBoost.setNumDecimalPlacesToDisplay (0);

    classicBtn.setComponentID ("mode");
    powerBtn.setComponentID ("power");
    preserveBtn.setButtonText ("Preserve");
    midiBtn.setButtonText ("MIDI Perform");
    for (auto* b : { &classicBtn, &powerBtn, &preserveBtn, &midiBtn })
    {
        b->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        addAndMakeVisible (*b);
    }

    auto& v = proc.apvts;
    rangeAtt   = std::make_unique<ComboAtt> (v, "range", rangeBox);
    keyAtt     = std::make_unique<ComboAtt> (v, "key", keyBox);
    scaleAtt   = std::make_unique<ComboAtt> (v, "scale", scaleBox);
    latencyAtt = std::make_unique<ComboAtt> (v, "latency", latencyBox);
    retuneAtt    = std::make_unique<SliderAtt> (v, "retune", retune);
    humanizeAtt  = std::make_unique<SliderAtt> (v, "humanize", humanize);
    flexAtt      = std::make_unique<SliderAtt> (v, "flex", flex);
    transposeAtt = std::make_unique<SliderAtt> (v, "transpose", transpose);
    formantAtt   = std::make_unique<SliderAtt> (v, "formant", formant);
    cutBoostAtt  = std::make_unique<SliderAtt> (v, "cutboost", cutBoost);
    classicAtt  = std::make_unique<ButtonAtt> (v, "classic", classicBtn);
    powerAtt    = std::make_unique<ButtonAtt> (v, "power", powerBtn);
    preserveAtt = std::make_unique<ButtonAtt> (v, "preserve", preserveBtn);
    midiAtt     = std::make_unique<ButtonAtt> (v, "midiperform", midiBtn);

    startTimerHz (30);
}

TuneForgeEditor::~TuneForgeEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void TuneForgeEditor::setupKnob (juce::Slider& s, juce::Colour colour, const juce::String& suffix, double defaultValue)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 20);
    s.setColour (juce::Slider::rotarySliderFillColourId, colour);
    s.setTextValueSuffix (suffix);
    s.setDoubleClickReturnValue (true, defaultValue);
    s.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible (s);
}

void TuneForgeEditor::timerCallback()
{
    const auto& e = proc.displayEngine();
    const bool voiced = e.uiVoiced.load();
    const float zoneCents = (classicBtn.getToggleState() ? 0.f : flex.getValue() / 100.0 * 50.0);

    pitchView.update (voiced, (int) std::lround (e.uiTarget.load()), e.uiCents.load(), e.uiVib.load(),
                      zoneCents, (float) proc.apvts.getRawParameterValue ("tuning")->load());

    keys.setActiveNote (voiced ? (((int) std::lround (e.uiTarget.load()) % 12) + 12) % 12 : -1);
    keys.repaint();

    const int lat = proc.currentLatencySamples();
    if (lat != lastLatency)
    {
        lastLatency = lat;
        repaint (headerRect);
    }
}

void TuneForgeEditor::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (tfui::bgTop, 0.f, 0.f, tfui::bgBottom, 0.f, b.getHeight(), false));
    g.fillAll();

    auto body = b.reduced (10.f);
    g.setColour (tfui::panel);
    g.fillRoundedRectangle (body, 22.f);
    g.setColour (tfui::line);
    g.drawRoundedRectangle (body, 22.f, 1.2f);

    // title
    g.setColour (tfui::text);
    setFont (g, 26.f, true);
    g.drawText ("TuneForge", juce::Rectangle<int> (30, 18, 170, 34), juce::Justification::centredLeft, false);
    g.setColour (tfui::accent);
    setFont (g, 11.f, true);
    g.drawText ("PITCH CORRECTION", juce::Rectangle<int> (32, 46, 170, 14), juce::Justification::centredLeft, false);

    // header captions
    caption (g, "Vocal Range", { rangeBox.getX(), 12, rangeBox.getWidth(), 14 }, juce::Justification::centredLeft);
    caption (g, "Key", { keyBox.getX(), 12, keyBox.getWidth(), 14 }, juce::Justification::centredLeft);
    caption (g, "Scale", { scaleBox.getX(), 12, scaleBox.getWidth(), 14 }, juce::Justification::centredLeft);
    caption (g, "Latency Mode", { latencyBox.getX(), 12, latencyBox.getWidth(), 14 }, juce::Justification::centredLeft);

    const double sr = juce::jmax (1.0, proc.getSampleRate());
    g.setColour (tfui::dim);
    setFont (g, 11.5f, true);
    g.drawText ("Latency", juce::Rectangle<int> (688, 22, 70, 14), juce::Justification::centredRight, false);
    g.setColour (tfui::text);
    setFont (g, 13.f, true);
    g.drawText (juce::String (1000.0 * proc.currentLatencySamples() / sr, 1) + " ms",
                juce::Rectangle<int> (688, 36, 70, 18), juce::Justification::centredRight, false);

    g.setColour (tfui::line);
    g.drawLine (22.f, 70.f, b.getRight() - 22.f, 70.f, 1.f);
    g.drawLine (548.f, 82.f, 548.f, b.getBottom() - 24.f, 1.f);
    g.drawLine (22.f, 406.f, 540.f, 406.f, 1.f);

    caption (g, "Edit Notes", { 30, 274, 200, 16 }, juce::Justification::centredLeft);
    caption (g, "Formant Designer", { 30, 412, 220, 16 }, juce::Justification::centredLeft);
    caption (g, "Vibrato", { 300, 412, 120, 16 }, juce::Justification::centredLeft);

    caption (g, "Transpose", { transpose.getX(), 428, transpose.getWidth(), 14 });
    caption (g, "Formant", { formant.getX(), 428, formant.getWidth(), 14 });
    caption (g, "Cut/Boost", { cutBoost.getX(), 428, cutBoost.getWidth(), 14 });

    caption (g, "Retune Speed", { retune.getX(), 82, retune.getWidth(), 16 });
    caption (g, "Humanize", { humanize.getX(), 322, humanize.getWidth(), 16 });
    caption (g, "Flex Tune", { flex.getX(), 322, flex.getWidth(), 16 });
}

void TuneForgeEditor::resized()
{
    headerRect = { 680, 14, 80, 50 };

    rangeBox.setBounds   (232, 28, 130, 28);
    keyBox.setBounds     (372, 28, 62, 28);
    scaleBox.setBounds   (442, 28, 140, 28);
    latencyBox.setBounds (592, 28, 88, 28);

    pitchView.setBounds (28, 80, 512, 186);
    keys.setBounds (28, 296, 420, 100);
    midiBtn.setBounds (458, 322, 84, 28);

    transpose.setBounds (30, 438, 82, 76);
    formant.setBounds   (126, 438, 82, 76);
    preserveBtn.setBounds (300, 440, 100, 28);
    cutBoost.setBounds  (420, 438, 82, 76);

    retune.setBounds (572, 102, 176, 208);
    humanize.setBounds (562, 338, 100, 96);
    flex.setBounds (658, 338, 100, 96);

    classicBtn.setBounds (572, 456, 130, 28);
    powerBtn.setBounds (712, 450, 40, 40);
}
