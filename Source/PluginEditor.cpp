#include "PluginEditor.h"

namespace pc
{

using namespace ui;

//==============================================================================
MainView::MainView (PhaseCheckProcessor& p)
    : processor (p),
      modeSwitch (*p.getState().getParameter (ParamID::mode)),
      fix (p.getState())
{
    setLookAndFeel (&lnf);
    setOpaque (true);

    addAndMakeVisible (modeSwitch);
    addChildComponent (hitView);
    addChildComponent (overlapView);
    addAndMakeVisible (fix);
    addAndMakeVisible (hold);
    addAndMakeVisible (reset);
    addAndMakeVisible (scaleButton);

    hold.setClickingTogglesState (true);
    hold.setTooltip ("Freeze all displays");
    reset.setTooltip ("Clear the averages and reset invert, delay and rotate to their defaults");
    scaleButton.setTooltip ("Window size (also on right-click)");
    scaleButton.setTitle ("Window size");

    reset.onClick = [this] { processor.resetAll(); };
    scaleButton.onClick = [this] { showScaleMenu(); };

    overlapShown = isOverlap();
    hitView.setVisible (! overlapShown);
    overlapView.setVisible (overlapShown);
    fix.setOverlapMode (overlapShown);

    setTitle ("Phase Check");
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);

    lastTime = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (30);
}

MainView::~MainView()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

bool MainView::isOverlap() const
{
    return processor.getState().getRawParameterValue (ParamID::mode)->load() > 0.5f;
}

void MainView::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = juce::jlimit (0.0, 0.2, (now - lastTime) / 1000.0);
    lastTime = now;

    const bool ov = isOverlap();
    if (ov != overlapShown)
    {
        overlapShown = ov;
        hitView.setVisible (! ov);
        overlapView.setVisible (ov);
        fix.setOverlapMode (ov);
        resized();
        repaint();
    }

    const auto scaleText = juce::String (juce::roundToInt (processor.getUiScale() * 100.0)) + "%";
    if (scaleButton.getButtonText() != scaleText)
        scaleButton.setButtonText (scaleText);

    if (! hold.getToggleState() || shown == nullptr)
    {
        auto latest = processor.getSnapshot();
        if (latest != nullptr)
            shown = latest;
    }

    if (shown == nullptr)
        return;

    const bool live = shown->sidechainLive;
    if (ov) overlapView.update (*shown, live);
    else hitView.update (*shown, live, dt);
    fix.update (*shown, live);

    repaint (headerArea);
    repaint (footerArea);
}

