#pragma once

#include "Theme.h"
#include "../analysis/Snapshot.h"

namespace pc::ui
{

/** Hit mode: correlation card, waveform overlay and phase-by-frequency.
    The third column is left free for the shared FixPanel. */
class HitModeView : public juce::Component
{
public:
    HitModeView();

    /** Called at 30 fps with the current snapshot (or the held one). */
    void update (const Snapshot& snap, bool live, double dtSeconds);

    juce::Rectangle<int> getFixPanelBounds() const { return fixBounds; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void paintCorrelation (juce::Graphics&, juce::Rectangle<int>);
    void paintWaveform (juce::Graphics&, juce::Rectangle<int>);
    void paintBands (juce::Graphics&, juce::Rectangle<int>);

    juce::Rectangle<int> corrBounds, waveBounds, bandBounds, fixBounds;

    Snapshot snap;
    bool live = false;
    bool stale = true;

    // meter ballistics (display side)
    double meter = 0.0;
    bool meterSeeded = false;

    // waveform crossfade
    std::vector<float> prevKick, prevSc;
    double prevOnsetMs = 0; bool prevOnsetValid = false;
    int lastHitCount = -1;
    float fade = 1.0f;
};

} // namespace pc::ui
