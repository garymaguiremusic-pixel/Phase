#include "AnalysisCore.h"

#include <algorithm>
#include <cmath>

namespace pc
{

void AnalysisCore::prepare (double sampleRate)
{
    fs = sampleRate > 0 ? sampleRate : 44100.0;

    std::int64_t size = 1;
    while (size < (std::int64_t) (fs * historySeconds)) size <<= 1;
    ringP.assign ((size_t) size, 0.0f);
    ringR.assign ((size_t) size, 0.0f);
    ringS.assign ((size_t) size, 0.0f);
    mask = size - 1;
    total = 0;
    validFrom = 0;

    kickDetector.prepare (fs);
    doReset();

    std::lock_guard<std::mutex> lg (snapshotLock);
    auto s = std::make_shared<Snapshot>();
    s->sampleRate = fs;
    snapshot = s;
}

void AnalysisCore::doReset()
{
    hit.reset();
    overlap.reset();
    pendingHits.clear();
    pendingBars.clear();
    kickOnsets.clear();
    fallbackIntervals.clear();
    fallbackBeat = 0;
    fallbackBarStart = -1;
    lastBoundary = -1;
    lastHitAt = -1;
    lastBarEnd = -1;
    resetRequested = false;
}

void AnalysisCore::discontinuity()
{
    validFrom = total;
    pendingHits.clear();
    pendingBars.clear();
    lastBoundary = -1;
    fallbackBarStart = -1;
    kickDetector.reset();
}

bool AnalysisCore::readRange (std::int64_t start, int n, float* p, float* r, float* s) const
{
    if (n <= 0 || start < validFrom || start < total - (mask + 1) || start + n > total)
        return false;

    for (int i = 0; i < n; ++i)
    {
        const auto idx = (size_t) ((start + i) & mask);
        p[i] = ringP[idx];
        r[i] = ringR[idx];
        s[i] = ringS[idx];
    }
    return true;
}

void AnalysisCore::push (const float* processed, const float* raw, const float* sidechain, int n,
                         const TransportInfo& transport, const KickState& state)
{
    if (n <= 0 || ringP.empty())
        return;

    lastState = state;
    scChannels = state.scChannels;
    const std::int64_t blockStart = total;

    for (int i = 0; i < n; ++i)
    {
        const auto idx = (size_t) ((total + i) & mask);
        ringP[idx] = processed[i];
        ringR[idx] = raw[i];
        const float sc = state.scChannels > 0 && sidechain != nullptr ? sidechain[i] : 0.0f;
        ringS[idx] = sc;

        if (std::abs (sc) > silenceThreshold) scSilentSamples = 0;
        else ++scSilentSamples;

        if (kickDetector.process (raw[i]))
        {
            const std::int64_t onset = total + i;
            kickOnsets.push_back (onset);
            while (kickOnsets.size() > 128) kickOnsets.pop_front();
            pendingHits.push_back ({ onset, state });
            while (pendingHits.size() > 4) pendingHits.pop_front();
            lastHitAt = onset;

            if (! hostTempoActive)
                onKickOnset (onset);
        }
    }
    total += n;

    const bool wasLive = scLive;
    scLive = state.scChannels > 0 && (double) scSilentSamples < silenceSeconds * fs;
    if (wasLive && ! scLive)
    {
        // never let old numbers come back as if they were live
        hit.reset();
        overlap.reset();
        pendingHits.clear();
    }

    const bool host = transport.playing && transport.hasTempo && transport.bpm > 0 && transport.hasPpq;
    if (host != hostTempoActive)
    {
        hostTempoActive = host;
        lastBoundary = -1;
        fallbackBarStart = -1;
        pendingBars.clear();
    }

    if (host)
        trackHostBars (transport, blockStart, n);
}

void AnalysisCore::trackHostBars (const TransportInfo& t, std::int64_t blockStart, int n)
{
    const int num = std::max (1, t.numerator);
    const int den = std::max (1, t.denominator);
    const double barQ = num * 4.0 / den;
    const double spq = fs * 60.0 / t.bpm;
    const double barSamples = barQ * spq;

    hostBpm = t.bpm;
    hostNum = num;
    hostDen = den;

    double barStartQ = t.hasBarStart ? t.ppqBarStart : std::floor ((t.ppq + 1.0e-6) / barQ) * barQ;
    if (barStartQ > t.ppq + 1.0e-6) barStartQ -= barQ;

    const double q0 = t.ppq, q1 = t.ppq + n / spq;
    for (double bq = barStartQ; bq < q1; bq += barQ)
    {
        if (bq < q0 - 1.0e-6) continue;

        const std::int64_t at = blockStart + (std::int64_t) std::llround ((bq - q0) * spq);

        if (lastBoundary >= 0 && (double) (at - lastBoundary) < barSamples * 0.5)
            continue; // the same boundary seen again (rounding across blocks)

        if (lastBoundary >= 0 && std::abs ((double) (at - lastBoundary) - barSamples) <= barSamples * 0.02 + 2.0)
        {
            pendingBars.push_back ({ lastBoundary, (int) (at - lastBoundary), num, den, t.bpm, true });
            while (pendingBars.size() > 3) pendingBars.pop_front();
        }
        lastBoundary = at;
    }
}

void AnalysisCore::onKickOnset (std::int64_t onset)
{
    if (kickOnsets.size() >= 2)
    {
        const auto prev = kickOnsets[kickOnsets.size() - 2];
        const double iv = (double) (onset - prev);
        if (iv > 0.15 * fs && iv < 2.5 * fs)
        {
            fallbackIntervals.push_back (onset - prev);
            while (fallbackIntervals.size() > 16) fallbackIntervals.pop_front();
        }
    }

    if (fallbackIntervals.size() < 3)
        return;

    // beat = mean of intervals close to the median (robust against missed / extra hits)
    std::vector<double> v (fallbackIntervals.begin(), fallbackIntervals.end());
    std::sort (v.begin(), v.end());
    const double med = v[v.size() / 2];
    double sum = 0; int cnt = 0;
    for (double x : v)
        if (std::abs (x - med) <= 0.05 * med) { sum += x; ++cnt; }
    fallbackBeat = cnt > 0 ? sum / cnt : med;

    if (fallbackBarStart < 0)
    {
        fallbackBarStart = onset;
        return;
    }

    const double since = (double) (onset - fallbackBarStart);
    if (since >= 3.5 * fallbackBeat)
    {
        const int len = since <= 4.5 * fallbackBeat ? (int) since : (int) std::lround (4.0 * fallbackBeat);
        pendingBars.push_back ({ fallbackBarStart, len, 4, 4, 60.0 * fs / fallbackBeat, false });
        while (pendingBars.size() > 3) pendingBars.pop_front();
        fallbackBarStart = onset;
    }
}

void AnalysisCore::update()
{
    if (ringP.empty())
        return;

    if (resetRequested.exchange (false))
        doReset();

    // ---- hits ----------------------------------------------------------------
    const int before = HitAnalyzer::samplesBeforeOnset (fs);
    const int after = HitAnalyzer::samplesAfterOnset (fs);

    while (! pendingHits.empty() && pendingHits.front().onset + after <= total)
    {
        const auto ph = pendingHits.front();
        pendingHits.pop_front();

        if (! scLive)
            continue;

        const int len = before + after;
        std::vector<float> p ((size_t) len), r ((size_t) len), s ((size_t) len);
        if (! readRange (ph.onset - before, len, p.data(), r.data(), s.data()))
            continue;

        HitWindowInput in;
        in.processed = p.data();
        in.raw = r.data();
        in.sidechain = s.data();
        in.length = len;
        in.onsetIndex = before;
        in.sampleRate = fs;
        in.state = ph.state;
        hit.analyseHit (in);
    }

    // ---- bars ----------------------------------------------------------------
    while (! pendingBars.empty())
    {
        const auto pb = pendingBars.front();
        const std::int64_t tail = (std::int64_t) (0.010 * fs);
        if (pb.start + pb.length + tail > total)
            break;
        pendingBars.pop_front();

        const double beatSec = (double) pb.length / std::max (1, pb.beats) / fs;
        const int pre = OverlapAnalyzer::preRollSamples (fs, beatSec);
        const int len = pre + pb.length + (int) tail;
        if ((double) pb.length > 6.0 * fs)
            continue;

        std::vector<float> p ((size_t) len), r ((size_t) len), s ((size_t) len);
        if (! readRange (pb.start - pre, len, p.data(), r.data(), s.data()))
            continue;

        BarInput in;
        in.processed = p.data();
        in.raw = r.data();
        in.sidechain = s.data();
        in.length = len;
        in.barStart = pre;
        in.barLength = pb.length;
        in.sampleRate = fs;
        in.state = lastState;
        in.beatsPerBar = pb.beats;
        in.denominator = pb.denominator;
        in.bpm = pb.bpm;
        in.hostTempo = pb.host;
        for (auto k : kickOnsets)
            if (k >= pb.start - pre && k < pb.start - pre + len)
                in.kickOnsets.push_back ((int) (k - (pb.start - pre)));

        overlap.analyseBar (in);
        lastBarEnd = pb.start + pb.length;
    }

    // ---- publish ---------------------------------------------------------------
    const bool live = scLive && ! audioIdle;
    auto snap = std::make_shared<Snapshot>();
    snap->serial = ++serial;
    snap->sampleRate = fs;
    snap->state = lastState;
    snap->sidechain = scChannels >= 2 ? SidechainStatus::stereo
                    : scChannels == 1 ? SidechainStatus::mono
                                      : SidechainStatus::notConnected;
    snap->sidechainLive = live;

    hit.fillResult (snap->hit);
    snap->hit.stale = ! live || ! snap->hit.hasData || lastHitAt < 0
                      || (double) (total - lastHitAt) > hitStaleSeconds * fs;
    snap->hit.suggestion = buildHitSuggestion (snap->hit, lastState, live);

    overlap.fillResult (snap->overlap);
    {
        auto& o = snap->overlap;
        if (hostTempoActive)
        {
            o.tempoKnown = true; o.hostTempo = true; o.bpm = hostBpm;
            o.beatsPerBar = hostNum; o.denominator = hostDen;
        }
        else
        {
            o.hostTempo = false;
            o.tempoKnown = fallbackBeat > 0;
            o.bpm = fallbackBeat > 0 ? 60.0 * fs / fallbackBeat : 0.0;
        }

        const double barSec = o.bpm > 0 ? o.beatsPerBar * (4.0 / o.denominator) * 60.0 / o.bpm : 2.0;
        o.stale = ! live || ! o.hasBar || lastBarEnd < 0
                  || (double) (total - lastBarEnd) > std::max (4.0, 2.5 * barSec) * fs;
    }
    snap->overlap.suggestion = buildOverlapSuggestion (snap->overlap, lastState, live);

    std::lock_guard<std::mutex> lg (snapshotLock);
    snapshot = std::move (snap);
}

std::shared_ptr<const Snapshot> AnalysisCore::getSnapshot() const
{
    std::lock_guard<std::mutex> lg (snapshotLock);
    return snapshot;
}

} // namespace pc