void MainView::resized()
{
    auto r = getLocalBounds().reduced (20);
    headerArea = r.removeFromTop (44);
    r.removeFromTop (16);
    footerArea = r.removeFromBottom (32);
    r.removeFromBottom (16);
    contentArea = r;

    // header: title block, then the mode switch; pills on the right
    auto h = headerArea;
    h.removeFromLeft (300);
    modeSwitch.setBounds (h.removeFromLeft (238).withSizeKeepingCentre (238, 36));
    pillArea = h.withTrimmedLeft (16);

    // footer
    auto f = footerArea;
    scaleButton.setBounds (f.removeFromRight (64));
    f.removeFromRight (8);
    reset.setBounds (f.removeFromRight (80));
    f.removeFromRight (8);
    hold.setBounds (f.removeFromRight (80));
    f.removeFromRight (12);
    footerTextArea = f;

    hitView.setBounds (contentArea);
    overlapView.setBounds (contentArea);
    const auto fixLocal = overlapShown ? overlapView.getFixPanelBounds() : hitView.getFixPanelBounds();
    fix.setBounds (fixLocal.translated (contentArea.getX(), contentArea.getY()));
}

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);

    // ---- header -----------------------------------------------------------------
    {
        auto h = headerArea;
        auto titleFont = Fonts::inter (20.0f, 700).withExtraKerningFactor (0.04f);
        juce::GlyphArrangement ga;
        ga.addLineOfText (titleFont, "PHASE CHECK", 0, 0);
        const int tw = (int) std::ceil (ga.getBoundingBox (0, -1, true).getWidth());
        g.setFont (titleFont);
        g.setColour (col::text);
        g.drawText ("PHASE CHECK", h.removeFromLeft (tw + 4), juce::Justification::centredLeft, false);
        h.removeFromLeft (10);
        g.setFont (Fonts::inter (12.0f));
        g.setColour (col::text2);
        g.drawText ("kick vs. sidechain", h.withTrimmedTop (4), juce::Justification::centredLeft, false);

        // pills, right-aligned
        const bool live = shown != nullptr && shown->sidechainLive;
        const auto status = shown != nullptr ? shown->sidechain : SidechainStatus::notConnected;

        juce::String scText = ! live ? juce::String ("No sidechain signal")
                            : status == SidechainStatus::stereo ? juce::String ("connected ") + sym::dot + " stereo"
                                                                 : juce::String ("connected ") + sym::dot + " mono";

        struct Pill { juce::String label, value; juce::Colour dot; };
        const Pill pills[] = { { "Main in", "Kick", col::kick },
                               { "Sidechain", scText, live ? col::sidechain : col::tick } };

        auto measure = [] (const juce::String& s, const juce::Font& f)
        {
            juce::GlyphArrangement a;
            a.addLineOfText (f, s, 0, 0);
            return (int) std::ceil (a.getBoundingBox (0, -1, true).getWidth());
        };

        const auto labelFont = Fonts::inter (13.0f);
        const auto valueFont = Fonts::inter (13.0f, 600);
        int x = pillArea.getRight();
        for (int i = 1; i >= 0; --i)
        {
            const auto& p = pills[i];
            const int w = 14 + 8 + 6 + measure (p.label, labelFont) + 6 + measure (p.value, valueFont) + 14;
            auto pr = juce::Rectangle<int> (x - w, pillArea.getCentreY() - 16, w, 32);
            x -= w + 10;

            g.setColour (col::card);
            g.fillRoundedRectangle (pr.toFloat(), 16.0f);
            g.setColour (col::border);
            g.drawRoundedRectangle (pr.toFloat().reduced (0.5f), 16.0f, 1.0f);

            auto c = pr.reduced (14, 0);
            g.setColour (p.dot);
            g.fillEllipse (c.removeFromLeft (8).toFloat().withSizeKeepingCentre (8.0f, 8.0f));
            c.removeFromLeft (6);
            g.setFont (labelFont);
            g.setColour (col::text2);
            g.drawText (p.label, c.removeFromLeft (measure (p.label, labelFont) + 6), juce::Justification::centredLeft, false);
            g.setFont (valueFont);
            g.setColour (i == 1 && ! live ? col::text2 : col::text);
            g.drawText (p.value, c, juce::Justification::centredLeft, false);
        }
    }

    // ---- footer -------------------------------------------------------------------
    {
        juce::String t;
        const auto d = " " + sym::dot + " ";
        if (! overlapShown)
        {
            t << "Window 50 ms" << d << "triggered on kick transient" << d << "analysis below 250 Hz";
        }
        else
        {
            const auto& o = shown != nullptr ? shown->overlap : OverlapResult {};
            t << "Overlap mode" << d;
            if (o.hostTempo && o.bpm > 0)
                t << "1 bar at " << juce::String (o.bpm, std::abs (o.bpm - std::round (o.bpm)) < 0.05 ? 0 : 1) << " BPM";
            else if (o.tempoKnown)
                t << "No host tempo: estimating from kick hits (" << juce::roundToInt (o.bpm) << " BPM)";
            else
                t << "No host tempo: estimating from kick hits";
            t << d << "analysis below 250 Hz" << d << "overlap threshold " << fixedValue (o.thresholdDb, 0) << " dB";
        }
        if (hold.getToggleState())
            t << d << "HOLD";
        g.setFont (Fonts::inter (12.0f));
        g.setColour (col::text2);
        g.drawFittedText (t, footerTextArea, juce::Justification::centredLeft, 1, 0.85f);
    }
}

void MainView::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showScaleMenu();
}

void MainView::showScaleMenu()
{
    juce::PopupMenu m;
    m.setLookAndFeel (&lnf);
    m.addSectionHeader ("Window size");
    const double current = processor.getUiScale();
    for (int pct : { 75, 100, 125, 150 })
        m.addItem (pct, juce::String (pct) + "%", true, std::abs (current * 100.0 - pct) < 0.5);

    juce::Component::SafePointer<MainView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&scaleButton), [safe] (int result)
    {
        if (safe != nullptr && result > 0 && safe->onScaleChosen)
            safe->onScaleChosen (result / 100.0);
    });
}

//==============================================================================
PhaseCheckEditor::PhaseCheckEditor (PhaseCheckProcessor& p)
    : AudioProcessorEditor (p), processor (p), view (p)
{
    addAndMakeVisible (view);
    view.onScaleChosen = [this] (double s) { applyScale (s); };

    setResizable (true, true);
    setResizeLimits (MainView::designWidth / 2, MainView::designHeight / 2,
                     MainView::designWidth * 2, MainView::designHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) MainView::designWidth / (double) MainView::designHeight);

    const double s = processor.getUiScale();
    setSize (juce::roundToInt (MainView::designWidth * s), juce::roundToInt (MainView::designHeight * s));
    initialised = true;
}

PhaseCheckEditor::~PhaseCheckEditor() = default;

void PhaseCheckEditor::applyScale (double s)
{
    processor.setUiScale (s);
    setSize (juce::roundToInt (MainView::designWidth * s), juce::roundToInt (MainView::designHeight * s));
}

void PhaseCheckEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::col::bg);
}

void PhaseCheckEditor::resized()
{
    const double s = (double) getWidth() / (double) MainView::designWidth;
    view.setBounds (0, 0, MainView::designWidth, MainView::designHeight);
    view.setTransform (juce::AffineTransform::scale ((float) s));
    if (initialised && getWidth() > 0)
        processor.setUiScale (s);   // remembered with the session
}

} // namespace pc
