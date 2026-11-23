#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace pc::ui
{

namespace col
{
    const juce::Colour bg         { 0xff15171B };
    const juce::Colour card       { 0xff1D2026 };
    const juce::Colour border     { 0xff2A2E36 };
    const juce::Colour inset      { 0xff16181D };
    const juce::Colour text       { 0xffE8EAED };
    const juce::Colour text2      { 0xff9AA0AA };
    const juce::Colour grid1      { 0xff24282F };
    const juce::Colour grid2      { 0xff2F343D };
    const juce::Colour tick       { 0xff4A505C };
    const juce::Colour orange     { 0xffFF9F5A };
    const juce::Colour blue       { 0xff6CB8FF };
    const juce::Colour negZone    { 0xff3A2618 };
    const juce::Colour posZone    { 0xff172A3D };
    const juce::Colour kick       { 0xffE8EAED };
    const juce::Colour sidechain  { 0xffC49BFF };
    const juce::Colour btnOff     { 0xff262A31 };
    const juce::Colour btnBorder  { 0xff3A3F49 };
    const juce::Colour btnOnText  { 0xff0E1621 };
    const juce::Colour beating    { 0xff7A808C };
}

namespace sym
{
    inline juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }
    inline const juce::String minus   = u8 ("\xE2\x88\x92");
    inline const juce::String dot     = u8 ("\xC2\xB7");     // middle dot
    inline const juce::String bullet  = u8 ("\xE2\x97\x8F");
    inline const juce::String arrow   = u8 ("\xE2\x86\x92");
    inline const juce::String mapsTo  = u8 ("\xE2\x86\xA6");
    inline const juce::String oslash  = u8 ("\xC3\x98");
    inline const juce::String check   = u8 ("\xE2\x9C\x93");
    inline const juce::String cross   = u8 ("\xE2\x9C\x97");
    inline const juce::String times   = u8 ("\xC3\x97");
    inline const juce::String degree  = u8 ("\xC2\xB0");
    inline const juce::String enDash  = u8 ("\xE2\x80\x93");
}

/** Embedded Inter and JetBrains Mono. */
struct Fonts
{
    static juce::Font inter (float size, int weight = 400);
    static juce::Font mono (float size, int weight = 400);
};

/** "+0.71" / "−0.58" */
juce::String signedValue (double v, int decimals = 2);
juce::String fixedValue (double v, int decimals);   // real minus sign, no plus

juce::Colour signColour (double v);

juce::String verdictFor (double correlation, bool cancellingWording = false);

void drawCard (juce::Graphics& g, juce::Rectangle<float> r);
void drawInset (juce::Graphics& g, juce::Rectangle<float> r);

/** Card title + secondary subtitle on one line. Returns the area below the title. */
juce::Rectangle<int> drawCardTitle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title,
                                    const juce::String& subtitle);

/** A −1..+1 bar that grows from the centre (orange left, blue right). */
void drawCentreBar (juce::Graphics& g, juce::Rectangle<float> track, double value, bool active, float alpha = 1.0f);

/** Diagonal 45 degree stripes inside r. */
void drawStripes (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float spacing = 6.0f);

void drawDashedRect (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour c);

} // namespace pc::ui
