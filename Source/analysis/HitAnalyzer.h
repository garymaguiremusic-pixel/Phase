#pragma once

#include "Snapshot.h"

#include <deque>
#include <vector>

namespace pc
{

/** Streaming kick-transient detector: envelope follower with an adaptive
    threshold, hysteresis and a hold time. Runs on the *unprocessed* kick. */
class OnsetDetector
{
public:
    void prepare (double fs);
    void reset();
    bool process (float x) noexcept;

private:
    double fs = 44100.0;
    double env = 0, peakRef = 0, runningMin = 1e9;
    double releaseCoef = 0, peakCoef = 0;
    int holdSamples = 0, sinceLast = 1 << 30;
    bool armed = true;
};

//==============================================================================
/** One captured hit: a segment of the three streams around a kick trigger. */
struct HitWindowInput
{
    const float* processed = nullptr;   // processed kick (after rotate / invert / delay)
    const float* raw = nullptr;         // unprocessed kick
    const float* sidechain = nullptr;
    int length = 0;
    int onsetIndex = 0;                 // trigger position inside the segment
    double sampleRate = 44100.0;
    KickState state;
};

/** Hit-mode analysis (brief section 5A). */
class HitAnalyzer
{
public:
    static constexpr double preRollMs = 5.0;
    static constexpr double windowMs = 50.0;     // hidden constant: post-trigger window
    static constexpr double maxLagMs = 10.0;
    static constexpr double warmUpMs = 100.0;    // filter settling before the window
    static constexpr double maxShiftMs = 30.0;   // room for simulated delays
    static constexpr double waveformMs = 40.0;
    static constexpr int averageHits = 8;        // exponential average length
    static constexpr double unclearConfidence = 0.5;

    static int samplesBeforeOnset (double fs);
    static int samplesAfterOnset (double fs);

    void reset();

    void analyseHit (const HitWindowInput& in);

    /** Fills everything except `stale` and `suggestion`. */
    void fillResult (HitResult& r) const;

    int getHitCount() const noexcept { return hitCount; }

private:
    struct Measurement
    {
        double correlation = 0;
        bool correlationValid = false;
        double lagSamples = 0, confidence = 0;
        bool sumValid = false; double sumHz = 0, sumDb = 0;
        std::array<double, kNumBands> band {};
        std::array<bool, kNumBands> bandActive {};
        double predFlip = 0, predDelay = 0, predBoth = 0, predDelayMs = 0;
        bool predDelayValid = false;
        int bestDeg = 0; double bestCorr = 0;
    };

    void accumulate (const Measurement& m, const KickState& s, double fs);

    bool hasState = false;
    KickState lastState;
    double fs = 44100.0;
    int hitCount = 0, hitsSinceReset = 0;

    double avgCorr = 0, avgSumDb = 0, avgSumHz = 0, avgFlip = 0, avgDelay = 0, avgBoth = 0, avgBest = 0;
    bool sumValid = false, predDelayValid = false;
    double predDelayMs = 0;
    int bestDeg = 0;
    std::array<double, kNumBands> avgBand {};
    std::array<bool, kNumBands> bandActive {};
    std::array<bool, kNumBands> bandSeeded {};
    std::deque<double> recentCorr, recentLagMs, recentConf;

    std::vector<float> waveKick, waveSc;
    bool scOnsetValid = false;
    double scOnsetMs = 0;
};

} // namespace pc
