#pragma once

#include "HilbertRotator.h"
#include "../analysis/Snapshot.h"

#include <algorithm>
#include <array>
#include <vector>

namespace pc
{

//==============================================================================
/** A linear ramp that lands exactly on its target (so 1.0 stays 1.0 and the
    plug-in can be bit-transparent once a ramp has finished). */
class LinearRamp
{
public:
    void reset (double sampleRate, double rampSeconds) noexcept
    {
        rampSamples = std::max (1, (int) (sampleRate * rampSeconds + 0.5));
        countdown = 0;
        current = target;
    }

    void setCurrentAndTarget (double v) noexcept { current = target = v; countdown = 0; }

    void setTarget (double v) noexcept
    {
        if (exactlyEqual (v, target))
            return;
        target = v;
        if (exactlyEqual (current, target)) { countdown = 0; return; }
        countdown = rampSamples;
        step = (target - current) / (double) rampSamples;
    }

    inline double next() noexcept
    {
        if (countdown > 0)
        {
            if (--countdown == 0) current = target;
            else current += step;
        }
        return current;
    }

    bool isRamping() const noexcept { return countdown > 0; }
    double getCurrent() const noexcept { return current; }
    double getTarget() const noexcept { return target; }

private:
    double current = 0.0, target = 0.0, step = 0.0;
    int rampSamples = 1, countdown = 0;
};

//==============================================================================
/** Fractional delay line with 4-point (3rd order) Lagrange interpolation. */
class FractionalDelay
{
public:
    void prepare (int maxDelaySamples)
    {
        int size = 1;
        while (size < maxDelaySamples + 8) size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        writePos = 0;
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    inline void push (float x) noexcept
    {
        buffer[(size_t) writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    /** Reads the sample written `delay` samples before the most recent push (delay >= 0). */
    inline float read (double delay) const noexcept
    {
        const int maxD = mask - 4;
        if (delay > maxD) delay = maxD;
        if (delay < 0.0) delay = 0.0;

        const int whole = (int) delay;
        const int base = whole > 0 ? whole - 1 : 0;   // points at base .. base+3
        const double t = delay - (double) base;       // evaluation point in [0, 3]

        const float y0 = tap (base), y1 = tap (base + 1), y2 = tap (base + 2), y3 = tap (base + 3);
        const double d0 = t, d1 = t - 1.0, d2 = t - 2.0, d3 = t - 3.0;

        const double w0 = -(d1 * d2 * d3) / 6.0;
        const double w1 =  (d0 * d2 * d3) / 2.0;
        const double w2 = -(d0 * d1 * d3) / 2.0;
        const double w3 =  (d0 * d1 * d2) / 6.0;
        return (float) (w0 * y0 + w1 * y1 + w2 * y2 + w3 * y3);
    }

private:
    inline float tap (int samplesAgo) const noexcept
    {
        return buffer[(size_t) ((writePos - 1 - samplesAgo) & mask)];
    }

    std::vector<float> buffer;
    int mask = 0, writePos = 0;
};

//==============================================================================
/** The kick path: phase rotate -> polarity -> delay. Real-time safe after prepare(). */
class KickProcessor
{
public:
    static constexpr int maxChannels = 2;
    static constexpr double maxDelayMs = 20.0;

    void prepare (double sampleRate, int numChannels);
    void reset();

    /** Targets; call once per block before process(). */
    void setTargets (bool invert, double delayMs, double rotateDegrees) noexcept;

    /** Processes in place. */
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    double getSampleRate() const noexcept { return fs; }

private:
    double fs = 44100.0;
    int channelsPrepared = 0;

    std::array<HilbertPair, maxChannels> hilbert;
    std::array<FractionalDelay, maxChannels> delays;

    LinearRamp polarity;   // +1 / -1, 10 ms
    LinearRamp rotateMix;  // 0 = bypass, 1 = rotated, 20 ms
    LinearRamp theta;      // radians, 30 ms
    LinearRamp delaySamples;

    double cosT = 1.0, sinT = 0.0, lastTheta = 0.0;
};

} // namespace pc
