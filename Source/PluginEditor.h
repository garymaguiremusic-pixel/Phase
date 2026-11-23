#pragma once

#include "PluginProcessor.h"
#include "gui/FixPanel.h"
#include "gui/HitModeView.h"
#include "gui/OverlapModeView.h"
#include "gui/Widgets.h"

namespace pc
{

/** Everything, laid out at the design size (1040 x 760) and scaled as a whole. */
class MainView : public juce::Component, private juce::Timer
{
public:
    static constexpr int designWidth = 1040, designHeight = 760;

    explicit MainView (PhaseCheckProcessor&);
    ~MainView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    std::function<void (double)> onScaleChosen;
    void showScaleMenu();

private:
    void timerCallback() override;
    bool isOverlap() const;

    PhaseCheckProcessor& processor;
    ui::PhaseLookAndFeel lnf;

    ui::ModeSwitch modeSwitch;
    ui::HitModeView hitView;
    ui::OverlapModeView overlapView;
    ui::FixPanel fix;

    ui::FlatButton hold { "Hold", ui::FlatButton::Style::footer };
    ui::FlatButton reset { "Reset", ui::FlatButton::Style::footer };
    ui::FlatButton scaleButton { "100%", ui::FlatButton::Style::footer };

    std::shared_ptr<const Snapshot> shown;
    bool overlapShown = false;
    double lastTime = 0;

    juce::Rectangle<int> headerArea, contentArea, footerArea, pillArea, footerTextArea;
};

class PhaseCheckEditor : public juce::AudioProcessorEditor
{
public:
    explicit PhaseCheckEditor (PhaseCheckProcessor&);
    ~PhaseCheckEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void applyScale (double s);

    PhaseCheckProcessor& processor;
    MainView view;
    bool initialised = false;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhaseCheckEditor)
};

} // namespace pc
