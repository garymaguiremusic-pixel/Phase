#include "OverlapAnalyzer.h"
#include "AnalysisUtils.h"
#include "../dsp/HilbertRotator.h"

#include <algorithm>
#include <cmath>

namespace pc
{

namespace
{
    /** Offline transient detector for bass-note onsets in the (low-passed) sidechain. */
    std::vector<int> detectNoteOnsets (const float* x, int n, double fs, double gateLevel)
    {
        std::vector<int> onsets;
        const double rel = std::exp (-1.0 / (0.020 * fs));
        const double slowCoef = 1.0 - std::exp (-1.0 / (0.060 * fs));
        const int hold = (int) (0.080 * fs);

        double fast = 0, slow = 0;
        int since = hold + 1;
        bool armed = true;

        for (int i = 0; i < n; ++i)
        {
            const double a = std::abs ((double) x[i]);
            fast = a > fast ? a : fast * rel;
            slow += (fast - slow) * slowCoef;
            ++since;

            if (! armed && fast < slow * 1.25)
                armed = true;

            if (armed && since > hold && fast > gateLevel && fast > slow * 2.0)
            {
                onsets.push_back (i);
                armed = false;
                since = 0;
            }
        }
        return onsets;
    }

    std::string positionLabel (double posBeats)
    {
        const int idx = std::max (0, (int) std::lround (posBeats * 2.0));
        return std::to_string (idx / 2 + 1) + ((idx % 2) ? "&" : "");
    }

