#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pc
{

/** Exact comparison, for the deliberate bit-exact checks (avoids -Wfloat-equal noise). */
inline bool exactlyEqual (double a, double b) noexcept { return ! (a < b) && ! (b < a); }

enum class Mode : int { hit = 0, overlap = 1 };

/** Parameter state of the kick path, as seen by the audio thread for a block. */
struct KickState
{
    bool invert = false;
    bool delayOn = false;     // the user's button state
    double delayMs = 0.0;     // parameter value
    double rotateDeg = 0.0;
    Mode mode = Mode::hit;
    int scChannels = 0;       // 0 = sidechain bus disabled / not connected

    /** Delay actually applied to the audio (Overlap mode never delays the kick). */
    double effectiveDelayMs() const noexcept { return (delayOn && mode == Mode::hit) ? delayMs : 0.0; }

    bool sameProcessing (const KickState& o) const noexcept
    {
        return invert == o.invert && exactlyEqual (effectiveDelayMs(), o.effectiveDelayMs()) && exactlyEqual (rotateDeg, o.rotateDeg);
    }
};

/** Host transport for one audio block. */
struct TransportInfo
{
    bool playing = false;
    bool hasTempo = false;
    double bpm = 0.0;
    bool hasPpq = false;
    double ppq = 0.0;
    bool hasBarStart = false;
    double ppqBarStart = 0.0;
    int numerator = 4, denominator = 4;
};

enum class SidechainStatus { notConnected, mono, stereo };

//==============================================================================
constexpr int kNumBands = 6;
constexpr std::array<double, kNumBands> kBandCentres { 30.0, 45.0, 60.0, 80.0, 120.0, 200.0 };

struct HitResult
{
    bool hasData = false;          // at least one analysed hit since the last reset
    bool stale = true;             // no recent hits or no sidechain: grey out
    int hitCount = 0;

    double correlation = 0.0;      // averaged over recent hits
    double lastHitCorrelation = 0.0;
    double correlationStdDev = 0.0;

    bool offsetClear = false;
    double offsetMs = 0.0;         // + = sidechain later than the processed kick
    double offsetSamples = 0.0;
    double confidence = 0.0;

    bool sumValid = false;
    double sumHz = 0.0;
    double sumDb = 0.0;

    std::array<double, kNumBands> bandCorrelation {};
    std::array<bool, kNumBands> bandActive {};
    bool lowBandLowConfidence = true;   // 30 Hz band: window shorter than two cycles

    double predictedFlip = 0.0;
    bool predictedDelayValid = false;
    double predictedDelayMs = 0.0;      // total kick delay the Delay button would set
    double predictedDelay = 0.0;
    double predictedBoth = 0.0;
    int bestRotateDeg = 0;
    double predictedBestRotate = 0.0;

    // Waveform overlay of the last hit (0 .. 40 ms after the kick trigger)
    std::vector<float> waveKick, waveSidechain;
    double waveLengthMs = 40.0;
    bool sidechainOnsetValid = false;
    double sidechainOnsetMs = 0.0;
    double windowMs = 50.0;

    std::string suggestion;
};

struct OverlapZone
{
    double startFrac = 0, endFrac = 0;   // position in the displayed bar, 0..1
    char type = 'A';                     // 'A' kick tail -> bass note, 'B' bass tail -> next kick
    bool beating = false;
    bool correlationValid = false;
    double correlation = 0.0;
    double durationMs = 0.0;
    int noteIndex = -1;                  // index into OverlapResult::notes, -1 = note before this bar
};

struct OverlapNote
{
    double posFrac = 0;          // onset position in the bar, 0..1
    std::string position;        // "1&", "4&" ...
    std::string name;            // "A1", "?" when no pitch was found
    double hz = 0.0;
    bool pitchValid = false;
    bool matched = true;
    double beatHz = 0.0;
};

struct OverlapResult
{
    bool hasBar = false;
    bool stale = true;
    int barCount = 0;

    std::vector<float> kickEnvDb, bassEnvDb;   // dB relative to each signal's own peak
    std::vector<OverlapZone> zones;
    std::vector<OverlapNote> notes;

    int beatsPerBar = 4, denominator = 4;
    bool tempoKnown = false;
    bool hostTempo = false;
    double bpm = 0.0;

    bool kickPitchValid = false;
    double kickTailHz = 0.0;
    std::string kickTailNote;

    bool aValid = false; double aCorrelation = 0.0; double aDurationMs = 0.0; int aCount = 0;
    bool bValid = false; double bCorrelation = 0.0; double bDurationMs = 0.0; int bCount = 0;
    bool handoverValid = false; double handoverDb = 0.0;
    int bestRotateDeg = 0; double bestRotateCorrelation = 0.0; bool bestRotateValid = false;

    bool noBass = true;
    bool noOverlap = true;
    double thresholdDb = -22.0;

    std::string suggestion;
};

struct Snapshot
{
    std::uint64_t serial = 0;
    double sampleRate = 44100.0;
    SidechainStatus sidechain = SidechainStatus::notConnected;
    bool sidechainLive = false;      // connected and not silent for > 1 s
    KickState state;
    HitResult hit;
    OverlapResult overlap;
};

} // namespace pc
