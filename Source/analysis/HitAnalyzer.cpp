#include "HitAnalyzer.h"
#include "AnalysisUtils.h"
#include "../dsp/HilbertRotator.h"

#include <algorithm>
#include <cmath>

namespace pc
{

//==============================================================================
void OnsetDetector::prepare (double sampleRate)
{
    fs = sampleRate > 0 ? sampleRate : 44100.0;
    releaseCoef = std::exp (-1.0 / (0.015 * fs));
    peakCoef = std::exp (-1.0 / (2.0 * fs));
    holdSamples = (int) (0.080 * fs);
    reset();
}

void OnsetDetector::reset()
{
    env = 0; peakRef = 0; runningMin = 1e9;
    sinceLast = 1 << 30;
    armed = true;
}

bool OnsetDetector::process (float x) noexcept
{
    const double a = std::abs ((double) x);
    env = a > env ? a : env * releaseCoef;
    peakRef = std::max (env, peakRef * peakCoef);

    const double thrOn = std::max (0.0032, peakRef * 0.1);   // -50 dBFS floor, -20 dB below recent peak
    const double thrOff = thrOn * 0.5;                       // 6 dB hysteresis

    if (sinceLast < (1 << 30)) ++sinceLast;

    if (sinceLast > holdSamples)
    {
        runningMin = std::min (runningMin, env);
        if (env < thrOff) armed = true;

        // Re-trigger on a clear new rise even if a long tail never fell below thrOff
        const bool rise = env > runningMin * 4.0;

        if (env > thrOn && (armed || rise))
        {
            armed = false;
            sinceLast = 0;
            runningMin = 1e9;
            return true;
        }
    }
    return false;
}

//==============================================================================
int HitAnalyzer::samplesBeforeOnset (double fs)
{
    return (int) std::ceil (fs * (warmUpMs + maxShiftMs + maxLagMs + preRollMs) / 1000.0) + 4;
}

int HitAnalyzer::samplesAfterOnset (double fs)
{
    return (int) std::ceil (fs * (windowMs + maxLagMs) / 1000.0) + 4;
}

void HitAnalyzer::reset()
{
    hasState = false;
    hitsSinceReset = 0;
    recentCorr.clear(); recentLagMs.clear(); recentConf.clear();
    bandSeeded = {};
    bandActive = {};
    sumValid = false;
    predDelayValid = false;
    waveKick.clear(); waveSc.clear();
    scOnsetValid = false;
}

void HitAnalyzer::analyseHit (const HitWindowInput& in)
{
    fs = in.sampleRate;
    const int n = in.length;
    if (n <= 0 || in.onsetIndex < samplesBeforeOnset (fs) - 4 || in.onsetIndex + samplesAfterOnset (fs) - 4 > n)
        return;

    const int pre = (int) std::lround (fs * preRollMs / 1000.0);
    const int post = (int) std::lround (fs * windowMs / 1000.0);
    const int maxLag = (int) std::lround (fs * maxLagMs / 1000.0);
    const int w0 = in.onsetIndex - pre;
    const int W = pre + post;

    // 1. band-limit everything with identical filters (from the start of the warm-up)
    std::vector<float> kLP ((size_t) n), sLP ((size_t) n), rLP ((size_t) n);
    lowPass (in.processed, kLP.data(), n, fs);
    lowPass (in.sidechain, sLP.data(), n, fs);
    lowPass (in.raw, rLP.data(), n, fs);

    Measurement m;

    // 2. time offset (sidechain relative to the processed kick)
    const auto xc = crossCorrelate (kLP.data() + w0, sLP.data() + w0, W, maxLag);
    m.lagSamples = xc.valid ? xc.lagSamples : 0.0;
    m.confidence = xc.valid ? std::abs (xc.peak) : 0.0;

    // 3. correlation at zero lag
    m.correlation = pearson (kLP.data() + w0, sLP.data() + w0, W, &m.correlationValid);
    if (! m.correlationValid)
        return; // silent sidechain or kick: nothing meaningful to add

    // 4. low-end sum at the kick fundamental
    {
        const double f0 = estimatePitchHz (kLP.data() + in.onsetIndex, post, fs, 35.0, 120.0);
        if (f0 > 0)
        {
            std::vector<float> sum ((size_t) post);
            for (int i = 0; i < post; ++i)
                sum[(size_t) i] = kLP[(size_t) (in.onsetIndex + i)] + sLP[(size_t) (in.onsetIndex + i)];

            const double gk = goertzelMagnitude (kLP.data() + in.onsetIndex, post, fs, f0);
            const double gs = goertzelMagnitude (sum.data(), post, fs, f0);
            if (gk > 1.0e-9)
            {
                m.sumValid = true;
                m.sumHz = f0;
                m.sumDb = std::clamp (dbFromRatio (gs / gk), -60.0, 12.0);
            }
        }
    }

    // 5. per-band phase
    {
        const double q = octaveBandwidthToQ (0.5);
        std::array<double, kNumBands> eK {}, eS {};
        std::vector<float> bk ((size_t) n), bs ((size_t) n);
        for (int b = 0; b < kNumBands; ++b)
        {
            bandPass (in.processed, bk.data(), n, fs, kBandCentres[(size_t) b], q);
            bandPass (in.sidechain, bs.data(), n, fs, kBandCentres[(size_t) b], q);
            bool ok = false;
            m.band[(size_t) b] = pearson (bk.data() + w0, bs.data() + w0, W, &ok);
            eK[(size_t) b] = ok ? rms (bk.data() + w0, W) : 0.0;
            eS[(size_t) b] = ok ? rms (bs.data() + w0, W) : 0.0;
        }
        const double maxK = *std::max_element (eK.begin(), eK.end());
        const double maxS = *std::max_element (eS.begin(), eS.end());
        const double rel = std::pow (10.0, -40.0 / 20.0);
        for (int b = 0; b < kNumBands; ++b)
            m.bandActive[(size_t) b] = maxK > 0 && maxS > 0
                                       && eK[(size_t) b] >= maxK * rel && eS[(size_t) b] >= maxS * rel;
    }

    // 6. predictions, simulated on the unprocessed kick through the real chain
    {
        const double pol = in.state.invert ? -1.0 : 1.0;
        const double dCur = in.state.effectiveDelayMs() * fs / 1000.0;
        const double dTot = dCur + m.lagSamples;
        const double maxD = fs * maxShiftMs / 1000.0;

        std::vector<float> base ((size_t) n), flipped ((size_t) n), tmp ((size_t) n);
        rotateBuffer (rLP.data(), base.data(), n, fs, in.state.rotateDeg);
        for (int i = 0; i < n; ++i)
        {
            flipped[(size_t) i] = (float) (-pol * base[(size_t) i]);
            base[(size_t) i] = (float) (pol * base[(size_t) i]);
        }

        m.predFlip = correlationWithDelay (flipped.data() + w0, sLP.data() + w0, W, dCur);

        m.predDelayValid = m.confidence >= unclearConfidence && m.lagSamples > 0.0 && dTot <= maxD
                           && dTot * 1000.0 / fs <= 20.0;
        if (m.predDelayValid)
        {
            m.predDelayMs = dTot * 1000.0 / fs;
            m.predDelay = correlationWithDelay (base.data() + w0, sLP.data() + w0, W, dTot);
            m.predBoth = correlationWithDelay (flipped.data() + w0, sLP.data() + w0, W, dTot);
        }

        m.bestCorr = -2.0;
        for (int deg = 0; deg <= 180; deg += 5)
        {
            rotateBuffer (rLP.data(), tmp.data(), n, fs, (double) deg);
            for (auto& v : tmp) v = (float) (v * pol);
            const double r = correlationWithDelay (tmp.data() + w0, sLP.data() + w0, W, dCur);
            if (r > m.bestCorr) { m.bestCorr = r; m.bestDeg = deg; }
        }
    }

    // 7. waveform overlay (full band, 0 .. 40 ms after the trigger)
    {
        const int len = std::min ((int) std::lround (fs * waveformMs / 1000.0), n - in.onsetIndex);
        const int stride = std::max (1, len / 1024);
        float peak = 0.0f;
        for (int i = 0; i < len; ++i) peak = std::max (peak, std::abs (in.processed[in.onsetIndex + i]));
        if (peak <= 1.0e-6f)
            for (int i = 0; i < len; ++i) peak = std::max (peak, std::abs (in.sidechain[in.onsetIndex + i]));
        const float g = peak > 1.0e-6f ? 1.0f / peak : 0.0f;

        waveKick.clear(); waveSc.clear();
        for (int i = 0; i < len; i += stride)
        {
            waveKick.push_back (std::clamp (in.processed[in.onsetIndex + i] * g, -1.5f, 1.5f));
            waveSc.push_back (std::clamp (in.sidechain[in.onsetIndex + i] * g, -1.5f, 1.5f));
        }

        scOnsetValid = m.confidence >= unclearConfidence;
        scOnsetMs = in.state.effectiveDelayMs() + m.lagSamples * 1000.0 / fs;
    }

    accumulate (m, in.state, fs);
}

void HitAnalyzer::accumulate (const Measurement& m, const KickState& s, double sampleRate)
{
    // A change to the kick processing invalidates the running averages:
    // start again so the meters follow the fix immediately.
    if (! hasState || ! s.sameProcessing (lastState))
    {
        hitsSinceReset = 0;
        recentCorr.clear(); recentLagMs.clear(); recentConf.clear();
        bandSeeded = {};
    }
    hasState = true;
    lastState = s;
    ++hitCount;

    const double alpha = hitsSinceReset == 0 ? 1.0 : 2.0 / (averageHits + 1.0);
    auto ema = [alpha] (double& acc, double v) { acc += alpha * (v - acc); };

    ema (avgCorr, m.correlation);
    ema (avgFlip, m.predFlip);
    ema (avgBest, m.bestCorr);

    if (m.sumValid)
    {
        if (! sumValid || hitsSinceReset == 0) { avgSumDb = m.sumDb; avgSumHz = m.sumHz; }
        else { ema (avgSumDb, m.sumDb); ema (avgSumHz, m.sumHz); }
        sumValid = true;
    }

    predDelayValid = m.predDelayValid;
    if (m.predDelayValid)
    {
        if (hitsSinceReset == 0) { avgDelay = m.predDelay; avgBoth = m.predBoth; }
        else { ema (avgDelay, m.predDelay); ema (avgBoth, m.predBoth); }
    }

    bestDeg = m.bestDeg;

    for (int b = 0; b < kNumBands; ++b)
    {
        bandActive[(size_t) b] = m.bandActive[(size_t) b];
        if (! bandSeeded[(size_t) b]) { avgBand[(size_t) b] = m.band[(size_t) b]; bandSeeded[(size_t) b] = true; }
        else ema (avgBand[(size_t) b], m.band[(size_t) b]);
    }

    auto pushLimited = [] (std::deque<double>& d, double v)
    {
        d.push_back (v);
        while ((int) d.size() > averageHits) d.pop_front();
    };

    pushLimited (recentCorr, m.correlation);
    pushLimited (recentConf, m.confidence);
    if (m.confidence >= unclearConfidence)
        pushLimited (recentLagMs, m.lagSamples * 1000.0 / sampleRate);

    // median of the recent clear offsets -> the delay the button would apply
    if (m.predDelayValid && ! recentLagMs.empty())
    {
        auto v = std::vector<double> (recentLagMs.begin(), recentLagMs.end());
        std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
        predDelayMs = std::clamp (s.effectiveDelayMs() + v[v.size() / 2], 0.0, 20.0);
    }

    ++hitsSinceReset;
}

void HitAnalyzer::fillResult (HitResult& r) const
{
    r.hasData = hitsSinceReset > 0;
    r.hitCount = hitCount;
    r.windowMs = windowMs;
    r.lowBandLowConfidence = windowMs < 2.0 * 1000.0 / kBandCentres[0];

    if (! r.hasData)
        return;

    r.correlation = avgCorr;
    r.lastHitCorrelation = recentCorr.empty() ? 0.0 : recentCorr.back();

    if (recentCorr.size() >= 2)
    {
        double mean = 0;
        for (auto v : recentCorr) mean += v;
        mean /= (double) recentCorr.size();
        double var = 0;
        for (auto v : recentCorr) var += (v - mean) * (v - mean);
        r.correlationStdDev = std::sqrt (var / (double) recentCorr.size());
    }

    double conf = 0;
    for (auto c : recentConf) conf += c;
    r.confidence = recentConf.empty() ? 0.0 : conf / (double) recentConf.size();

    r.offsetClear = ! recentLagMs.empty() && r.confidence >= unclearConfidence;
    if (r.offsetClear)
    {
        auto v = std::vector<double> (recentLagMs.begin(), recentLagMs.end());
        std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
        r.offsetMs = v[v.size() / 2];
        r.offsetSamples = r.offsetMs * fs / 1000.0;
    }

    r.sumValid = sumValid;
    r.sumHz = avgSumHz;
    r.sumDb = avgSumDb;

    r.bandCorrelation = avgBand;
    r.bandActive = bandActive;

    r.predictedFlip = avgFlip;
    r.predictedDelayValid = predDelayValid && r.offsetClear && r.offsetMs > 0;
    r.predictedDelayMs = predDelayMs;
    r.predictedDelay = avgDelay;
    r.predictedBoth = avgBoth;
    r.bestRotateDeg = bestDeg;
    r.predictedBestRotate = avgBest;

    r.waveKick = waveKick;
    r.waveSidechain = waveSc;
    r.waveLengthMs = waveformMs;
    r.sidechainOnsetValid = scOnsetValid;
    r.sidechainOnsetMs = scOnsetMs;
}

} // namespace pc
