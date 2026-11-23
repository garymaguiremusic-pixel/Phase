#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "analysis/AnalysisEngine.h"
#include "dsp/KickProcessor.h"

namespace pc
{

namespace ParamID
{
    inline constexpr const char* invert  = "invert";
    inline constexpr const char* delayMs = "delayMs";
    inline constexpr const char* delayOn = "delayOn";
    inline constexpr const char* rotate  = "rotate";
    inline constexpr const char* mode    = "mode";
}

class PhaseCheckProcessor final : public juce::AudioProcessor,
                                  private juce::AudioProcessorValueTreeState::Listener,
                                  private juce::AsyncUpdater
{
public:
    PhaseCheckProcessor();
    ~PhaseCheckProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Phase Check"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }
    std::shared_ptr<const Snapshot> getSnapshot() const { return engine.getSnapshot(); }

    /** Footer "Reset": clears the analysis averages and resets invert / delay / rotate. */
    void resetAll();

    /** Editor scale (1.0 = 1040 x 760), stored with the session. */
    double getUiScale() const;
    void setUiScale (double s);

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    void parameterChanged (const juce::String& id, float newValue) override;
    void handleAsyncUpdate() override;
    void processChunk (juce::AudioBuffer<float>& main, const juce::AudioBuffer<float>* sc,
                       int start, int n, const TransportInfo& transport, const KickState& state);

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float>* pInvert = nullptr;
    std::atomic<float>* pDelayMs = nullptr;
    std::atomic<float>* pDelayOn = nullptr;
    std::atomic<float>* pRotate = nullptr;
    std::atomic<float>* pMode = nullptr;

    KickProcessor kick;
    AnalysisEngine engine;

    std::vector<float> rawMono, procMono, scMono;
    double currentRate = 44100.0;

    std::atomic<bool> delayOnTurnedOn { false };
    std::atomic<bool> restoringState { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhaseCheckProcessor)
};

} // namespace pc
