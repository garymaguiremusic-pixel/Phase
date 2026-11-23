#include "Theme.h"

#include <BinaryData.h>

namespace pc::ui
{

namespace
{
    juce::Typeface::Ptr load (const char* data, int size)
    {
        return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
    }

    struct Typefaces
    {
        juce::Typeface::Ptr interRegular  = load (PhaseCheckBinary::InterRegular_ttf,  PhaseCheckBinary::InterRegular_ttfSize);
        juce::Typeface::Ptr interMedium   = load (PhaseCheckBinary::InterMedium_ttf,   PhaseCheckBinary::InterMedium_ttfSize);
        juce::Typeface::Ptr interSemiBold = load (PhaseCheckBinary::InterSemiBold_ttf, PhaseCheckBinary::InterSemiBold_ttfSize);
        juce::Typeface::Ptr interBold     = load (PhaseCheckBinary::InterBold_ttf,     PhaseCheckBinary::InterBold_ttfSize);
        juce::Typeface::Ptr monoRegular   = load (PhaseCheckBinary::JetBrainsMonoRegular_ttf,  PhaseCheckBinary::JetBrainsMonoRegular_ttfSize);
        juce::Typeface::Ptr monoSemiBold  = load (PhaseCheckBinary::JetBrainsMonoSemiBold_ttf, PhaseCheckBinary::JetBrainsMonoSemiBold_ttfSize);
    };

    const Typefaces& typefaces()
    {
        static const Typefaces t;
        return t;
    }
}

juce::Font Fonts::inter (float size, int weight)
{
    const auto& t = typefaces();
    auto tf = weight >= 700 ? t.interBold : weight >= 600 ? t.interSemiBold : weight >= 500 ? t.interMedium : t.interRegular;
    return juce::Font (juce::FontOptions (tf).withPointHeight (size));
}

juce::Font Fonts::mono (float size, int weight)
{
    const auto& t = typefaces();
    return juce::Font (juce::FontOptions (weight >= 600 ? t.monoSemiBold : t.monoRegular).withPointHeight (size));
}

juce::String fixedValue (double v, int decimals)
{
    auto s = juce::String (std::abs (v), decimals);
    const bool zero = s.containsOnly ("0.");
    return (v < 0 && ! zero) ? sym::minus + s : s;
}

juce::String signedValue (double v, int decimals)
{
    auto s = juce::String (std::abs (v), decimals);
    if (s.containsOnly ("0.")) return s;
    return (v < 0 ? sym::minus : juce::String ("+")) + s;
}

juce::Colour signColour (double v)
{
    return v < 0 ? col::orange : col::blue;
}

juce::String verdictFor (double c, bool cancellingWording)
{
    if (c >= 0.5) return "In phase";
    if (c >= 0.0) return "Partly in phase";
    if (c >= -0.5) return "Partly out of phase";
    return cancellingWording ? "Out of phase: cancelling" : "Out of phase";
}

void drawCard (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (col::card);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (col::border);
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
}

void drawInset (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (col::inset);
    g.fillRoundedRectangle (r, 8.0f);
}

juce::Rectangle<int> drawCardTitle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title,
                                    const juce::String& subtitle)
{
    auto row = area.removeFromTop (20);
    const auto tf = Fonts::inter (14.0f, 600);
    g.setFont (tf);
    g.setColour (col::text);
    juce::GlyphArrangement ga;
    ga.addLineOfText (tf, title, 0, 0);
    const int tw = (int) std::ceil (ga.getBoundingBox (0, -1, true).getWidth());
    g.drawText (title, row.removeFromLeft (tw + 2), juce::Justification::centredLeft, false);

    if (subtitle.isNotEmpty())
    {
        row.removeFromLeft (8);
        g.setFont (Fonts::inter (12.0f));
        g.setColour (col::text2);
        g.drawText (subtitle, row, juce::Justification::centredLeft, true);
    }
    return area;
}

void drawCentreBar (juce::Graphics& g, juce::Rectangle<float> track, double value, bool active, float alpha)
{
    g.setColour (col::grid1.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (track, track.getHeight() * 0.5f);

    const float cx = track.getCentreX();
    if (active)
    {
        const double v = juce::jlimit (-1.0, 1.0, value);
        const float w = (float) std::abs (v) * track.getWidth() * 0.5f;
        auto bar = v < 0 ? juce::Rectangle<float> (cx - w, track.getY(), w, track.getHeight())
                         : juce::Rectangle<float> (cx, track.getY(), w, track.getHeight());
        g.setColour (signColour (v).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (bar, std::min (3.0f, track.getHeight() * 0.5f));
    }

    g.setColour (col::tick.withMultipliedAlpha (alpha));
    g.fillRect (juce::Rectangle<float> (cx - 0.5f, track.getY() - 3.0f, 1.0f, track.getHeight() + 6.0f));
}

void drawStripes (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float spacing)
{
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (r.toNearestInt());
    g.setColour (c);
    for (float x = r.getX() - r.getHeight(); x < r.getRight(); x += spacing)
        g.drawLine (x, r.getBottom(), x + r.getHeight(), r.getY(), 1.2f);
}

void drawDashedRect (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour c)
{
    juce::Path p;
    p.addRoundedRectangle (r, radius);
    juce::Path dashed;
    const float dashes[] = { 5.0f, 4.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, p, dashes, 2);
    g.setColour (c);
    g.fillPath (dashed);
}

} // namespace pc::ui