    double median (std::vector<double> v)
    {
        if (v.empty()) return 0.0;
        std::sort (v.begin(), v.end());
        const size_t m = v.size() / 2;
        return (v.size() % 2) ? v[m] : 0.5 * (v[m - 1] + v[m]);
    }
}

int OverlapAnalyzer::preRollSamples (double fs, double beatSeconds)
{
    const double s = std::clamp (beatSeconds + 0.12, 0.15, 1.6);
    return (int) std::ceil (s * fs);
}

void OverlapAnalyzer::reset()
{
    history.clear();
    hasState = false;
    latest = {};
}

void OverlapAnalyzer::analyseBar (const BarInput& in)
{
    const double fs = in.sampleRate;
    const int n = in.length;
    const int bs = in.barStart, bl = in.barLength;
    if (n <= 0 || bl <= 0 || bs < 0 || bs + bl > n)
        return;

    if (! hasState || in.state.invert != lastState.invert || ! exactlyEqual (in.state.rotateDeg, lastState.rotateDeg))
        history.clear();
    hasState = true;
    lastState = in.state;
    ++barCount;

    // ---- band-limit (identical filters) -----------------------------------
    std::vector<float> kLP ((size_t) n), rLP ((size_t) n), sLP ((size_t) n);
    lowPass (in.processed, kLP.data(), n, fs);
    lowPass (in.raw, rLP.data(), n, fs);
    lowPass (in.sidechain, sLP.data(), n, fs);

    // ---- RMS envelopes, 5 ms window, 1 ms hop -------------------------------
    const int win = std::max (2, (int) std::lround (fs * envWindowMs / 1000.0));
    const int hop = std::max (1, (int) std::lround (fs * envHopMs / 1000.0));
    const int F = n >= win ? 1 + (n - win) / hop : 0;
    if (F < 4) return;

    // RMS of the local quadrature amplitude: same 5 ms / 1 ms framing as a plain
    // RMS envelope, but without the ripple a 5 ms window has on a 50 Hz tail.
    auto envelope = [&] (const std::vector<float>& x)
    {
        const auto pw = quadraturePower (x.data(), n, fs);
        std::vector<double> prefix ((size_t) n + 1, 0.0);
        for (int i = 0; i < n; ++i) prefix[(size_t) i + 1] = prefix[(size_t) i] + pw[(size_t) i];
        std::vector<double> e ((size_t) F);
        for (int f = 0; f < F; ++f)
            e[(size_t) f] = std::sqrt (std::max (0.0, (prefix[(size_t) (f * hop + win)] - prefix[(size_t) (f * hop)]) / win));
        return e;
    };

    const auto eK = envelope (kLP), eS = envelope (sLP), eR = envelope (rLP);

    auto centre = [&] (int f) { return f * hop + win / 2; };
    const int fA = std::clamp ((int) std::ceil ((double) (bs - win / 2) / hop), 0, F - 1);
    const int fB = std::clamp ((int) std::floor ((double) (bs + bl - 1 - win / 2) / hop), 0, F - 1);

    double pK = 0, pS = 0, pR = 0;
    for (int f = fA; f <= fB; ++f)
    {
        pK = std::max (pK, eK[(size_t) f]);
        pS = std::max (pS, eS[(size_t) f]);
        pR = std::max (pR, eR[(size_t) f]);
    }

    auto toDb = [] (double v, double peak) { return peak > 1e-9 ? dbFromRatio (v / peak) : -120.0; };
    std::vector<double> dK ((size_t) F), dS ((size_t) F), dR ((size_t) F);
    for (int f = 0; f < F; ++f)
    {
        dK[(size_t) f] = toDb (eK[(size_t) f], pK);
        dS[(size_t) f] = toDb (eS[(size_t) f], pS);
        dR[(size_t) f] = toDb (eR[(size_t) f], pR);
    }

    const double msToS = fs / 1000.0;
    auto frameAt = [&] (int sample) { return std::clamp ((sample - win / 2) / hop, 0, F - 1); };

    OverlapResult res;
    res.hasBar = true;
    res.beatsPerBar = in.beatsPerBar;
    res.denominator = in.denominator;
    res.bpm = in.bpm;
    res.tempoKnown = in.bpm > 0;
    res.hostTempo = in.hostTempo;
    res.thresholdDb = thresholdDb;

    // ---- display envelopes ------------------------------------------------
    {
        res.kickEnvDb.resize (displayPoints);
        res.bassEnvDb.resize (displayPoints);
        for (int p = 0; p < displayPoints; ++p)
        {
            const int s0 = bs + (int) ((double) p / displayPoints * bl);
            const int s1 = bs + (int) ((double) (p + 1) / displayPoints * bl);
            const int f0 = frameAt (s0), f1 = std::max (f0, frameAt (s1));
            double mk = -120, ms = -120;
            for (int f = f0; f <= f1; ++f)
            {
                mk = std::max (mk, dK[(size_t) f]);
                ms = std::max (ms, dS[(size_t) f]);
            }
            res.kickEnvDb[(size_t) p] = (float) std::max (-60.0, mk);
            res.bassEnvDb[(size_t) p] = (float) std::max (-60.0, pS > 1e-6 ? ms : -60.0);
        }
    }

    // ---- kick onsets & kick tail pitch ------------------------------------
    std::vector<int> kicks = in.kickOnsets;
    std::sort (kicks.begin(), kicks.end());

    std::vector<double> tailPitches;
    for (size_t i = 0; i < kicks.size(); ++i)
    {
        const int k = kicks[i];
        if (k < bs || k >= bs + bl) continue;
        const int next = i + 1 < kicks.size() ? kicks[i + 1] : n;
        const int a = k + (int) (kickTailStartMs * msToS);
        int e = std::min (next, std::min (n, a + (int) (400.0 * msToS)));
        // end of tail: last frame above -35 dB
        int lastLoud = a;
        for (int f = frameAt (a); f < F && centre (f) < e; ++f)
            if (dR[(size_t) f] >= -35.0) lastLoud = centre (f);
        e = std::min (e, lastLoud + hop);
        if (e - a >= (int) (40.0 * msToS))
        {
            const double hz = estimatePitchHz (rLP.data() + a, e - a, fs, 30.0, 120.0);
            if (hz > 0) tailPitches.push_back (hz);
        }
    }

    if (! tailPitches.empty())
    {
        res.kickPitchValid = true;
        res.kickTailHz = median (tailPitches);
        res.kickTailNote = noteName (res.kickTailHz);
    }

    // ---- bass notes ---------------------------------------------------------
    struct Note { int onset; bool inBar; double hz; bool pitchValid; bool matched; double beatHz; int displayIndex; };
    std::vector<Note> notes;
    {
        double scPeak = 0;
        for (int i = bs; i < bs + bl; ++i) scPeak = std::max (scPeak, (double) std::abs (sLP[(size_t) i]));

        if (scPeak > 1.0e-5)
        {
            const auto ons = detectNoteOnsets (sLP.data(), n, fs, scPeak * 0.0316);
            for (size_t i = 0; i < ons.size(); ++i)
            {
                Note nt {};
                nt.onset = ons[i];
                nt.inBar = nt.onset >= bs && nt.onset < bs + bl;
                nt.displayIndex = -1;
                const int next = i + 1 < ons.size() ? ons[i + 1] : n;

                int lastLoud = nt.onset;
                for (int f = frameAt (nt.onset + (int) (10 * msToS)); f < F && centre (f) < next; ++f)
                    if (dS[(size_t) f] >= -30.0) lastLoud = centre (f);

                const int a = nt.onset + (int) (15.0 * msToS);
                const int e = std::min (next, lastLoud) - (int) (5.0 * msToS);
                nt.hz = e - a >= (int) (40.0 * msToS) ? estimatePitchHz (sLP.data() + a, e - a, fs, 30.0, 160.0) : 0.0;
                nt.pitchValid = nt.hz > 0;
                nt.matched = true;

                if (nt.pitchValid && res.kickPitchValid)
                {
                    const double diff = std::abs (nt.hz - res.kickTailHz);
                    const double tol = std::max (1.5, res.kickTailHz * (std::pow (2.0, 25.0 / 1200.0) - 1.0));
                    nt.matched = diff <= tol;
                    nt.beatHz = diff;
                }
                notes.push_back (nt);
            }
        }
    }

    const double beatSamples = (double) bl / std::max (1, in.beatsPerBar);
    for (auto& nt : notes)
    {
        if (! nt.inBar) continue;
        OverlapNote on;
        on.posFrac = (double) (nt.onset - bs) / bl;
        on.position = positionLabel ((nt.onset - bs) / beatSamples);
        on.pitchValid = nt.pitchValid;
        on.hz = nt.hz;
        on.name = nt.pitchValid ? noteName (nt.hz) : std::string ("?");
        on.matched = nt.matched;
        on.beatHz = nt.beatHz;
        nt.displayIndex = (int) res.notes.size();
        res.notes.push_back (on);
    }
    res.noBass = res.notes.empty();

    // ---- overlap zones ------------------------------------------------------
    struct Zone { int s, e; char type; int note; bool beating; };
    std::vector<Zone> zones;
    {
        int runStart = -1;
        for (int f = fA; f <= fB + 1; ++f)
        {
            const bool both = f <= fB && dK[(size_t) f] >= thresholdDb && dS[(size_t) f] >= thresholdDb;
            if (both && runStart < 0) runStart = f;
            if (! both && runStart >= 0)
            {
                const int s = std::max (bs, centre (runStart) - hop / 2);
                const int e = std::min (bs + bl, centre (f - 1) + hop / 2 + (hop % 2));
                if (e - s >= (int) std::lround (minZoneMs * msToS))
                {
                    Zone z { s, e, 'A', -1, false };
                    for (int k : kicks)
                        if (std::abs (s - k) <= (int) (typeBWindowMs * msToS)) { z.type = 'B'; break; }

                    const int tol = (int) (3.0 * msToS);
                    for (int i = (int) notes.size() - 1; i >= 0; --i)
                        if (notes[(size_t) i].onset <= s + tol) { z.note = i; break; }

                    z.beating = z.note >= 0 && ! notes[(size_t) z.note].matched;
                    zones.push_back (z);
                }
                runStart = -1;
            }
        }
    }
    res.noOverlap = zones.empty();

    BarStats stats;
    for (auto& z : zones)
    {
        const int len = z.e - z.s;
        OverlapZone oz;
        oz.startFrac = (double) (z.s - bs) / bl;
        oz.endFrac = (double) (z.e - bs) / bl;
        oz.type = z.type;
        oz.beating = z.beating;
        oz.durationMs = len / msToS;
        oz.noteIndex = z.note >= 0 ? notes[(size_t) z.note].displayIndex : -1;
        oz.correlation = pearson (kLP.data() + z.s, sLP.data() + z.s, len, &oz.correlationValid);
        res.zones.push_back (oz);

        if (z.beating)
            continue;

        Pool p;
        double kk2 = 0;
        for (int i = z.s; i < z.e; ++i)
        {
            const double k = kLP[(size_t) i], s = sLP[(size_t) i];
            p.ks += k * s; p.kk += k * k; p.ss += s * s;
            kk2 += (k + s) * (k + s);
        }
        p.durMs = oz.durationMs;
        p.count = 1;

        if (z.type == 'A')
        {
            stats.a.add (p);
            const double louder = std::max (p.kk, p.ss);
            if (louder > 1e-14)
            {
                stats.handoverSumDb += 10.0 * std::log10 (std::max (kk2, 1e-20) / louder);
                ++stats.handoverCount;
            }
        }
        else
        {
            stats.b.add (p);
        }
    }

    // ---- best phase rotation on the matched zones (decimated for speed) -----
    {
        bool anyA = false, anyMatched = false;
        for (auto& z : zones)
        {
            if (z.beating) continue;
            anyMatched = true;
            if (z.type == 'A') anyA = true;
        }

        if (anyMatched)
        {
            const int D = std::max (1, (int) std::floor (fs / 6000.0));
            const double fsd = fs / D;
            const int nd = n / D;
            std::vector<float> rd ((size_t) nd), sd ((size_t) nd), rot ((size_t) nd);
            for (int i = 0; i < nd; ++i) { rd[(size_t) i] = rLP[(size_t) (i * D)]; sd[(size_t) i] = sLP[(size_t) (i * D)]; }
            const double pol = in.state.invert ? -1.0 : 1.0;

            for (int step = 0; step < sweepSteps; ++step)
            {
                rotateBuffer (rd.data(), rot.data(), nd, fsd, step * 5.0);
                Pool p;
                for (auto& z : zones)
                {
                    if (z.beating || (anyA && z.type != 'A')) continue;
                    for (int i = z.s / D; i < std::min (nd, z.e / D); ++i)
                    {
                        const double k = pol * rot[(size_t) i], s = sd[(size_t) i];
                        p.ks += k * s; p.kk += k * k; p.ss += s * s;
                    }
                    p.count = 1;
                }
                stats.sweep[(size_t) step] = p;
            }
        }
    }

    history.push_back (stats);
    while ((int) history.size() > barsToAverage)
        history.pop_front();

    latest = std::move (res);
}

void OverlapAnalyzer::fillResult (OverlapResult& r) const
{
    r = latest;
    r.barCount = barCount;
    if (! latest.hasBar)
        return;

    Pool a, b;
    double hoSum = 0; int hoCount = 0;
    std::array<Pool, sweepSteps> sweep {};
    for (auto& h : history)
    {
        a.add (h.a);
        b.add (h.b);
        hoSum += h.handoverSumDb; hoCount += h.handoverCount;
        for (int i = 0; i < sweepSteps; ++i) sweep[(size_t) i].add (h.sweep[(size_t) i]);
    }

    r.aValid = a.valid();
    r.aCorrelation = a.corr();
    r.aCount = a.count;
    r.aDurationMs = a.count > 0 ? a.durMs / a.count : 0.0;

    r.bValid = b.valid();
    r.bCorrelation = b.corr();
    r.bCount = b.count;
    r.bDurationMs = b.count > 0 ? b.durMs / b.count : 0.0;

    r.handoverValid = hoCount > 0;
    r.handoverDb = hoCount > 0 ? hoSum / hoCount : 0.0;

    r.bestRotateValid = false;
    double best = -2;
    for (int i = 0; i < sweepSteps; ++i)
    {
        if (! sweep[(size_t) i].valid()) continue;
        const double c = sweep[(size_t) i].corr();
        if (c > best) { best = c; r.bestRotateDeg = i * 5; r.bestRotateValid = true; }
    }
    r.bestRotateCorrelation = r.bestRotateValid ? best : 0.0;
}

} // namespace pc
