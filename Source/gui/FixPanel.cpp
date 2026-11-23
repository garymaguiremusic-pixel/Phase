#include "FixPanel.h"
#include "../PluginProcessor.h"

namespace pc::ui
{

namespace
{
    juce::String msText (double v)
    {
        auto s = juce::String (v, 2);
        if (s.endsWithChar ('0')) s = s.dropLastCharacters (1);
        return s;
    }
}

FixPanel::FixPanel (juce::AudioProcessorValueTreeState& state) : apvts (state)
{
    using APVTS = juce::AudioProcessorValueTreeState;

    addAndMakeVisible (invert);
    addAndMakeVisible (delay);
    addAndMakeVisible (knob);
    addAndMakeVisible (slider);

    invertAtt = std::make_unique<APVTS::ButtonAttachment> (apvts, ParamID::invert, invert);
    delayAtt = std::make_unique<APVTS::ButtonAttachment> (apvts, ParamID::delayOn, delay);
    knobAtt = std::make_unique<APVTS::SliderAttachment> (apvts, ParamID::rotate, knob);
    sliderAtt = std::make_unique<APVTS::SliderAttachment> (apvts, ParamID::rotate, slider);

    invert.setTooltip ("Flip the kick's polarity (multiply by -1). Click-free.");

    for (auto* s : { &knob, &slider })
    {
        s->setDoubleClickReturnValue (true, 0.0);
        s->setScrollWheelEnabled (true);
        s->setWantsKeyboardFocus (true);
        s->setTitle ("Phase rotate");
        s->setTooltip ("Phase rotate: shifts the kick's low-end phase without moving its timing. Double-click resets to 0"
                       + sym::degree + ".");
        s->onValueChange = [this] { repaint (rotateLabelArea); };
    }
    // the knob sweeps -135 .. +135 degrees for 0 .. 180
    knob.setRotaryParameters (juce::degreesToRadians (-135.0f) + juce::MathConstants<float>::twoPi,
                              juce::degreesToRadians (135.0f) + juce::MathConstants<float>::twoPi, true);
    knob.setMouseDragSensitivity (220);
    knob.setVelocityBasedMode (false);

    setTitle ("Fix it on the kick");
    setFocusContainerType (FocusContainerType::focusContainer);
}

FixPanel::~FixPanel() = default;

void FixPanel::setOverlapMode (bool isOverlap)
{
    if (overlap == isOverlap) return;
    overlap = isOverlap;
    delay.setVisible (! overlap);
    resized();
    repaint();
}

void FixPanel::update (const Snapshot& snap, bool live)
{
    const auto& h = snap.hit;
    const bool on = apvts.getRawParameterValue (ParamID::delayOn)->load() > 0.5f;
    const double setMs = apvts.getRawParameterValue (ParamID::delayMs)->load();

    juce::String label = "Delay kick";
    juce::String reason;
    bool canEnable = true;

    if (on)
        label << " " << msText (setMs) << " ms";
    else if (! live || h.stale)
    {
        canEnable = false;
        reason = "No measurement yet: needs a live sidechain and kick hits.";
    }
    else if (! h.offsetClear)
    {
        canEnable = false;
        reason = "Offset unclear: the kick and sidechain hits don't line up clearly enough to measure.";
    }
    else if (h.offsetMs < -0.005)
    {
        canEnable = false;
        label << " " << msText (std::abs (h.offsetMs)) << " ms";
        reason = "The sidechain arrives earlier than the kick. The kick can't be moved earlier: "
                 "delay the sidechain track in Studio Pro instead (Track Delay).";
    }
    else if (! h.predictedDelayValid)
    {
        canEnable = false;
        reason = "Nothing to delay: the onsets already line up.";
    }
    else
        label << " " << msText (h.predictedDelayMs) << " ms";

    delay.setTitleText (label);
    delay.setEnabled (on || canEnable);
    delay.setTooltip (on ? "The kick is delayed to line up with the sidechain. Click to turn off."
                         : (canEnable ? "Delay the kick by the measured offset so both hits start together." : reason));

    const auto& text = overlap ? snap.overlap.suggestion : snap.hit.suggestion;
    const auto newText = juce::String::fromUTF8 (text.c_str());
    if (newText != suggestion || live != suggestionLive)
    {
        suggestion = newText;
        suggestionLive = live;
        repaint (suggestionArea);
    }
}

void FixPanel::resized()
{
    auto r = getLocalBounds().reduced (18);
    titleArea = r.removeFromTop (20);
    r.removeFromTop (overlap ? 10 : 14);

    if (overlap)
    {
        dashedRow = r.removeFromTop (44);
        r.removeFromTop (8);
        invert.setBounds (r.removeFromTop (56));
        r.removeFromTop (8);
    }
    else
    {
        dashedRow = {};
        invert.setBounds (r.removeFromTop (56));
        r.removeFromTop (10);
        delay.setBounds (r.removeFromTop (56));
        r.removeFromTop (12);
    }

    auto row = r.removeFromTop (48);
    knob.setBounds (row.removeFromLeft (44).withSizeKeepingCentre (44, 44));
    row.removeFromLeft (12);
    rotateLabelArea = row.removeFromTop (24);
    slider.setBounds (row.withHeight (20));

    r.removeFromTop (overlap ? 10 : 16);
    suggestionArea = r;
}

void FixPanel::paint (juce::Graphics& g)
{
    drawCard (g, getLocalBounds().toFloat());
    drawCardTitle (g, titleArea, "Fix it on the kick", {});

    if (! dashedRow.isEmpty())
    {
        const auto rf = dashedRow.toFloat().reduced (0.5f);
        drawDashedRect (g, rf, 10.0f, col::btnBorder);
        auto c = dashedRow.reduced (14, 0);
        g.setColour (col::text2);
        g.setFont (Fonts::mono (16.0f, 600));
        g.drawText (sym::mapsTo, c.removeFromLeft (24), juce::Justification::centred, false);
        c.removeFromLeft (10);
        auto t = c.withSizeKeepingCentre (c.getWidth(), 34);
        g.setFont (Fonts::inter (13.0f, 600));
        g.drawText ("Delay kick: not used here", t.removeFromTop (18), juce::Justification::centredLeft, true);
        g.setFont (Fonts::inter (12.0f));
        g.setColour (col::tick.brighter (0.3f));
        g.drawText ("Notes fall on different beats", t, juce::Justification::centredLeft, true);
    }

    // rotate label + value
    {
        auto r = rotateLabelArea;
        g.setFont (Fonts::inter (13.0f, 600));
        g.setColour (col::text);
        g.drawText ("Phase rotate", r, juce::Justification::centredLeft, false);
        g.setFont (Fonts::mono (13.0f, 600));
        g.setColour (knob.getValue() > 0 ? col::blue : col::text2);
        g.drawText (juce::String (juce::roundToInt (knob.getValue())) + sym::degree, r, juce::Justification::centredRight, false);
    }

    // suggestion box
    if (suggestionArea.getHeight() > 30)
    {
        drawInset (g, suggestionArea.toFloat());
        auto r = suggestionArea.reduced (12, 10);
        g.setFont (Fonts::inter (12.0f, 500));
        g.setColour (col::text2);
        g.drawText ("Suggestion", r.removeFromTop (16), juce::Justification::centredLeft, false);
        r.removeFromTop (4);

        // shrink the text until it fits the box
        juce::TextLayout tl;
        for (float size = 13.0f; size >= 10.0f; size -= 0.5f)
        {
            juce::AttributedString as;
            as.setJustification (juce::Justification::topLeft);
            as.setLineSpacing (size >= 12.0f ? 2.0f : 1.0f);
            as.append (suggestion, Fonts::inter (size), suggestionLive ? col::text : col::text2);
            tl.createLayout (as, (float) r.getWidth());
            if (tl.getHeight() <= (float) r.getHeight())
                break;
        }
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (r);
        tl.draw (g, r.toFloat());
    }
}

} // namespace pc::ui
