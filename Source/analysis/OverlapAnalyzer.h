#pragma once

#include "Snapshot.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <vector>

namespace pc
{

/** One bar of audio plus some pre-roll, handed to the overlap analysis. */
struct BarInput
{
    const float* processed = nullptr;
    const float* raw = nullptr;
    const float* sidechain = nullptr;
    int length = 0;              // whole segment, pre-roll included
    int barStart = 0;            // bar start inside the segment
    int barLength = 0;
    std::vector<int> kickOnsets; // kick triggers inside the segment (from the streaming detector)
    double sampleRate = 44100.0;
    KickState state;
    int beatsPerBar = 4;
    int denominator = 4;
    double bpm = 0.0;
    bool hostTempo = false;
};

/** Overlap-mode analysis (brief section 5B). */
class OverlapAnalyzer
{
public:
    static constexpr double thresholdDb = -22.0;      // hidden constant, shown in the footer
    static constexpr double minZoneMs = 4.0;
    static constexpr double typeBWindowMs = 25.0;
    static constexpr double envWindowMs = 5.0;
    static constexpr double envHopMs = 1.0;
    static constexpr double kickTailStartMs = 60.0;
    static constexpr int barsToAverage = 4;
    static constexpr int sweepSteps = 37;             // 0..180 in 5 degree steps
    static constexpr int displayPoints = 480;

    /** Pre-roll needed before the bar start, in samples. */
    static int preRollSamples (double fs, double beatSeconds);

    void reset();
    void analyseBar (const BarInput& in);
    void fillResult (OverlapResult& r) const;

    int getBarCount() const noexcept { return barCount; }

private:
    struct Pool
    {
        double ks = 0, kk = 0, ss = 0;
        double durMs = 0; int count = 0;
        void add (const Pool& o) { ks += o.ks; kk += o.kk; ss += o.ss; durMs += o.durMs; count += o.count; }
        bool valid() const { return count > 0 && kk > 1e-14 && ss > 1e-14; }
        double corr() const { return valid() ? std::max (-1.0, std::min (1.0, ks / std::sqrt (kk * ss))) : 0.0; }
    };

    struct BarStats
    {
        Pool a, b;
        double handoverSumDb = 0; int handoverCount = 0;
        std::array<Pool, sweepSteps> sweep {};
    };

    std::deque<BarStats> history;
    bool hasState = false;
    KickState lastState;
    int barCount = 0;

    // Latest bar, for display
    OverlapResult latest;
};

} // namespace pc
