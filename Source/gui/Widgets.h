#pragma once

#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace pc::ui
{

//==============================================================================
class PhaseLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PhaseLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float sliderPos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override { return 7; }

    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

    juce::Font getPopupMenuFont() override { return Fonts::inter (14.0f); }
};

//==============================================================================
/** Small flat button: segments of the mode switch, footer buttons. */
class FlatButton : public juce::Button
{
public:
    enum class Style { segment, footer };

    FlatButton (const juce::String& text, Style s);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Style style;
};

//==============================================================================
/** The big "Fix it on the kick" toggles: icon, title and a sub-label. */
class BigToggle : public juce::Button
{
public:
    BigToggle (const juce::String& icon, bool iconInMono, const juce::String& title,
               const juce::String& subOff, const juce::String& subOn);

    void setTitleText (const juce::String& t);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::String icon, titleText, subOff, subOn;
    bool iconMono;
};

//==============================================================================
/** Segmented "Hit mode | Overlap mode" control linked to the `mode` parameter. */
class ModeSwitch : public juce::Component
{
public:
    explicit ModeSwitch (juce::RangedAudioParameter& modeParam);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void setMode (int m);

    FlatButton hit { "Hit mode", FlatButton::Style::segment };
    FlatButton overlap { "Overlap mode", FlatButton::Style::segment };
    juce::ParameterAttachment attachment;
};

} // namespace pc::ui
