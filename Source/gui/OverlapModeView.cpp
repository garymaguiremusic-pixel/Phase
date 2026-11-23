#include "OverlapModeView.h"

namespace pc::ui
{

namespace
{
    constexpr float envFloorDb = -48.0f;

    juce::String hzText (double hz, int decimals)
    {
        return juce::String (hz, decimals) + " Hz";
    }

    juce::String noteText (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
}

OverlapModeView::OverlapModeView()
{
    setInterceptsMouseClicks (false, false);
}

void OverlapModeView::update (const Snapshot& s, bool isLive)
{
    const bool changed = s.serial != lastSerial || isLive != live;
    snap = s;
    live = isLive;
    stale = s.overlap.stale || ! isLive;
    lastSerial = s.serial;
    if (changed) repaint();
}

void OverlapModeView::resized()
{
    auto r = getLocalBounds();
    timelineBounds = r.removeFromTop (238);
    r.removeFromTop (16);

    const float total = (float) (r.getWidth() - 32);
    const int w1 = juce::roundToInt (total * 1.2f / 3.2f);
    const int w2 = juce::roundToInt (total * 1.0f / 3.2f);
    zonesBounds = r.removeFromLeft (w1);
    r.removeFromLeft (16);
    pitchBounds = r.removeFromLeft (w2);
    r.removeFromLeft (16);
    fixBounds = r;
}

void OverlapModeView::paint (juce::Graphics& g)
{
    paintTimeline (g, timelineBounds);
    paintZones (g, zonesBounds);
    paintPitch (g, pitchBounds);
}

//==============================================================================
void OverlapModeView::paintTimeline (juce::Graphics& g, juce::Rectangle<int> area)
{
    drawCard (g, area.toFloat());
    auto r = area.reduced (20, 18);
    auto titleRow = r.removeFromTop (20);

    // title
    g.setFont (Fonts::inter (14.0f, 600));
    g.setColour (col::text);
    g.drawText ("One bar " + sym::dot + " where kick and bass overlap", titleRow, juce::Justification::centredLeft, false);

    // legend on the right of the title row
    {
        struct Item { int kind; juce::Colour c; juce::String text; };
        const Item items[] = {
            { 0, col::kick, "Kick" },
            { 0, col::sidechain, "Bass (sidechain)" },
            { 1, col::orange, "overlap, out of phase" },
            { 1, col::blue, "overlap, in phase" },
            { 2, col::beating, "overlap, beating (pitch clash)" },
        };

        float size = 12.0f;
        auto widthOf = [&] (float sz)
        {
            float w = 0;
            for (auto& it : items)
            {
                juce::GlyphArrangement ga;
                ga.addLineOfText (Fonts::inter (sz), it.text, 0, 0);
                w += 14 + 6 + ga.getBoundingBox (0, -1, true).getWidth() + 14;
            }
            return w;
        };
        const float available = (float) titleRow.getWidth() - 300.0f;
        while (size > 9.5f && widthOf (size) > available) size -= 0.5f;

        float x = (float) titleRow.getRight() - widthOf (size) + 14;
        const float cy = (float) titleRow.getCentreY();
        for (auto& it : items)
        {
            juce::GlyphArrangement ga;
            ga.addLineOfText (Fonts::inter (size), it.text, 0, 0);
            const float tw = ga.getBoundingBox (0, -1, true).getWidth();

            auto sw = juce::Rectangle<float> (x, cy - 5.0f, 14.0f, 10.0f);
            if (it.kind == 0)
            {
                g.setColour (it.c);
                g.fillRoundedRectangle (sw.withSizeKeepingCentre (14.0f, 3.0f), 1.5f);
            }
            else if (it.kind == 1)
            {
                g.setColour (it.c.withAlpha (0.45f));
                g.fillRect (sw.withSizeKeepingCentre (10.0f, 10.0f));
                g.setColour (it.c);
                g.fillRect (sw.withSizeKeepingCentre (10.0f, 10.0f).removeFromTop (2.0f));
            }
            else
            {
                auto sq = sw.withSizeKeepingCentre (10.0f, 10.0f);
                g.setColour (it.c.withAlpha (0.25f));
                g.fillRect (sq);
                drawStripes (g, sq, it.c, 3.5f);
            }
            x += 20.0f;
            g.setColour (col::text2);
            g.setFont (Fonts::inter (size));
            g.drawText (it.text, juce::Rectangle<float> (x, (float) titleRow.getY(), tw + 4, (float) titleRow.getHeight()),
                        juce::Justification::centredLeft, false);
            x += tw + 14;
        }
    }

    r.removeFromTop (12);
    auto noteStrip = r.removeFromTop (18);
    r.removeFromTop (4);
    auto plot = r.removeFromTop (120).toFloat();
    r.removeFromTop (6);
    auto beatRow = r.removeFromTop (18);

    drawInset (g, plot);
    const auto& o = snap.overlap;
    const float alpha = stale ? 0.4f : 1.0f;
    const auto inner = plot.reduced (8.0f, 6.0f);

    auto xFor = [&inner] (double frac) { return inner.getX() + (float) juce::jlimit (0.0, 1.0, frac) * inner.getWidth(); };

    // beats
    const int beats = std::max (1, o.beatsPerBar);
    const int sub = o.denominator <= 4 ? 2 : 1;
    g.setColour (col::grid2);
    for (int b = 1; b < beats; ++b)
    {
        const float x = xFor ((double) b / beats);
        g.fillRect (juce::Rectangle<float> (x - 0.5f, inner.getY(), 1.0f, inner.getHeight()));
    }

    if (o.hasBar)
    {
        // zones behind the envelopes
        for (const auto& z : o.zones)
        {
            const float x0 = xFor (z.startFrac), x1 = std::max (x0 + 2.0f, xFor (z.endFrac));
            auto band = juce::Rectangle<float> (x0, inner.getY(), x1 - x0, inner.getHeight());
            if (z.beating)
            {
                g.setColour (col::beating.withAlpha (0.18f * alpha));
                g.fillRect (band);
                drawStripes (g, band, col::beating.withAlpha (0.6f * alpha), 5.0f);
                g.setColour (col::beating.withMultipliedAlpha (alpha));
            }
            else
            {
                const auto c = signColour (z.correlation);
                g.setColour (c.withAlpha (0.30f * alpha));
                g.fillRect (band);
                g.setColour (c.withMultipliedAlpha (alpha));
            }
            g.fillRect (band.removeFromTop (3.0f));
        }

        // envelopes
        auto envPath = [&] (const std::vector<float>& env, bool closed)
        {
            juce::Path p;
            if (env.size() < 2) return p;
            const float bottom = inner.getBottom();
            for (size_t i = 0; i < env.size(); ++i)
            {
                const float x = inner.getX() + (float) i / (float) (env.size() - 1) * inner.getWidth();
                const float norm = juce::jlimit (0.0f, 1.0f, (env[i] - envFloorDb) / -envFloorDb);
                const float y = bottom - norm * (inner.getHeight() - 4.0f);
                if (i == 0) { if (closed) { p.startNewSubPath (x, bottom); p.lineTo (x, y); } else p.startNewSubPath (x, y); }
                else p.lineTo (x, y);
            }
            if (closed) { p.lineTo (inner.getRight(), bottom); p.closeSubPath(); }
            return p;
        };

        g.setColour (col::kick.withAlpha (0.14f * alpha));
        g.fillPath (envPath (o.kickEnvDb, true));
        g.setColour (col::sidechain.withAlpha (0.22f * alpha));
        g.fillPath (envPath (o.bassEnvDb, true));
        g.setColour (col::kick.withAlpha (0.9f * alpha));
        g.strokePath (envPath (o.kickEnvDb, false), juce::PathStrokeType (1.4f));
        g.setColour (col::sidechain.withMultipliedAlpha (alpha));
        g.strokePath (envPath (o.bassEnvDb, false), juce::PathStrokeType (1.4f));

        // labels for type A zones
        g.setFont (Fonts::mono (10.0f, 600));
        for (const auto& z : o.zones)
        {
            if (z.type != 'A') continue;
            const float x0 = xFor (z.startFrac), x1 = xFor (z.endFrac);
            const auto text = z.beating ? juce::String ("beating") : (z.correlationValid ? fixedValue (z.correlation, 2) : sym::enDash);
            auto box = juce::Rectangle<float> ((x0 + x1) * 0.5f - 30.0f, inner.getY() + 5.0f, 60.0f, 12.0f);
            g.setColour ((z.beating ? col::text2 : signColour (z.correlation)).withMultipliedAlpha (alpha));
            g.drawText (text, box, juce::Justification::centred, false);
        }

        // note labels
        g.setFont (Fonts::mono (12.0f, 600));
        for (const auto& n : o.notes)
        {
            const float x = xFor (n.posFrac);
            auto t = noteText (n.name);
            if (! n.matched) t << " " << sym::dot << " clash";
            g.setColour ((n.matched ? col::sidechain : col::text2).withMultipliedAlpha (alpha));
            g.drawText (t, juce::Rectangle<float> (x, (float) noteStrip.getY(), 120.0f, (float) noteStrip.getHeight()),
                        juce::Justification::centredLeft, false);
        }
    }

    if (stale || ! o.hasBar)
    {
        g.setColour (col::text2);
        g.setFont (Fonts::inter (13.0f, 500));
        const juce::String msg = ! live ? juce::String ("No sidechain signal")
                               : ! o.hasBar ? "Waiting for a full bar" + juce::String::fromUTF8 ("\xE2\x80\xA6") + " press play in Studio Pro"
                                            : juce::String ("Waiting for the next bar");
        g.drawText (msg, plot, juce::Justification::centred, false);
    }
    else if (o.noBass)
    {
        g.setColour (col::text2);
        g.setFont (Fonts::inter (12.0f, 500));
        g.drawText ("No bass notes detected in this bar", noteStrip, juce::Justification::centredRight, false);
    }

    // beat labels on an equal grid of eighths (adapted to the time signature)
    g.setFont (Fonts::mono (12.0f));
    g.setColour (col::text2);
    const int cells = beats * sub;
    for (int i = 0; i < cells; ++i)
    {
        const float x = xFor ((double) i / cells);
        const juce::String t = (i % sub == 0) ? juce::String (i / sub + 1) : juce::String ("&");
        g.drawText (t, juce::Rectangle<float> (x, (float) beatRow.getY(), 30.0f, (float) beatRow.getHeight()),
                    juce::Justification::centredLeft, false);
    }
}

//==============================================================================
void OverlapModeView::paintZones (juce::Graphics& g, juce::Rectangle<int> area)
{
    drawCard (g, area.toFloat());
    auto r = drawCardTitle (g, area.reduced (18), "Overlap zones", "matched notes only");
    r.removeFromTop (12);

    const auto& o = snap.overlap;
    auto handover = r.removeFromBottom (22);
    r.removeFromBottom (8);

    if (! stale && o.hasBar && o.noBass)
    {
        drawInset (g, r.toFloat());
        g.setColour (col::text2);
        g.setFont (Fonts::inter (13.0f, 500));
        g.drawText ("No bass notes detected in this bar", r, juce::Justification::centred, false);
        return;
    }

    const int rowH = (r.getHeight() - 10) / 2;
    auto drawRow = [&] (juce::Rectangle<int> row, const juce::String& name, bool valid, double corr, double durMs, int count)
    {
        drawInset (g, row.toFloat());
        auto c = row.reduced (14, 12);
        auto top = c.removeFromTop (18);
        g.setFont (Fonts::inter (13.0f, 600));
        g.setColour (col::text);
        g.drawText (name, top, juce::Justification::centredLeft, false);

        const bool show = valid && ! stale;
        g.setFont (Fonts::mono (12.0f));
        g.setColour (col::text2);
        g.drawText (show ? juce::String (juce::roundToInt (durMs)) + " ms avg" : (count == 0 && ! stale && o.hasBar ? juce::String ("none") : sym::enDash),
                    top, juce::Justification::centredRight, false);

        c.removeFromTop (10);
        auto barRow = c.removeFromTop (20);
        auto value = barRow.removeFromRight (60);
        barRow.removeFromRight (10);
        drawCentreBar (g, barRow.toFloat().withSizeKeepingCentre ((float) barRow.getWidth(), 10.0f), corr, show, show ? 1.0f : 0.4f);
        g.setFont (Fonts::mono (14.0f, 600));
        g.setColour (show ? signColour (corr) : col::tick);
        g.drawText (show ? fixedValue (corr, 2) : sym::enDash, value, juce::Justification::centredRight, false);

        c.removeFromTop (6);
        g.setFont (Fonts::inter (12.0f, 500));
        g.setColour (show ? signColour (corr) : col::text2);
        const juce::String verdict = show ? verdictFor (corr, true)
                                   : (stale ? juce::String ("No data") : juce::String ("No matched overlap in recent bars"));
        g.drawText (verdict, c.removeFromTop (16), juce::Justification::centredLeft, false);
    };

    drawRow (r.removeFromTop (rowH), "A " + sym::dot + " Kick tail " + sym::arrow + " bass note", o.aValid, o.aCorrelation, o.aDurationMs, o.aCount);
    r.removeFromTop (10);
    drawRow (r.removeFromTop (rowH), "B " + sym::dot + " Bass tail " + sym::arrow + " next kick", o.bValid, o.bCorrelation, o.bDurationMs, o.bCount);

    g.setFont (Fonts::inter (13.0f));
    g.setColour (col::text2);
    juce::String ho = "Level at the kick " + sym::arrow + " bass handover: ";
    ho << ((! stale && o.handoverValid) ? signedValue (o.handoverDb, 1) + " dB" : sym::enDash);
    g.drawText (ho, handover, juce::Justification::centredLeft, true);
}

//==============================================================================
void OverlapModeView::paintPitch (juce::Graphics& g, juce::Rectangle<int> area)
{
    drawCard (g, area.toFloat());
    auto r = drawCardTitle (g, area.reduced (18), "Pitch check", "kick tail vs. bass notes");
    r.removeFromTop (10);

    const auto& o = snap.overlap;
    const bool show = ! stale && o.hasBar;

    auto readout = r.removeFromTop (40);
    g.setFont (Fonts::mono (32.0f, 600));
    g.setColour (show && o.kickPitchValid ? col::text : col::tick);
    const juce::String big = show && o.kickPitchValid ? hzText (o.kickTailHz, 1) : sym::enDash;
    juce::GlyphArrangement ga;
    ga.addLineOfText (Fonts::mono (32.0f, 600), big, 0, 0);
    const int bw = (int) std::ceil (ga.getBoundingBox (0, -1, true).getWidth());
    g.drawText (big, readout.removeFromLeft (bw + 4), juce::Justification::centredLeft, false);
    readout.removeFromLeft (10);
    g.setFont (Fonts::inter (13.0f));
    g.setColour (col::text2);
    g.drawText ("kick tail" + (show && o.kickPitchValid ? " " + sym::dot + " " + noteText (o.kickTailNote) : juce::String()),
                readout.withTrimmedTop (8), juce::Justification::centredLeft, false);

    r.removeFromTop (10);
    auto foot = r.removeFromBottom (50);
    r.removeFromBottom (6);

    g.setFont (Fonts::inter (12.0f));
    g.setColour (col::text2);
    g.drawFittedText ("Beating means the two pitches drift in and out of phase. Polarity or rotation can't fix that: "
                      "retune the kick, shorten its tail, or duck the bass harder.",
                      foot, juce::Justification::bottomLeft, 4, 0.85f);

    if (! show)
        return;

    if (o.notes.empty())
    {
        drawInset (g, r.toFloat().withHeight (std::min (60.0f, (float) r.getHeight())));
        g.setColour (col::text2);
        g.setFont (Fonts::inter (13.0f, 500));
        g.drawText ("No bass notes detected in this bar", r.withHeight (std::min (60, r.getHeight())), juce::Justification::centred, false);
        return;
    }

    const int n = (int) o.notes.size();
    const int gap = 6;
    int rowH = juce::jlimit (18, 30, (r.getHeight() - gap * (n - 1)) / std::max (1, n));
    const int fit = std::max (1, (r.getHeight() + gap) / (rowH + gap));
    const int shown = std::min (n, fit);

    for (int i = 0; i < shown; ++i)
    {
        const auto& note = o.notes[(size_t) i];
        auto row = r.removeFromTop (rowH);
        r.removeFromTop (gap);
        drawInset (g, row.toFloat());
        auto c = row.reduced (10, 0);

        g.setFont (Fonts::mono (12.0f));
        g.setColour (col::text2);
        g.drawText (noteText (note.position), c.removeFromLeft (30), juce::Justification::centredLeft, false);
        g.setFont (Fonts::inter (13.0f, 600));
        g.setColour (col::sidechain);
        g.drawText (noteText (note.name), c.removeFromLeft (38), juce::Justification::centredLeft, false);
        g.setFont (Fonts::mono (12.0f));
        g.setColour (col::text2);
        g.drawText (note.pitchValid ? juce::String (juce::roundToInt (note.hz)) + " Hz" : sym::enDash,
                    c.removeFromLeft (52), juce::Justification::centredLeft, false);

        g.setFont (Fonts::inter (12.0f, 600));
        if (! note.pitchValid || ! o.kickPitchValid)
        {
            g.setColour (col::text2);
            g.drawText ("pitch unclear", c, juce::Justification::centredRight, false);
        }
        else if (note.matched)
        {
            g.setColour (col::blue);
            g.drawText (sym::check + " matched", c, juce::Justification::centredRight, false);
        }
        else
        {
            g.setColour (col::orange);
            g.drawText (sym::cross + " beats " + juce::String (std::max (1, juce::roundToInt (note.beatHz))) + sym::times + " / sec",
                        c, juce::Justification::centredRight, false);
        }
    }

    if (shown < n)
    {
        g.setFont (Fonts::inter (11.0f));
        g.setColour (col::text2);
        g.drawText ("+" + juce::String (n - shown) + " more", r.removeFromTop (14), juce::Justification::centredRight, false);
    }
}

} // namespace pc::ui
