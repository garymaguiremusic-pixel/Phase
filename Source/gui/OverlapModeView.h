#pragma once

#include "Theme.h"
#include "../analysis/Snapshot.h"

namespace pc::ui
{

/** Overlap mode: one-bar timeline, overlap zones and pitch check.
    The third column is left free for the shared FixPanel. */
class OverlapModeView : public juce::Component
{
public:
    OverlapModeView();

    void update (const Snapshot& snap, bool live);
    juce::Rectangle<int> getFixPanelBounds() const { return fixBounds; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void paintTimeline (juce::Graphics&, juce::Rectangle<int>);
    void paintZones (juce::Graphics&, juce::Rectangle<int>);
    void paintPitch (juce::Graphics&, juce::Rectangle<int>);

    juce::Rectangle<int> timelineBounds, zonesBounds, pitchBounds, fixBounds;

    Snapshot snap;
    bool live = false;
    bool stale = true;
    std::uint64_t lastSerial = 0;
};

} // namespace pc::ui
