// Phase Check unit tests (run with ctest, or directly).

#include "analysis/AnalysisCore.h"
#include "analysis/AnalysisUtils.h"
#include "dsp/KickProcessor.h"
#include "Synth.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace pc;
using namespace pc::synth;

namespace
{
    constexpr double pi = 3.14159265358979323846;
    int failures = 0, checks = 0;

#define CHECK(cond, ...)                                                        \
    do {                                                                        \
        ++checks;                                                               \
        if (! (cond)) {                                                         \
            ++failures;                                                         \
            std::printf ("  FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond);      \
            std::printf (__VA_ARGS__);                                          \
            std::printf ("\n");                                                 \
        }                                                                       \
    } while (0)

    void section (const char* name) { std::printf ("[%s]\n", name); }

    bool finite (double v) { return std::isfinite (v); }
    bool same_ (float a, float b) { return exactlyEqual (a, b); }

    void feed (AnalysisCore& core, const std::vector<float>& proc, const std::vector<float>& raw,
               const std::vector<float>& sc, const KickState& st, int block = 512,
               std::function<TransportInfo (size_t)> transport = {})
    {
        for (size_t pos = 0; pos < proc.size(); pos += (size_t) block)
        {
            const int n = (int) std::min ((size_t) block, proc.size() - pos);
            TransportInfo t = transport ? transport (pos) : TransportInfo {};
            core.push (proc.data() + pos, raw.data() + pos, sc.data() + pos, n, t, st);
            if ((pos / (size_t) block) % 8 == 0) core.update();
        }
        core.update();
    }

    bool snapshotIsFinite (const Snapshot& s)
    {
        const auto& h = s.hit;
        bool ok = finite (h.correlation) && finite (h.offsetMs) && finite (h.offsetSamples) && finite (h.confidence)
               && finite (h.sumDb) && finite (h.sumHz) && finite (h.predictedFlip) && finite (h.predictedDelay)
               && finite (h.predictedBoth) && finite (h.predictedBestRotate) && finite (h.correlationStdDev)
               && finite (h.sidechainOnsetMs);
        for (auto v : h.bandCorrelation) ok = ok && finite (v);
        for (auto v : h.waveKick) ok = ok && finite (v);
        for (auto v : h.waveSidechain) ok = ok && finite (v);
        const auto& o = s.overlap;
        ok = ok && finite (o.aCorrelation) && finite (o.bCorrelation) && finite (o.aDurationMs) && finite (o.bDurationMs)
                && finite (o.handoverDb) && finite (o.kickTailHz) && finite (o.bpm) && finite (o.bestRotateCorrelation);
        for (auto& z : o.zones) ok = ok && finite (z.correlation) && finite (z.durationMs);
        for (auto& n : o.notes) ok = ok && finite (n.hz) && finite (n.beatHz);
        for (auto v : o.kickEnvDb) ok = ok && finite (v);
        for (auto v : o.bassEnvDb) ok = ok && finite (v);
        return ok;
    }

    double sinePhaseDeg (const std::vector<float>& x, double fs, double f, size_t from, size_t to)
    {
        double re = 0, im = 0;
        for (size_t i = from; i < to; ++i)
        {
            re += x[i] * std::cos (2 * pi * f * i / fs);
            im += x[i] * std::sin (2 * pi * f * i / fs);
        }
        return std::atan2 (re, im) * 180.0 / pi;   // phase of sin(wt + phi)
    }

    double wrapDeg (double d)
    {
        while (d > 180) d -= 360;
        while (d < -180) d += 360;
        return d;
    }

    //==========================================================================
    void testHitOffsetAndPolarity (double fs)
    {
        char name[96];
        std::snprintf (name, sizeof (name), "Hit mode: kick vs. inverted copy delayed 1.8 ms @ %.1f kHz", fs / 1000.0);
        section (name);

        const auto kick = kickTrain (fs, 6.0, 0.5);
        auto sc = delayed (kick, 1.8e-3 * fs);
        for (auto& v : sc) v = -v;

        KickState st;
        st.scChannels = 1;

        AnalysisCore core;
        core.prepare (fs);
        feed (core, kick, kick, sc, st);
        auto s = core.getSnapshot();

        CHECK (s->sidechainLive, "sidechain should be live");
        CHECK (s->hit.hasData && ! s->hit.stale, "hit data expected (hits=%d)", core.getHitCount());
        CHECK (s->hit.offsetClear, "offset should be clear (conf %.3f)", s->hit.confidence);
        CHECK (std::abs (s->hit.offsetMs - 1.8) <= 0.02, "offset %.4f ms, expected 1.80 +/- 0.02", s->hit.offsetMs);
        CHECK (s->hit.correlation < -0.3, "correlation %.3f should be clearly negative", s->hit.correlation);
        CHECK (s->hit.predictedDelayValid, "delay prediction should be valid");
        CHECK (s->hit.predictedBoth > 0.9, "flip + delay prediction %.3f should be > 0.9", s->hit.predictedBoth);
        CHECK (s->hit.predictedFlip > 0.3, "flip prediction %.3f", s->hit.predictedFlip);
        CHECK (s->hit.suggestion.find ("Flip polarity first") != std::string::npos, "suggestion: %s", s->hit.suggestion.c_str());
        CHECK (std::abs (s->hit.predictedDelayMs - 1.8) < 0.03, "delay button value %.3f", s->hit.predictedDelayMs);
        CHECK (s->hit.sumValid && s->hit.sumHz > 40 && s->hit.sumHz < 120, "sum freq %.1f", s->hit.sumHz);
        CHECK (snapshotIsFinite (*s), "no NaNs");

        // Apply the fix through the real kick chain: invert + delay 1.8 ms
        KickProcessor kp;
        kp.prepare (fs, 1);
        kp.setTargets (true, 1.8, 0.0);
        kp.reset(); // jump straight to the targets
        std::vector<float> processed = kick;
        float* chans[] = { processed.data() };
        kp.process (chans, 1, (int) processed.size());

        KickState fixed = st;
        fixed.invert = true;
        fixed.delayOn = true;
        fixed.delayMs = 1.8;

        AnalysisCore core2;
        core2.prepare (fs);
        feed (core2, processed, kick, sc, fixed);
        auto s2 = core2.getSnapshot();
        CHECK (s2->hit.correlation > 0.95, "fixed correlation %.3f", s2->hit.correlation);
        CHECK (s2->hit.offsetClear && std::abs (s2->hit.offsetMs) < 0.03, "fixed offset %.4f", s2->hit.offsetMs);
        CHECK (s2->hit.suggestion.find ("aligned and in phase") != std::string::npos, "suggestion: %s", s2->hit.suggestion.c_str());
    }

    void testIdenticalAndInverted()
    {
        section ("Hit mode: identical -> +1, inverted -> -1");
        const double fs = 48000;
        const auto kick = kickTrain (fs, 4.0, 0.5);
        KickState st; st.scChannels = 2;

        AnalysisCore core; core.prepare (fs);
        feed (core, kick, kick, kick, st);
        auto s = core.getSnapshot();
        CHECK (s->hit.correlation > 0.99, "identical corr %.4f", s->hit.correlation);
        CHECK (s->hit.offsetClear && std::abs (s->hit.offsetMs) < 0.005, "identical offset %.4f", s->hit.offsetMs);
        for (int b = 0; b < kNumBands; ++b)
            if (s->hit.bandActive[(size_t) b])
                CHECK (s->hit.bandCorrelation[(size_t) b] > 0.99, "band %d corr %.3f", b, s->hit.bandCorrelation[(size_t) b]);
        CHECK (s->hit.lowBandLowConfidence, "30 Hz band must be flagged low-confidence with a 50 ms window");
        CHECK (s->sidechain == SidechainStatus::stereo, "status stereo");

        auto inv = kick;
        for (auto& v : inv) v = -v;
        AnalysisCore core2; core2.prepare (fs);
        feed (core2, kick, kick, inv, st);
        auto s2 = core2.getSnapshot();
        CHECK (s2->hit.correlation < -0.99, "inverted corr %.4f", s2->hit.correlation);
        CHECK (s2->hit.offsetClear && std::abs (s2->hit.offsetMs) < 0.005, "inverted offset %.4f", s2->hit.offsetMs);
        CHECK (s2->hit.predictedFlip > 0.99, "flip prediction %.3f", s2->hit.predictedFlip);
    }

    //==========================================================================
    void testRotator()
    {
        for (double fs : { 44100.0, 48000.0, 96000.0 })
        {
            char name[96];
            std::snprintf (name, sizeof (name), "Rotator @ %.1f kHz", fs / 1000.0);
            section (name);

            // 90 deg phase shift at 60 Hz, relative to the rotator's own 0 deg branch
            // (both branches share the common all-pass phase, see README).
            {
                const size_t n = (size_t) (fs * 2.0);
                std::vector<float> x (n);
                for (size_t i = 0; i < n; ++i) x[i] = (float) std::sin (2 * pi * 60.0 * i / fs);

                std::vector<float> rot90 (n), rot0 (n);
                rotateBuffer (x.data(), rot90.data(), (int) n, fs, 90.0);
                // unbypassed 0 deg = path I
                HilbertPair hp; hp.setCoefficients (HilbertDesigner::design (fs));
                for (size_t i = 0; i < n; ++i) { double I, Q; hp.process (x[i], I, Q); rot0[i] = (float) I; }

                const size_t from = n / 2, to = n;
                const double d = wrapDeg (sinePhaseDeg (rot90, fs, 60, from, to) - sinePhaseDeg (rot0, fs, 60, from, to));
                CHECK (std::abs (d - 90.0) <= 2.0, "phase shift %.3f deg", d);

                // The real-time processor gives the same result
                KickProcessor kp; kp.prepare (fs, 1);
                kp.setTargets (false, 0.0, 90.0); kp.reset();
                std::vector<float> y = x; float* ch[] = { y.data() };
                kp.process (ch, 1, (int) n);
                const double d2 = wrapDeg (sinePhaseDeg (y, fs, 60, from, to) - sinePhaseDeg (rot0, fs, 60, from, to));
                CHECK (std::abs (d2 - 90.0) <= 2.0, "processor phase shift %.3f deg", d2);
            }

            // magnitude flat within +/-0.1 dB from 30 Hz to 10 kHz (90 and 45 deg)
            double worst = 0;
            for (double f = 30; f <= 10000; f *= 1.25)
                for (double th : { 45.0, 90.0, 135.0 })
                {
                    const size_t n = (size_t) (fs * 1.5);
                    std::vector<float> x (n), y (n);
                    for (size_t i = 0; i < n; ++i) x[i] = (float) std::sin (2 * pi * f * i / fs);
                    rotateBuffer (x.data(), y.data(), (int) n, fs, th);
                    const size_t from = n / 2;
                    const double gx = rms (x.data() + from, (int) (n - from));
                    const double gy = rms (y.data() + from, (int) (n - from));
                    worst = std::max (worst, std::abs (dbFromRatio (gy / gx)));
                }
            CHECK (worst <= 0.1, "worst magnitude deviation %.4f dB", worst);

            // 0 deg: bit-transparent
            {
                const size_t n = 20000;
                std::vector<float> x (n);
                uint32_t seed = 1234;
                for (auto& v : x) { seed = seed * 1664525u + 1013904223u; v = (float) ((seed >> 8) / 8388608.0 - 1.0); }
                KickProcessor kp; kp.prepare (fs, 2);
                kp.setTargets (false, 0.0, 0.0);
                std::vector<float> l = x, r = x;
                float* ch[] = { l.data(), r.data() };
                for (size_t pos = 0; pos < n; pos += 333)
                {
                    float* c2[] = { l.data() + pos, r.data() + pos };
                    kp.process (c2, 2, (int) std::min<size_t> (333, n - pos));
                }
                (void) ch;
                bool same = true;
                for (size_t i = 0; i < n; ++i) same = same && same_ (l[i], x[i]) && same_ (r[i], x[i]);
                CHECK (same, "0 deg must be bit-identical to the input");

                // leave 0 deg and come back: once the crossfade is done it is bit-identical again
                KickProcessor kp2; kp2.prepare (fs, 1);
                std::vector<float> y = x;
                kp2.setTargets (false, 0.0, 40.0);
                float* c3[] = { y.data() };
                kp2.process (c3, 1, 4000);
                kp2.setTargets (false, 0.0, 0.0);
                float* c4[] = { y.data() + 4000 };
                kp2.process (c4, 1, (int) n - 4000);
                const size_t settle = 4000 + (size_t) (0.05 * fs);
                bool same2 = true;
                for (size_t i = settle; i < n; ++i) same2 = same2 && same_ (y[i], x[i]);
                CHECK (same2, "back at 0 deg must be bit-identical again");
            }
        }
    }

    void testPolarityAndDelay()
    {
        section ("Polarity and delay");
        const double fs = 48000;
        const size_t n = 9600;
        std::vector<float> x (n);
        for (size_t i = 0; i < n; ++i) x[i] = (float) std::sin (2 * pi * 100.0 * i / fs) * 0.5f;

        // inverted, settled -> exactly -x
        KickProcessor kp; kp.prepare (fs, 1);
        kp.setTargets (true, 0.0, 0.0); kp.reset();
        auto y = x; float* ch[] = { y.data() };
        kp.process (ch, 1, (int) n);
        bool exact = true;
        for (size_t i = 0; i < n; ++i) exact = exact && same_ (y[i], -x[i]);
        CHECK (exact, "settled inversion must be exact");

        // toggle is a 10 ms ramp (no jump)
        KickProcessor kp2; kp2.prepare (fs, 1);
        std::vector<float> dc (n, 1.0f);
        kp2.setTargets (false, 0, 0); kp2.reset();
        float* c2[] = { dc.data() };
        kp2.process (c2, 1, 100);
        kp2.setTargets (true, 0, 0);
        float* c3[] = { dc.data() + 100 };
        kp2.process (c3, 1, (int) n - 100);
        double maxStep = 0;
        for (size_t i = 1; i < n; ++i) maxStep = std::max (maxStep, (double) std::abs (dc[i] - dc[i - 1]));
        CHECK (maxStep < 0.01, "polarity crossfade max step %.4f", maxStep);
        CHECK (std::abs (dc[(size_t) (100 + 0.011 * fs)] + 1.0f) < 1e-6, "ramp reaches -1 within 10 ms");

        // fractional delay: 1.8 ms = 86.4 samples at 48 kHz
        KickProcessor kp3; kp3.prepare (fs, 1);
        kp3.setTargets (false, 1.8, 0); kp3.reset();
        auto z = x; float* c4[] = { z.data() };
        kp3.process (c4, 1, (int) n);
        double err = 0;
        for (size_t i = 200; i < n; ++i)
        {
            const double ref = std::sin (2 * pi * 100.0 * (i - 86.4) / fs) * 0.5;
            err = std::max (err, std::abs (z[i] - ref));
        }
        CHECK (err < 1e-4, "fractional delay error %.6f", err);
    }

    void testSilentSidechain()
    {
        section ("Silent / missing sidechain");
        const double fs = 44100;
        const auto kick = kickTrain (fs, 4.0, 0.5);
        std::vector<float> silent (kick.size(), 0.0f);

        for (int channels : { 1, 0 })
        {
            KickState st; st.scChannels = channels;
            AnalysisCore core; core.prepare (fs);
            feed (core, kick, kick, silent, st);
            auto s = core.getSnapshot();
            CHECK (! s->sidechainLive, "sidechain must not be live (channels %d)", channels);
            CHECK (s->hit.stale && s->overlap.stale, "readouts must be greyed out");
            CHECK (s->hit.suggestion.find ("No sidechain signal") != std::string::npos, "suggestion: %s", s->hit.suggestion.c_str());
            CHECK (snapshotIsFinite (*s), "no NaNs");
            if (channels == 0) CHECK (s->sidechain == SidechainStatus::notConnected, "status");
        }

        // live, then silent for > 1 s: old numbers must not be shown as live
        KickState st; st.scChannels = 1;
        AnalysisCore core; core.prepare (fs);
        auto sc = kick;
        for (size_t i = (size_t) (2.0 * fs); i < sc.size(); ++i) sc[i] = 0;
        feed (core, kick, kick, sc, st);
        auto s = core.getSnapshot();
        CHECK (! s->sidechainLive && s->hit.stale, "went silent -> stale");
        CHECK (! s->hit.hasData, "averages cleared when the sidechain goes silent");
    }

    void testOverlapMode()
    {
        section ("Overlap mode: synthetic bar at 138 BPM");

        OverlapScene sc;
        sc.render();
        const double fs = sc.fs;

        // --- run one bar directly through the analyser (exact kick onsets) ---
        auto runBar = [&] (const std::vector<float>& bass, OverlapResult& out, int barIndex = 2)
        {
            OverlapAnalyzer an;
            const double barStartT = sc.start + barIndex * 4 * sc.beat();
            const int pre = OverlapAnalyzer::preRollSamples (fs, sc.beat());
            const int bs = (int) std::lround (barStartT * fs);
            const int bl = (int) std::lround (4 * sc.beat() * fs);
            const int len = pre + bl + (int) (0.01 * fs);
            BarInput in;
            in.processed = sc.kick.data() + (bs - pre);
            in.raw = in.processed;
            in.sidechain = bass.data() + (bs - pre);
            in.length = len;
            in.barStart = pre;
            in.barLength = bl;
            in.sampleRate = fs;
            in.state.scChannels = 1;
            in.state.mode = Mode::overlap;
            in.bpm = sc.bpm;
            in.hostTempo = true;
            for (double k : sc.kickOnsets)
            {
                const int idx = (int) std::lround (k * fs) - (bs - pre);
                if (idx >= 0 && idx < len) in.kickOnsets.push_back (idx);
            }
            an.analyseBar (in);
            an.fillResult (out);
        };

        OverlapResult r;
        runBar (sc.bass, r);

        int nA = 0, nB = 0;
        for (auto& z : r.zones) (z.type == 'A' ? nA : nB)++;
        CHECK (nA == 4, "type A zones: %d (expected 4)", nA);
        CHECK (nB == 3 || nB == 4, "type B zones: %d (expected 3 or 4)", nB);

        // ground truth durations from the analytic envelopes (relative to each one's peak)
        {
            const double thr = std::pow (10.0, -22.0 / 20.0);
            const double kPeak = sc.kickAmp (0);
            double bPeak = 0;
            const double barT = sc.start + 2 * 4 * sc.beat();
            for (double t = barT; t < barT + 4 * sc.beat(); t += 1e-4)
                for (int b = -1; b < 4; ++b)
                {
                    const double on = barT + b * sc.beat() + sc.beat() / 2;
                    if (t >= on) bPeak = std::max (bPeak, sc.bassAmp (on, t - on));
                }

            auto kickEnv = [&] (double t)
            {
                double best = 0;
                for (double k : sc.kickOnsets) if (t >= k && t - k < sc.beat()) best = std::max (best, sc.kickAmp (t - k));
                return best / kPeak;
            };
            auto bassEnv = [&] (double t)
            {
                double v = 0;
                for (int b = -1; b < 8; ++b)
                {
                    const double on = barT + b * sc.beat() + sc.beat() / 2;
                    if (t >= on && t - on < sc.bassLen + 0.25) v = std::max (v, sc.bassAmp (on, t - on));
                }
                return v / bPeak;
            };

            std::vector<std::pair<double, double>> truth;
            double zs = -1;
            for (double t = barT; t <= barT + 4 * sc.beat(); t += 1e-4)
            {
                const bool both = kickEnv (t) >= thr && bassEnv (t) >= thr && t < barT + 4 * sc.beat() - 1e-4;
                if (both && zs < 0) zs = t;
                if (! both && zs >= 0) { if (t - zs >= 0.004) truth.push_back ({ zs, t }); zs = -1; }
            }

            CHECK (truth.size() == r.zones.size(), "zone count %zu vs truth %zu", r.zones.size(), truth.size());
            for (size_t i = 0; i < std::min (truth.size(), r.zones.size()); ++i)
            {
                const double tDur = (truth[i].second - truth[i].first) * 1000.0;
                CHECK (std::abs (r.zones[i].durationMs - tDur) <= 5.0, "zone %zu (%c) duration %.1f ms vs truth %.1f ms [%.4f-%.4f vs %.4f-%.4f]",
                       i, r.zones[i].type, r.zones[i].durationMs, tDur,
                       r.zones[i].startFrac * 4 * sc.beat(), r.zones[i].endFrac * 4 * sc.beat(),
                       truth[i].first - barT, truth[i].second - barT);
            }
        }

        CHECK (r.kickPitchValid && std::abs (r.kickTailHz - 55.0) <= 1.0, "kick tail %.2f Hz", r.kickTailHz);
        CHECK (r.kickTailNote == "A1", "kick tail note %s", r.kickTailNote.c_str());

        CHECK (r.notes.size() == 4, "notes in bar: %zu", r.notes.size());
        if (r.notes.size() == 4)
        {
            for (int i = 0; i < 3; ++i)
                CHECK (r.notes[(size_t) i].matched && r.notes[(size_t) i].name == "A1", "note %d: %s %.2f Hz matched=%d",
                       i, r.notes[(size_t) i].name.c_str(), r.notes[(size_t) i].hz, (int) r.notes[(size_t) i].matched);
            const auto& g = r.notes[3];
            CHECK (! g.matched && g.name == "G1", "G1 note: %s %.2f Hz matched=%d", g.name.c_str(), g.hz, (int) g.matched);
            CHECK (std::abs (g.beatHz - 6.0) <= 1.0, "beat rate %.2f Hz", g.beatHz);
            CHECK (g.position == "4&", "G1 position %s", g.position.c_str());
            CHECK (r.notes[0].position == "1&", "first note position %s", r.notes[0].position.c_str());
        }

        // beating zones are excluded
        int beatingA = 0, matchedA = 0;
        for (auto& z : r.zones)
            if (z.type == 'A') (z.beating ? beatingA : matchedA)++;
        CHECK (beatingA == 1 && matchedA == 3, "A zones beating %d matched %d", beatingA, matchedA);
        CHECK (r.aCount == 3, "A correlation pooled over %d zones (expected 3 matched)", r.aCount);
        CHECK (r.aValid, "type A correlation valid");
        CHECK (r.suggestion.empty(), "(suggestion is built by the core)");

        // pooled A correlation equals the pooled value over matched zones only
        {
            const auto s = buildOverlapSuggestion (r, KickState {}, true);
            CHECK (s.find ("G1 note beats against the 55 Hz kick tail") != std::string::npos, "suggestion: %s", s.c_str());
            CHECK (s.find ("On 4&") != std::string::npos, "suggestion: %s", s.c_str());
        }

        // inverting the bass flips the sign of the type A correlation
        {
            OverlapScene inv = sc;
            inv.render (true, true);
            OverlapResult ri;
            runBar (inv.bass, ri);
            CHECK (ri.aValid && std::abs (ri.aCorrelation + r.aCorrelation) < 0.05 && std::abs (r.aCorrelation) > 0.1,
                   "A corr %.3f vs inverted %.3f", r.aCorrelation, ri.aCorrelation);
        }

        // no bass at all: "no overlap", not NaN
        {
            OverlapScene nb = sc;
            nb.render (false);
            OverlapResult rn;
            runBar (nb.bass, rn);
            CHECK (rn.noOverlap && rn.noBass && ! rn.aValid && ! rn.bValid, "no-bass flags");
            CHECK (std::isfinite (rn.aCorrelation) && std::isfinite (rn.bCorrelation) && std::isfinite (rn.handoverDb), "finite");
            const auto s = buildOverlapSuggestion (rn, KickState {}, true);
            CHECK (s.find ("don't overlap") != std::string::npos, "suggestion: %s", s.c_str());
        }

        // full pipeline with host transport
        {
            AnalysisCore core; core.prepare (fs);
            KickState st; st.scChannels = 1; st.mode = Mode::overlap;
            const double spq = fs * 60.0 / sc.bpm;
            const double startSamples = sc.start * fs;
            feed (core, sc.kick, sc.kick, sc.bass, st, 512, [&] (size_t pos)
            {
                TransportInfo t;
                t.playing = true; t.hasTempo = true; t.bpm = sc.bpm; t.hasPpq = true;
                t.ppq = ((double) pos - startSamples) / spq;
                return t;
            });
            auto s = core.getSnapshot();
            CHECK (s->overlap.hasBar && core.getBarCount() >= 4, "bars analysed: %d", core.getBarCount());
            CHECK (s->overlap.hostTempo && std::abs (s->overlap.bpm - 138) < 0.01, "host tempo %.2f", s->overlap.bpm);
            int a = 0; for (auto& z : s->overlap.zones) if (z.type == 'A') ++a;
            CHECK (a == 4, "pipeline A zones %d", a);
            CHECK (s->overlap.notes.size() == 4, "pipeline notes %zu", s->overlap.notes.size());
            CHECK (s->overlap.suggestion.find ("G1") != std::string::npos, "suggestion: %s", s->overlap.suggestion.c_str());
            CHECK (snapshotIsFinite (*s), "no NaNs");
        }

        // no host tempo: estimate from kick hits
        {
            AnalysisCore core; core.prepare (fs);
            KickState st; st.scChannels = 1; st.mode = Mode::overlap;
            feed (core, sc.kick, sc.kick, sc.bass, st);
            auto s = core.getSnapshot();
            CHECK (std::abs (core.getTempoEstimateBpm() - 138.0) <= 1.0, "estimated tempo %.3f BPM", core.getTempoEstimateBpm());
            CHECK (! s->overlap.hostTempo && s->overlap.tempoKnown, "fallback tempo flags");
            CHECK (s->overlap.hasBar, "fallback bars analysed (%d)", core.getBarCount());
            int a = 0; for (auto& z : s->overlap.zones) if (z.type == 'A') ++a;
            CHECK (a == 4, "fallback A zones %d", a);
        }
    }
}

int main()
{
    testHitOffsetAndPolarity (44100.0);
    testHitOffsetAndPolarity (48000.0);
    testIdenticalAndInverted();
    testRotator();
    testPolarityAndDelay();
    testSilentSidechain();
    testOverlapMode();

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
