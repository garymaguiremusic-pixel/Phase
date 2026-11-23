#include "Widgets.h"

namespace pc::ui
{

//==============================================================================
PhaseLookAndFeel::PhaseLookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, col::card);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::blue);
    setColour (juce::PopupMenu::highlightedTextColourId, col::btnOnText);
    setColour (juce::TooltipWindow::backgroundColourId, col::card);
    setColour (juce::TooltipWindow::textColourId, col::text);
    setColour (juce::TooltipWindow::outlineColourId, col::border);
}

void PhaseLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float startAngle, float endAngle, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float d = std::min (bounds.getWidth(), bounds.getHeight());
    const auto face = bounds.withSizeKeepingCentre (d, d).reduced (1.0f);
    const auto c = face.getCentre();
    const float r = face.getWidth() * 0.5f;

    // dark face, ring and the travelled arc
    g.setColour (col::inset);
    g.fillEllipse (face);
    g.setColour (col::btnBorder);
    g.drawEllipse (face, 1.0f);

    const float angle = startAngle + pos * (endAngle - startAngle);
    juce::Path arc;
    arc.addCentredArc (c.x, c.y, r - 3.0f, r - 3.0f, 0.0f, startAngle, angle, true);
    g.setColour (col::blue.withAlpha (s.isEnabled() ? 0.35f : 0.15f));
    g.strokePath (arc, juce::PathStrokeType (2.0f));

    // pointer
    const auto tip = c.getPointOnCircumference (r - 5.0f, angle);
    const auto base = c.getPointOnCircumference (r * 0.25f, angle);
    g.setColour (s.isEnabled() ? col::blue : col::tick);
    g.drawLine ({ base, tip }, 2.5f);

    if (s.hasKeyboardFocus (false))
    {
        g.setColour (col::blue.withAlpha (0.6f));
        g.drawEllipse (face.expanded (1.5f), 1.0f);
    }
}

void PhaseLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos, float,
                                         float, juce::Slider::SliderStyle, juce::Slider& s)
{
    const auto area = juce::Rectangle<int> (x, y, w, h).toFloat();
    const auto track = area.withSizeKeepingCentre (area.getWidth(), 4.0f);
    g.setColour (col::grid2);
    g.fillRoundedRectangle (track, 2.0f);

    g.setColour (col::blue.withAlpha (s.isEnabled() ? 1.0f : 0.4f));
    g.fillRoundedRectangle (track.withRight (sliderPos), 2.0f);

    const auto thumb = juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ sliderPos, track.getCentreY() });
    g.setColour (col::text);
    g.fillEllipse (thumb);
    if (s.hasKeyboardFocus (false))
    {
        g.setColour (col::blue);
        g.drawEllipse (thumb.expanded (2.0f), 1.0f);
    }
}

juce::Rectangle<int> PhaseLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                         juce::Rectangle<int> parentArea)
{
    juce::AttributedString s;
    s.append (tipText, Fonts::inter (13.0f), col::text);
    juce::TextLayout tl;
    tl.createLayout (s, 280.0f);
    const int w = (int) tl.getWidth() + 20, h = (int) tl.getHeight() + 14;
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin (parentArea);
}

void PhaseLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto r = juce::Rectangle<int> (width, height).toFloat();
    g.setColour (col::card);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (col::btnBorder);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    juce::AttributedString s;
    s.append (text, Fonts::inter (13.0f), col::text);
    juce::TextLayout tl;
    tl.createLayout (s, (float) width - 20.0f);
    tl.draw (g, r.reduced (10.0f, 7.0f));
}

//==============================================================================
FlatButton::FlatButton (const juce::String& label, Style s) : juce::Button (label), style (s)
{
    setTitle (label);
    setWantsKeyboardFocus (true);
}

void FlatButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat();
    const bool on = getToggleState();

    if (style == Style::segment)
    {
        if (on)
        {
            g.setColour (col::blue);
            g.fillRoundedRectangle (r, 6.0f);
        }
        else if (highlighted)
        {
            g.setColour (col::btnOff);
            g.fillRoundedRectangle (r, 6.0f);
        }
        g.setColour (on ? col::btnOnText : (highlighted ? col::text : col::text2));
        g.setFont (Fonts::inter (13.0f, on ? 600 : 500));
        g.drawText (getButtonText(), r, juce::Justification::centred, false);
    }
    else
    {
        g.setColour (on ? col::blue : (down ? col::grid2 : col::btnOff));
        g.fillRoundedRectangle (r.reduced (0.5f), 8.0f);
        if (! on)
        {
            g.setColour (highlighted ? col::tick : col::btnBorder);
            g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
        }
        g.setColour (on ? col::btnOnText : col::text);
        g.setFont (Fonts::inter (13.0f, 600));
        g.drawText (getButtonText(), r, juce::Justification::centred, false);
    }

    if (hasKeyboardFocus (false))
    {
        g.setColour (col::blue.withAlpha (0.8f));
        g.drawRoundedRectangle (r.reduced (0.5f), style == Style::segment ? 6.0f : 8.0f, 1.5f);
    }
}

//==============================================================================
BigToggle::BigToggle (const juce::String& i, bool iconInMono, const juce::String& t,
                      const juce::String& off, const juce::String& on)
    : juce::Button (t), icon (i), titleText (t), subOff (off), subOn (on), iconMono (iconInMono)
{
    setClickingTogglesState (true);
    setWantsKeyboardFocus (true);
    setTitle (t);
}

void BigToggle::setTitleText (const juce::String& t)
{
    if (t == titleText) return;
    titleText = t;
    setTitle (t);
    repaint();
}

void BigToggle::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = getToggleState();
    const bool enabled = isEnabled();
    const float alpha = enabled ? 1.0f : 0.4f;

    if (on)
    {
        g.setColour (col::blue.withMultipliedAlpha (alpha).brighter (down ? -0.1f : 0.0f));
        g.fillRoundedRectangle (r, 10.0f);
    }
    else
    {
        g.setColour ((down ? col::grid2 : col::btnOff).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour ((highlighted && enabled ? col::tick : col::btnBorder).withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (r, 10.0f, 1.0f);
    }

    const auto textCol = (on ? col::btnOnText : col::text).withMultipliedAlpha (alpha);
    const auto subCol = (on ? col::btnOnText.withAlpha (0.75f) : col::text2).withMultipliedAlpha (alpha);

    auto content = getLocalBounds().reduced (14, 0);
    auto iconArea = content.removeFromLeft (24);
    g.setColour (textCol);
    g.setFont (iconMono ? Fonts::mono (18.0f, 600) : Fonts::inter (18.0f, 600));
    g.drawText (icon, iconArea, juce::Justification::centred, false);
    content.removeFromLeft (10);

    auto textArea = content.withSizeKeepingCentre (content.getWidth(), 38);
    g.setFont (Fonts::inter (14.0f, 600));
    g.drawText (titleText, textArea.removeFromTop (20), juce::Justification::centredLeft, true);
    g.setColour (subCol);
    g.setFont (Fonts::inter (12.0f));
    g.drawText (on ? subOn : subOff, textArea, juce::Justification::centredLeft, true);

    if (hasKeyboardFocus (false))
    {
        g.setColour (on ? col::text : col::blue);
        g.drawRoundedRectangle (r.reduced (1.0f), 9.0f, 1.5f);
    }
}

//==============================================================================
ModeSwitch::ModeSwitch (juce::RangedAudioParameter& modeParam)
    : attachment (modeParam, [this] (float v)
                  {
                      const bool isOverlap = v > 0.5f;
                      hit.setToggleState (! isOverlap, juce::dontSendNotification);
                      overlap.setToggleState (isOverlap, juce::dontSendNotification);
                  })
{
    for (auto* b : { &hit, &overlap })
    {
        addAndMakeVisible (*b);
        b->setRadioGroupId (0x50433031);
    }
    hit.setTooltip ("For sounds that hit together: kick mics, layered kicks, kick + bass on the same beat.");
    overlap.setTooltip ("For kick-and-offbeat-bass patterns (trance, techno, house). Needs the song playing.");
    hit.onClick = [this] { setMode (0); };
    overlap.onClick = [this] { setMode (1); };

    setTitle ("Mode");
    setFocusContainerType (FocusContainerType::focusContainer);
    attachment.sendInitialUpdate();
}

void ModeSwitch::setMode (int m)
{
    attachment.setValueAsCompleteGesture ((float) m);
}

void ModeSwitch::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (col::card);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (col::border);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
}

void ModeSwitch::resized()
{
    auto r = getLocalBounds().reduced (3);
    const int w = r.getWidth() / 2;
    hit.setBounds (r.removeFromLeft (w));
    overlap.setBounds (r);
}

} // namespace pc::ui
