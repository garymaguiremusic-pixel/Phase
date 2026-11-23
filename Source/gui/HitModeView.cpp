#include "HitModeView.h"

namespace pc::ui
{

namespace
{
    juce::String msText (double v)
    {
        auto s = juce::String (std::abs (v), 2);
        if (s.endsWithChar ('0')) s = s.dropLastCharacters (1);
        return s;
    }

    juce::Path wavePath (const std::vector<float>& w, juce::Rectangle<float> plot, double lengthMs, double shownMs)
    {
        juce::Path p;
        if (w.size() < 2) return p;
        const float midY = plot.getCentreY();
        const float amp = plot.getHeight() * 0.5f - 6.0f;
        const double msPerPoint = lengthMs / (double) w.size();
        bool started = false;
        for (size_t i = 0; i < w.size(); ++i)
        {
            const double ms = (double) i * msPerPoint;
            if (ms > shownMs) break;
            const float x = plot.getX() + (float) (ms / shownMs) * plot.getWidth();
            const float y = midY - juce::jlimit (-1.2f, 1.2f, w[i]) * amp;
            if (! started) { p.startNewSubPath (x, y); started = true; }
            else p.lineTo (x, y);
        }
        return p;
    }
}

HitModeView::HitModeView()
{
    setInterceptsMouseClicks (false, false);
}

void HitModeView::update (const Snapshot& s, bool isLive, double dt)
{
    snap = s;
    live = isLive;
    stale = s.hit.stale || ! isLive;

    // meter ballistics: ~150 ms attack (moving away from 0), ~600 ms release
    if (! stale)
    {
        const double target = s.hit.correlation;
        if (! meterSeeded) { meter = target; meterSeeded = true; }
        const bool attack = std::abs (target) > std::abs (meter) && (target * meter >= 0);
        const double tau = attack ? 0.150 : 0.600;
        meter += (target - meter) * (1.0 - std::exp (-dt / tau));
    }
    else
    {
        meterSeeded = false;
    }

    // crossfade in a new hit
    if (s.hit.hitCount != lastHitCount)
    {
        if (lastHitCount >= 0) fade = 0.0f;
        lastHitCount = s.hit.hitCount;
    }
    if (fade < 1.0f)
        fade = std::min (1.0f, fade + (float) (dt / 0.12));
    if (fade >= 1.0f)
    {
        prevKick = s.hit.waveKick;
        prevSc = s.hit.waveSidechain;
        prevOnsetMs = s.hit.sidechainOnsetMs;
        prevOnsetValid = s.hit.sidechainOnsetValid;
    }

    repaint();
}

void HitModeView::resized()
{
    auto r = getLocalBounds();
    corrBounds = r.removeFromTop (150);
    r.removeFromTop (16);

    const float total = (float) (r.getWidth() - 32);
    const int w1 = juce::roundToInt (total * 1.55f / 3.55f);
    const int w2 = juce::roundToInt (total * 1.0f / 3.55f);
    waveBounds = r.removeFromLeft (w1);
    r.removeFromLeft (16);
    bandBounds = r.removeFromLeft (w2);
    r.removeFromLeft (16);
    fixBounds = r;
}

void HitModeView::paint (juce::Graphics& g)
{
    paintCorrelation (g, corrBounds);
    paintWaveform (g, waveBounds);
    paintBands (g, bandBounds);
}

//==============================================================================
void HitModeView::paintCorrelation (juce::Graphics& g, juce::Rectangle<int> area)
{
    drawCard (g, area.toFloat());
    auto r = area.reduced (20, 18);

    // ---- left block -----------------------------------------------------------
    auto left = r.removeFromLeft (230);
    g.setColour (col::text2);
    g.setFont (Fonts::inter (12.0f, 600).withExtraKerningFactor (0.06f));
    g.drawText ("CORRELATION", left.removeFromTop (16), juce::Justification::centredLeft, false);

    const double c = meter;
    const auto valueCol = stale ? col::tick : signColour (c);
    g.setFont (Fonts::mono (48.0f, 600));
    g.setColour (stale ? col::tick : col::text);
    g.drawText (stale ? sym::enDash : fixedValue (c, 2), left.removeFromTop (56), juce::Justification::centredLeft, false);

    g.setFont (Fonts::inter (15.0f, 600));
    g.setColour (valueCol);
    juce::String verdict = stale ? (live ? juce::String ("Waiting for kick hits") : juce::String ("No sidechain signal")) : verdictFor (c);
    g.drawText (verdict, left.removeFromTop (22), juce::Justification::centredLeft, false);

    g.setFont (Fonts::inter (13.0f));
    g.setColour (col::text2);
    juce::String sum;
    if (! stale && snap.hit.sumValid)
        sum << "Sum at " << juce::roundToInt (snap.hit.sumHz) << " Hz: " << signedValue (snap.hit.sumDb, 1) << " dB";
    else
        sum << "Sum at " << sym::enDash << " Hz: " << sym::enDash << " dB";
    g.drawText (sum, left.removeFromTop (20), juce::Justification::centredLeft, false);

    // ---- meter ------------------------------------------------------------------
    r.removeFromLeft (24);
    auto meterArea = r.withTrimmedTop (22);
    auto bar = meterArea.removeFromTop (24).toFloat();
    const float cx = bar.getCentreX();
    const float alpha = stale ? 0.45f : 1.0f;

    {
        juce::Path clip;
        clip.addRoundedRectangle (bar, 6.0f);
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (clip);
        g.setColour (col::negZone.withMultipliedAlpha (alpha));
        g.fillRect (bar.withRight (cx));
        g.setColour (col::posZone.withMultipliedAlpha (alpha));
        g.fillRect (bar.withLeft (cx));
    }

    auto xFor = [&bar] (double v) { return bar.getX() + (float) ((v + 1.0) * 0.5) * bar.getWidth(); };
    g.setColour (col::tick);
    for (double t : { -0.5, 0.0, 0.5 })
    {
        const float x = xFor (t);
        const float h = exactlyEqual (t, 0.0) ? bar.getHeight() : bar.getHeight() * 0.5f;
        g.fillRect (juce::Rectangle<float> (x - 0.5f, bar.getCentreY() - h * 0.5f, 1.0f, h));
    }

    if (! stale)
    {
        const float x = xFor (juce::jlimit (-1.0, 1.0, c));
        g.setColour (signColour (c));
        g.fillRoundedRectangle (juce::Rectangle<float> (x - 2.0f, bar.getY() - 5.0f, 4.0f, bar.getHeight() + 10.0f), 2.0f);
    }

    meterArea.removeFromTop (10);
    auto labels = meterArea.removeFromTop (16).toFloat();
    g.setFont (Fonts::mono (12.0f));
    g.setColour (col::text2);
    const char* scale[] = { "-1", "-0.5", "0", "+0.5", "+1" };
    const double vals[] = { -1.0, -0.5, 0.0, 0.5, 1.0 };
    for (int i = 0; i < 5; ++i)
    {
        auto txt = juce::String (scale[i]).replace ("-", sym::minus);
        const float x = xFor (vals[i]);
        auto just = i == 0 ? juce::Justification::centredLeft : i == 4 ? juce::Justification::centredRight : juce::Justification::centred;
        auto box = i == 0 ? juce::Rectangle<float> (x, labels.getY(), 60, 16)
                 : i == 4 ? juce::Rectangle<float> (x - 60, labels.getY(), 60, 16)
                          : juce::Rectangle<float> (x - 30, labels.getY(), 60, 16);
        g.drawText (txt, box, just, false);
    }

    meterArea.removeFromTop (8);
    auto captions = meterArea.removeFromTop (18);
    g.setFont (Fonts::inter (12.0f, 600).withExtraKerningFactor (0.04f));
    g.setColour (col::orange.withMultipliedAlpha (alpha));
    g.drawText ("OUT OF PHASE " + sym::dot + " cancels", captions, juce::Justification::centredLeft, false);
    g.setColour (col::blue.withMultipliedAlpha (alpha));
    g.drawText ("IN PHASE " + sym::dot + " adds up", captions, juce::Justification::centredRight, false);
}

//==============================================================================
void HitModeView::paintWaveform (juce::Graphics& g, juce::Rectangle<int> area)
{
    drawCard (g, area.toFloat());
    auto r = drawCardTitle (g, area.reduced (18), "Waveform overlay", "first 40 ms after hit");
    r.removeFromTop (12);

    auto infoLine = r.removeFromBottom (40);
    r.removeFromBottom (10);
    auto legend = r.removeFromBottom (18);
    r.removeFromBottom (8);
    auto plot = r.toFloat();

    drawInset (g, plot);
    const double shownMs = 40.0;

    // grid
    g.setColour (col::grid1);
    for (int ms = 10; ms < 40; ms += 10)
    {
        const float x = plot.getX() + (float) (ms / shownMs) * plot.getWidth();
        g.fillRect (juce::Rectangle<float> (x - 0.5f, plot.getY() + 6, 1.0f, plot.getHeight() - 12));
    }
    g.setColour (col::grid2);
    g.fillRect (juce::Rectangle<float> (plot.getX() + 6, plot.getCentreY() - 0.5f, plot.getWidth() - 12, 1.0f));

    const auto& h = snap.hit;
    const float baseAlpha = stale ? 0.35f : 1.0f;

    auto drawSet = [&] (const std::vector<float>& k, const std::vector<float>& s, bool onsetValid, double onsetMs, float a)
    {
        if (a <= 0.0f) return;
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (plot.toNearestInt());

        if (onsetValid && onsetMs >= 0.0 && onsetMs <= shownMs)
        {
            const float x = plot.getX() + (float) (onsetMs / shownMs) * plot.getWidth();
            juce::Path line;
            line.startNewSubPath (x, plot.getY() + 4);
            line.lineTo (x, plot.getBottom() - 4);
            juce::Path dashed;
            const float d[] = { 4.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, line, d, 2);
            g.setColour (col::sidechain.withMultipliedAlpha (0.8f * a));
            g.fillPath (dashed);
        }

        g.setColour (col::sidechain.withMultipliedAlpha (a));
        g.strokePath (wavePath (s, plot, h.waveLengthMs, shownMs), juce::PathStrokeType (1.6f));
        g.setColour (col::kick.withMultipliedAlpha (0.9f * a));
        g.strokePath (wavePath (k, plot, h.waveLengthMs, shownMs), juce::PathStrokeType (1.6f));
    };

    if (h.hasData || ! prevKick.empty())
    {
        if (fade < 1.0f)
            drawSet (prevKick, prevSc, prevOnsetValid, prevOnsetMs, (1.0f - fade) * baseAlpha);
        drawSet (h.waveKick, h.waveSidechain, h.sidechainOnsetValid, h.sidechainOnsetMs, fade * baseAlpha);
    }

    if (stale)
    {
        g.setColour (col::text2);
        g.setFont (Fonts::inter (13.0f, 500));
        g.drawText (live ? "Waiting for kick hits" : "No sidechain signal", plot, juce::Justification::centred, false);
    }

    // legend
    {
        auto l = legend;
        auto swatch = [&] (juce::Colour c, const juce::String& t)
        {
            auto sw = l.removeFromLeft (16);
            g.setColour (c);
            g.fillRoundedRectangle (sw.toFloat().withSizeKeepingCentre (14.0f, 3.0f), 1.5f);
            l.removeFromLeft (6);
            g.setColour (col::text2);
            g.setFont (Fonts::inter (12.0f));
            g.drawText (t, l.removeFromLeft (70), juce::Justification::centredLeft, false);
        };
        swatch (col::kick, "Kick");
        swatch (col::sidechain, "Sidechain");
        g.setFont (Fonts::mono (12.0f));
        g.setColour (col::text2);
        const auto d = " " + sym::dot + " ";
        g.drawText ("0" + d + "10" + d + "20" + d + "30" + d + "40 ms", legend, juce::Justification::centredRight, false);
    }

    // info line
    drawInset (g, infoLine.toFloat());
    juce::String info;
    const double delayMs = snap.state.effectiveDelayMs();
    if (stale)
        info = live ? "Waiting for kick hits" + juce::String::fromUTF8 ("\xE2\x80\xA6") : "No sidechain signal";
    else if (delayMs > 0 && h.offsetClear && std::abs (h.offsetMs) < 0.05)
        info << "Kick delayed " << msText (delayMs) << " ms. Both hits now start together.";
    else if (! h.offsetClear)
        info = "Offset unclear: the two hits don't line up clearly.";
    else
    {
        const auto samples = juce::roundToInt (std::abs (h.offsetSamples));
        const double k = snap.sampleRate / 1000.0;
        const auto khz = std::abs (k - std::round (k)) < 1.0e-6 ? juce::String (juce::roundToInt (k)) : juce::String (k, 1);
        if (std::abs (h.offsetMs) < 0.05)
            info << "Both hits start together (" << samples << " samples at " << khz << " kHz).";
        else
            info << "Sidechain arrives " << msText (h.offsetMs) << " ms " << (h.offsetMs > 0 ? "after" : "before")
                 << " the kick (" << samples << " samples at " << khz << " kHz).";
        if (delayMs > 0)
            info = "Kick delayed " + msText (delayMs) + " ms. " + info;
    }
    g.setColour (stale ? col::text2 : col::text);
    g.setFont (Fonts::inter (13.0f));
    g.drawFittedText (info, infoLine.reduced (12, 4), juce::Justification::centredLeft, 2, 0.9f);
}

//==============================================================================
void HitModeView::paintBands (juce::Graphics& g, juce::Rectangle<int> area)
{
    drawCard (g, area.toFloat());
    auto r = drawCardTitle (g, area.reduced (18), "Phase by frequency", "low end");
    r.removeFromTop (14);

    auto footnote = r.removeFromBottom (30);
    auto captions = r.removeFromBottom (18);
    r.removeFromBottom (4);

    const auto& h = snap.hit;
    const int rows = kNumBands;
    const int rowH = r.getHeight() / rows;
    const int labelW = 50, valueW = 50;

    for (int b = 0; b < rows; ++b)
    {
        auto row = r.removeFromTop (rowH);
        auto label = row.removeFromLeft (labelW);
        row.removeFromLeft (10);
        auto value = row.removeFromRight (valueW);
        row.removeFromRight (8);

        const bool lowConf = b == 0 && h.lowBandLowConfidence;
        const bool active = ! stale && h.bandActive[(size_t) b];
        const float alpha = active ? (lowConf ? 0.6f : 1.0f) : 0.35f;
        const double v = h.bandCorrelation[(size_t) b];

        g.setFont (Fonts::mono (12.0f));
        g.setColour (col::text2.withMultipliedAlpha (active ? 1.0f : 0.6f));
        g.drawText (juce::String (juce::roundToInt (kBandCentres[(size_t) b])) + " Hz" + (lowConf ? "*" : ""),
                    label, juce::Justification::centredRight, false);

        drawCentreBar (g, row.toFloat().withSizeKeepingCentre ((float) row.getWidth(), 10.0f), v, active, alpha);

        g.setFont (Fonts::mono (12.0f, 600));
        g.setColour (active ? signColour (v).withMultipliedAlpha (alpha) : col::tick);
        g.drawText (active ? fixedValue (v, 2) : sym::enDash, value, juce::Justification::centredRight, false);
    }

    auto cap = captions.withTrimmedLeft (labelW + 10).withTrimmedRight (valueW + 8);
    g.setFont (Fonts::inter (12.0f));
    g.setColour (col::orange.withAlpha (0.85f));
    g.drawText ("cancel", cap, juce::Justification::centredLeft, false);
    g.setColour (col::blue.withAlpha (0.85f));
    g.drawText ("add", cap, juce::Justification::centredRight, false);

    if (h.lowBandLowConfidence)
    {
        g.setFont (Fonts::inter (11.0f));
        g.setColour (col::text2.withAlpha (0.8f));
        g.drawFittedText ("* under 2 cycles fit in the 50 ms window: low confidence", footnote,
                          juce::Justification::bottomLeft, 2, 0.9f);
    }
}

} // namespace pc::ui
