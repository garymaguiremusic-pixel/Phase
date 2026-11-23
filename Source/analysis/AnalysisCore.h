#pragma once

#include "HitAnalyzer.h"
#include "OverlapAnalyzer.h"
#include "Snapshot.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace pc
{

std::string buildHitSuggestion (const HitResult& h, const KickState& s, bool sidechainLive);
std::string buildOverlapSuggestion (const OverlapResult& o, const KickState& s, bool sidechainLive);

//==============================================================================
/** Everything the analysis does, without threads: feed it audio blocks with
    push(), call update() to run pending analyses and publish a snapshot.
    Used by the background worker in the plug-in and directly by the tests. */
class AnalysisCore
{
public:
    static constexpr double historySeconds = 8.0;
    static constexpr double silenceThreshold = 0.001;  // -60 dBFS
    static constexpr double silenceSeconds = 1.0;
    static constexpr double hitStaleSeconds = 3.0;

    void prepare (double sampleRate);
    void requestReset() noexcept { resetRequested = true; }

    void push (const float* processed, const float* raw, const float* sidechain, int n,
               const TransportInfo& transport, const KickState& state);

    /** Marks a gap in the audio stream (dropped blocks). */
    void discontinuity();

    void update();

    /** The host has stopped sending audio (set by the worker thread). */
    void setAudioIdle (bool idle) noexcept { audioIdle = idle; }

    std::shared_ptr<const Snapshot> getSnapshot() const;

    // for tests / diagnostics
    double getSampleRate() const noexcept { return fs; }
    double getTempoEstimateBpm() const noexcept { return fallbackBeat > 0 ? 60.0 * fs / fallbackBeat : 0.0; }
    int getHitCount() const noexcept { return hit.getHitCount(); }
    int getBarCount() const noexcept { return overlap.getBarCount(); }

private:
    bool readRange (std::int64_t start, int n, float* p, float* r, float* s) const;
    void onKickOnset (std::int64_t onset);
    void trackHostBars (const TransportInfo& t, std::int64_t blockStart, int n);
    void doReset();

    double fs = 44100.0;
    std::vector<float> ringP, ringR, ringS;
    std::int64_t mask = 0, total = 0, validFrom = 0;

    OnsetDetector kickDetector;
    std::deque<std::int64_t> kickOnsets;

    struct PendingHit { std::int64_t onset; KickState state; };
    std::deque<PendingHit> pendingHits;

    struct PendingBar { std::int64_t start; int length; int beats, denominator; double bpm; bool host; };
    std::deque<PendingBar> pendingBars;

    // sidechain presence
    std::int64_t scSilentSamples = 0;
    bool scLive = false;
    int scChannels = 0;

    std::int64_t lastHitAt = -1, lastBarEnd = -1;

    // host tempo bar tracking
    bool hostTempoActive = false;
    std::int64_t lastBoundary = -1;
    double hostBpm = 0;
    int hostNum = 4, hostDen = 4;

    // fallback tempo from kick hits
    std::deque<std::int64_t> fallbackIntervals;
    double fallbackBeat = 0;
    std::int64_t fallbackBarStart = -1;

    KickState lastState;

    HitAnalyzer hit;
    OverlapAnalyzer overlap;

    std::atomic<bool> resetRequested { false };
    bool audioIdle = false;

    mutable std::mutex snapshotLock;
    std::shared_ptr<const Snapshot> snapshot;
    std::uint64_t serial = 0;
};

} // namespace pc
