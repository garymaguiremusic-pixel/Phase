#pragma once

#include "Widgets.h"
#include "../analysis/Snapshot.h"

namespace pc::ui
{

/** "Fix it on the kick": invert, delay, phase rotate and the suggestion box. Shared by both modes. */
class FixPanel : public juce::Component
{
public:
    explicit FixPanel (juce::AudioProcessorValueTreeState& state);
    ~FixPanel() override;

    void setOverlapMode (bool isOverlap);
    void update (const Snapshot& snap, bool live);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::AudioProcessorValueTreeState& apvts;

    BigToggle invert { sym::oslash, false, "Invert polarity", "Off", "On: kick inverted" };
    BigToggle delay { sym::mapsTo, true, "Delay kick", "Off", "On: auto-aligned" };

    juce::Slider knob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider slider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> invertAtt, delayAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> knobAtt, sliderAtt;

    bool overlap = false;
    juce::String suggestion;
    bool suggestionLive = false;

    juce::Rectangle<int> titleArea, dashedRow, rotateLabelArea, suggestionArea;
};

} // namespace pc::ui
