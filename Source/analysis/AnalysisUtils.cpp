#include "AnalysisUtils.h"

#include <algorithm>
#include <cstdio>

namespace pc
{

namespace
{
    constexpr double pi = 3.14159265358979323846;
}

Biquad Biquad::lowPass (double fs, double f, double q)
{
    const double w0 = 2.0 * pi * std::min (f, 0.49 * fs) / fs;
    const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    Biquad b;
    b.b0 = (1.0 - cw) / 2.0 / a0;
    b.b1 = (1.0 - cw) / a0;
    b.b2 = (1.0 - cw) / 2.0 / a0;
    b.a1 = -2.0 * cw / a0;
    b.a2 = (1.0 - alpha) / a0;
    return b;
}

Biquad Biquad::bandPass (double fs, double f, double q)
{
    const double w0 = 2.0 * pi * std::min (f, 0.49 * fs) / fs;
    const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    Biquad b;
    b.b0 = alpha / a0;
    b.b1 = 0.0;
    b.b2 = -alpha / a0;
    b.a1 = -2.0 * cw / a0;
    b.a2 = (1.0 - alpha) / a0;
    return b;
}

void LowPass4::prepare (double fs, double cutoff)
{
    // Butterworth 4th order = Q 0.5412 and 1.3066
    s1 = Biquad::lowPass (fs, cutoff, 0.54119610);
    s2 = Biquad::lowPass (fs, cutoff, 1.30656296);
}

void lowPass (const float* in, float* out, int n, double fs, double cutoff)
{
    LowPass4 f;
    f.prepare (fs, cutoff);
    for (int i = 0; i < n; ++i)
        out[i] = (float) f.process ((double) in[i]);
}

void bandPass (const float* in, float* out, int n, double fs, double centre, double q)
{
    auto f = Biquad::bandPass (fs, centre, q);
    for (int i = 0; i < n; ++i)
        out[i] = (float) f.process ((double) in[i]);
}

double octaveBandwidthToQ (double octaves)
{
    const double p = std::pow (2.0, octaves);
    return std::sqrt (p) / (p - 1.0);
}

//==============================================================================
double sumSquares (const float* x, int n)
{
    double s = 0;
    for (int i = 0; i < n; ++i) s += (double) x[i] * x[i];
    return s;
}

double rms (const float* x, int n)
{
    return n > 0 ? std::sqrt (sumSquares (x, n) / n) : 0.0;
}

double pearson (const float* a, const float* b, int n, bool* valid)
{
    if (valid) *valid = false;
    if (n < 2) return 0.0;

    double ma = 0, mb = 0;
    for (int i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
    ma /= n; mb /= n;

    double sab = 0, saa = 0, sbb = 0;
    for (int i = 0; i < n; ++i)
    {
        const double da = a[i] - ma, db = b[i] - mb;
        sab += da * db; saa += da * da; sbb += db * db;
    }

    if (saa < 1.0e-14 || sbb < 1.0e-14)
        return 0.0;

    if (valid) *valid = true;
    return std::clamp (sab / std::sqrt (saa * sbb), -1.0, 1.0);
}

double normalisedDot (const float* a, const float* b, int n, bool* valid)
{
    if (valid) *valid = false;
    double sab = 0, saa = 0, sbb = 0;
    for (int i = 0; i < n; ++i)
    {
        sab += (double) a[i] * b[i];
        saa += (double) a[i] * a[i];
        sbb += (double) b[i] * b[i];
    }
    if (saa < 1.0e-14 || sbb < 1.0e-14) return 0.0;
    if (valid) *valid = true;
    return std::clamp (sab / std::sqrt (saa * sbb), -1.0, 1.0);
}

XCorrResult crossCorrelate (const float* a, const float* b, int n, int maxLag)
{
    XCorrResult res;
    const double saa = sumSquares (a, n);
    if (saa < 1.0e-14 || n < 4 || maxLag < 1)
        return res;

    std::vector<double> r ((size_t) (2 * maxLag + 1), 0.0);

    // running energy of the b window
    double sbb = 0;
    for (int i = 0; i < n; ++i) sbb += (double) b[i - maxLag] * b[i - maxLag];

    for (int L = -maxLag; L <= maxLag; ++L)
    {
        if (L > -maxLag)
        {
            const double out = b[L - 1];
            const double in  = b[L - 1 + n];
            sbb += in * in - out * out;
            if (sbb < 0) sbb = 0;
        }

        double sab = 0;
        const float* bb = b + L;
        for (int i = 0; i < n; ++i) sab += (double) a[i] * bb[i];

        r[(size_t) (L + maxLag)] = sbb > 1.0e-14 ? sab / std::sqrt (saa * sbb) : 0.0;
    }

    int best = 0;
    double bestAbs = -1;
    for (int i = 0; i < (int) r.size(); ++i)
    {
        if (std::abs (r[(size_t) i]) > bestAbs)
        {
            bestAbs = std::abs (r[(size_t) i]);
            best = i;
        }
    }

    if (bestAbs <= 0)
        return res;

    double offset = 0.0, peak = r[(size_t) best];
    if (best > 0 && best < (int) r.size() - 1)
    {
        const double ym = r[(size_t) best - 1], y0 = r[(size_t) best], yp = r[(size_t) best + 1];
        const double denom = ym - 2.0 * y0 + yp;
        if (std::abs (denom) > 1.0e-18)
        {
            offset = std::clamp (0.5 * (ym - yp) / denom, -0.5, 0.5);
            peak = y0 - 0.25 * (ym - yp) * offset;
        }
    }

    res.lagSamples = (double) (best - maxLag) + offset;
    res.peak = std::clamp (peak, -1.0, 1.0);
    res.valid = true;
    return res;
}

double correlationWithDelay (const float* a, const float* b, int n, double lagSamples)
{
    const int l0 = (int) std::floor (lagSamples);
    const double frac = lagSamples - l0;

    auto at = [&] (int L)
    {
        bool ok = false;
        const double v = pearson (a - L, b, n, &ok);
        return ok ? v : 0.0;
    };

    const double r0 = at (l0);
    if (frac < 1.0e-9) return r0;
    return r0 + frac * (at (l0 + 1) - r0);
}

//==============================================================================
double estimatePitchHz (const float* x, int n, double fs, double fMin, double fMax, double* clarity)
{
    if (clarity) *clarity = 0.0;
    if (n < 16 || fs <= 0) return 0.0;

    // Decimate to roughly 6 kHz (input is already band-limited well below that).
    const int D = std::max (1, (int) std::floor (fs / 6000.0));
    const double fsd = fs / D;
    std::vector<float> d;
    d.reserve ((size_t) (n / D + 1));
    for (int i = 0; i < n; i += D) d.push_back (x[i]);

    const int N = (int) d.size();
    const int tauMin = std::max (2, (int) std::floor (fsd / fMax));
    const int tauMax = (int) std::ceil (fsd / fMin);
    const int W = N - tauMax - 1;
    if (W < tauMax / 2 || W < 8)
        return 0.0;

    if (sumSquares (d.data(), N) < 1.0e-12)
        return 0.0;

    std::vector<double> diff ((size_t) tauMax + 2, 0.0);
    for (int tau = 1; tau <= tauMax + 1; ++tau)
    {
        double s = 0;
        for (int j = 0; j < W; ++j)
        {
            const double e = (double) d[(size_t) j] - d[(size_t) (j + tau)];
            s += e * e;
        }
        diff[(size_t) tau] = s;
    }

    // cumulative mean normalised difference
    std::vector<double> cmnd (diff.size(), 1.0);
    double running = 0;
    for (int tau = 1; tau <= tauMax + 1; ++tau)
    {
        running += diff[(size_t) tau];
        cmnd[(size_t) tau] = running > 0 ? diff[(size_t) tau] * tau / running : 1.0;
    }

    int best = -1;
    const double threshold = 0.25;
    for (int tau = tauMin; tau <= tauMax; ++tau)
    {
        if (cmnd[(size_t) tau] < threshold)
        {
            while (tau + 1 <= tauMax && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau]) ++tau;
            best = tau;
            break;
        }
    }

    if (best < 0)
    {
        double m = 1e9;
        for (int tau = tauMin; tau <= tauMax; ++tau)
            if (cmnd[(size_t) tau] < m) { m = cmnd[(size_t) tau]; best = tau; }
        if (m > 0.6) return 0.0;
    }

    double t = best;
    if (best > 1 && best < tauMax + 1)
    {
        const double ym = cmnd[(size_t) best - 1], y0 = cmnd[(size_t) best], yp = cmnd[(size_t) best + 1];
        const double denom = ym - 2 * y0 + yp;
        if (std::abs (denom) > 1e-18)
            t += std::clamp (0.5 * (ym - yp) / denom, -0.5, 0.5);
    }

    if (clarity) *clarity = std::clamp (1.0 - cmnd[(size_t) best], 0.0, 1.0);
    const double f = fsd / t;
    return (f >= fMin * 0.97 && f <= fMax * 1.03) ? f : 0.0;
}

double goertzelMagnitude (const float* x, int n, double fs, double f)
{
    if (n < 2) return 0.0;
    double re = 0, im = 0, wsum = 0;
    const double w = 2.0 * pi * f / fs;
    for (int i = 0; i < n; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (2.0 * pi * i / (n - 1));
        re += win * x[i] * std::cos (w * i);
        im -= win * x[i] * std::sin (w * i);
        wsum += win;
    }
    return std::sqrt (re * re + im * im) / std::max (wsum, 1.0e-9);
}

std::vector<double> quadraturePower (const float* x, int n, double fs)
{
    std::vector<double> out ((size_t) std::max (0, n), 0.0);
    if (n < 3) return out;

    std::vector<double> d ((size_t) n, 0.0);
    for (int i = 1; i < n - 1; ++i)
        d[(size_t) i] = 0.5 * ((double) x[i + 1] - (double) x[i - 1]) * fs;

    // local RMS angular frequency over ~30 ms (follows a kick's pitch drop)
    std::vector<double> pxx ((size_t) n + 1, 0.0), pdd ((size_t) n + 1, 0.0);
    for (int i = 0; i < n; ++i)
    {
        pxx[(size_t) i + 1] = pxx[(size_t) i] + (double) x[i] * x[i];
        pdd[(size_t) i + 1] = pdd[(size_t) i] + d[(size_t) i] * d[(size_t) i];
    }

    const int half = std::max (1, (int) (0.015 * fs));
    const double wMin = 2.0 * pi * 20.0, wMax = 2.0 * pi * 400.0;
    for (int i = 0; i < n; ++i)
    {
        const int a = std::max (0, i - half), b = std::min (n, i + half);
        const double sxx = pxx[(size_t) b] - pxx[(size_t) a];
        const double sdd = pdd[(size_t) b] - pdd[(size_t) a];
        const double w = sxx > 1e-24 ? std::clamp (std::sqrt (sdd / sxx), wMin, wMax) : 2.0 * pi * 60.0;
        const double q = d[(size_t) i] / w;
        out[(size_t) i] = 0.5 * ((double) x[i] * x[i] + q * q);
    }
    return out;
}

std::vector<float> rmsEnvelopeDb (const float* x, int n, int win, int hop, float floorDb)
{
    std::vector<float> env;
    if (n < win || win < 1 || hop < 1) return env;

    const int frames = 1 + (n - win) / hop;
    env.resize ((size_t) frames);

    double peak = 0;
    for (int f = 0; f < frames; ++f)
    {
        const double v = rms (x + f * hop, win);
        env[(size_t) f] = (float) v;
        peak = std::max (peak, v);
    }

    for (auto& v : env)
    {
        if (peak <= 1.0e-9) { v = floorDb; continue; }
        v = (float) std::max ((double) floorDb, dbFromRatio (v / peak));
    }
    return env;
}

//==============================================================================
std::string noteName (double hz)
{
    if (! (hz > 0.0)) return {};
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int midi = (int) std::lround (69.0 + 12.0 * std::log2 (hz / 440.0));
    const int pc = ((midi % 12) + 12) % 12;
    const int octave = (int) std::floor (midi / 12.0) - 1;
    return std::string (names[pc]) + std::to_string (octave);
}

double centsBetween (double f1, double f2)
{
    if (f1 <= 0 || f2 <= 0) return 0.0;
    return 1200.0 * std::log2 (f2 / f1);
}

std::string formatFixed (double v, int decimals)
{
    char buf[64];
    std::snprintf (buf, sizeof (buf), "%.*f", decimals, std::abs (v));
    std::string s = buf;
    // avoid "-0.00"
    bool allZero = true;
    for (char c : s) if (c >= '1' && c <= '9') allZero = false;
    if (v < 0 && ! allZero) s = "\xE2\x88\x92" + s;
    return s;
}

std::string formatSigned (double v, int decimals)
{
    char buf[64];
    std::snprintf (buf, sizeof (buf), "%.*f", decimals, std::abs (v));
    std::string s = buf;
    bool allZero = true;
    for (char c : s) if (c >= '1' && c <= '9') allZero = false;
    if (allZero) return s;
    return (v < 0 ? std::string ("\xE2\x88\x92") : std::string ("+")) + s;
}

} // namespace pc
