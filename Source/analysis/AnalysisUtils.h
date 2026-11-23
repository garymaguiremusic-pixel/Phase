#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace pc
{

//==============================================================================
/** Transposed direct form II biquad in double precision. */
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() noexcept { z1 = z2 = 0; }

    static Biquad lowPass (double fs, double f, double q);
    static Biquad bandPass (double fs, double f, double q);   // 0 dB peak gain
};

/** 4th-order Butterworth low-pass (two cascaded biquads). Both signals of a
    comparison always go through identical copies, so relative phase is kept. */
struct LowPass4
{
    Biquad s1, s2;

    void prepare (double fs, double cutoff);
    void reset() noexcept { s1.reset(); s2.reset(); }
    inline double process (double x) noexcept { return s2.process (s1.process (x)); }
};

constexpr double kAnalysisCutoffHz = 250.0;

/** Filters a whole buffer from rest (state starts at zero). */
void lowPass (const float* in, float* out, int n, double fs, double cutoff = kAnalysisCutoffHz);
void bandPass (const float* in, float* out, int n, double fs, double centre, double q);

/** Q of a band-pass whose bandwidth is the given number of octaves. */
double octaveBandwidthToQ (double octaves);

//==============================================================================
/** Pearson correlation (means removed). Returns 0 and valid=false when either signal is silent. */
double pearson (const float* a, const float* b, int n, bool* valid = nullptr);

/** Normalised dot product (no mean removal). */
double normalisedDot (const float* a, const float* b, int n, bool* valid = nullptr);

double sumSquares (const float* x, int n);
double rms (const float* x, int n);

struct XCorrResult
{
    double lagSamples = 0.0;  // positive = b is later than a
    double peak = 0.0;        // signed normalised correlation at the peak
    bool valid = false;
};

/** Normalised cross-correlation of a[0..n) against b[lag .. lag+n) for |lag| <= maxLag.
    `b` must be readable from index -maxLag to n + maxLag. Picks the peak of |r| and
    refines it with parabolic interpolation. */
XCorrResult crossCorrelate (const float* a, const float* b, int n, int maxLag);

/** Correlation of a delayed copy of `a` with `b`: sum a[i - lag] b[i] / norms, with a
    fractional lag (linear between the two neighbouring integer lags). `a` must be
    readable at negative indices down to -(ceil(lag)+1). */
double correlationWithDelay (const float* a, const float* b, int n, double lagSamples);

//==============================================================================
/** YIN-style fundamental estimate between fMin and fMax. The input should already
    be low-passed (it is decimated internally for speed). Returns 0 when no pitch. */
double estimatePitchHz (const float* x, int n, double fs, double fMin, double fMax, double* clarity = nullptr);

/** Magnitude of the Hann-windowed DFT of x at frequency f. */
double goertzelMagnitude (const float* x, int n, double fs, double f);

/** Local squared amplitude of a low-frequency signal: (x^2 + (x'/w)^2) / 2, where w is
    the signal's RMS angular frequency. For a sine at w this is constant (A^2 / 2), so
    its moving average is an RMS envelope without the ripple a 5 ms window has on a
    50 Hz tail, and unlike an FFT Hilbert envelope it doesn't leak before onsets. */
std::vector<double> quadraturePower (const float* x, int n, double fs);

/** RMS envelope in dB relative to its own peak (frames of `win` samples, step `hop`). */
std::vector<float> rmsEnvelopeDb (const float* x, int n, int win, int hop, float floorDb = -120.0f);

//==============================================================================
/** Note name with octave (A4 = 440 Hz), e.g. "A1". Empty for f <= 0. */
std::string noteName (double hz);
double centsBetween (double f1, double f2);

/** "+0.71" / "−0.58" with a real minus sign (UTF-8). */
std::string formatSigned (double v, int decimals = 2);
std::string formatFixed (double v, int decimals);

inline double dbFromRatio (double r) { return 20.0 * std::log10 (std::max (r, 1.0e-12)); }

inline bool isFiniteNumber (double v) { return std::isfinite (v); }

} // namespace pc
