#pragma once

// Synthetic test material shared by the unit tests and the screenshot tool.

#include <algorithm>
#include <cmath>
#include <vector>

namespace pc::synth
{
    constexpr double synthPi = 3.14159265358979323846;


    /** Decaying 55 Hz sine with a pitch drop from 145 Hz, starting at t = 0. */
    inline float kickSample (double t, double decay = 0.110)
    {
        if (t < 0) return 0.0f;
        const double tauPitch = 0.012;
        // phase = 2 pi * integral of f(t) = 55 t + 90 tauPitch (1 - e^{-t/tauPitch})
        const double phase = 2.0 * synthPi * (55.0 * t + 90.0 * tauPitch * (1.0 - std::exp (-t / tauPitch)));
        return (float) (std::exp (-t / decay) * std::sin (phase));
    }

    inline std::vector<float> kickTrain (double fs, double seconds, double period, double firstAt = 0.05)
    {
        std::vector<float> x ((size_t) (seconds * fs), 0.0f);
        for (double onset = firstAt; onset < seconds; onset += period)
            for (size_t i = (size_t) (onset * fs); i < x.size(); ++i)
            {
                const double t = i / fs - onset;
                if (t > period - 0.005) break;
                x[i] += 0.8f * kickSample (t);
            }
        return x;
    }

    /** Fractional delay by windowed-sinc interpolation (independent of the plug-in code). */
    inline std::vector<float> delayed (const std::vector<float>& x, double delaySamples)
    {
        std::vector<float> y (x.size(), 0.0f);
        const int half = 32;
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double pos = (double) i - delaySamples;
            const int c = (int) std::floor (pos);
            double acc = 0;
            for (int k = c - half + 1; k <= c + half; ++k)
            {
                if (k < 0 || k >= (int) x.size()) continue;
                const double d = pos - k;
                const double sinc = std::abs (d) < 1e-12 ? 1.0 : std::sin (synthPi * d) / (synthPi * d);
                const double w = 0.5 + 0.5 * std::cos (synthPi * d / half);
                acc += x[(size_t) k] * sinc * w;
            }
            y[i] = (float) acc;
        }
        return y;
    }

    /** Overlap-mode material: 138 BPM, kicks on the beats, bass on the offbeats (A1 A1 A1 G1). */
    struct OverlapScene
    {
        double fs = 44100, bpm = 138;
        double beat() const { return 60.0 / bpm; }
        double kickDecay = 0.110;
        double bassLen = 0.200, bassRelease = 0.030, duckDepth = 0.5, duckTime = 0.050;
        std::vector<double> noteHz { 55.0, 55.0, 55.0, 48.999 };
        double bassGain = 0.8;
        double start = 0.25;   // first downbeat (s)
        int bars = 6;

        std::vector<float> kick, bass;
        std::vector<double> kickOnsets;

        void render (bool withBass = true, bool invertBass = false)
        {
            const size_t n = (size_t) ((start + bars * 4 * beat() + 0.5) * fs);
            kick.assign (n, 0.0f);
            bass.assign (n, 0.0f);
            kickOnsets.clear();
            for (int b = 0; b < bars * 4 + 1; ++b)
                kickOnsets.push_back (start + b * beat());

            for (double k : kickOnsets)
                for (size_t i = (size_t) (k * fs); i < n; ++i)
                {
                    const double t = i / fs - k;
                    if (t > beat()) break;
                    const double fade = t > beat() - 0.003 ? (beat() - t) / 0.003 : 1.0;
                    kick[i] += (float) (0.9 * kickSample (t, kickDecay) * fade);
                }

            if (! withBass) return;
            for (int b = -1; b < bars * 4 + 1; ++b)
            {
                const double on = start + b * beat() + beat() / 2;
                const double f = noteHz[(size_t) (((b % 4) + 4) % 4)];
                for (size_t i = (size_t) std::max (0.0, on * fs); i < n; ++i)
                {
                    const double t = i / fs - on;
                    if (t < 0) continue;
                    if (t > bassLen + 0.25) break;
                    bass[i] += (float) ((invertBass ? -1.0 : 1.0) * bassAmp (on, t) * std::sin (2 * synthPi * f * t));
                }
            }
        }

        double duck (double absT) const
        {
            double last = -1;
            for (double k : kickOnsets) if (k <= absT) last = k;
            return last < 0 ? 1.0 : 1.0 - duckDepth * std::exp (-(absT - last) / duckTime);
        }

        double bassAmp (double on, double t) const
        {
            const double a = t < 0.005 ? t / 0.005 : (t < bassLen ? 1.0 : std::exp (-(t - bassLen) / bassRelease));
            return bassGain * a * duck (on + t);
        }

        double kickAmp (double t) const { return t < 0 ? 0 : 0.9 * std::exp (-t / kickDecay); }
    };

} // namespace pc::synth
