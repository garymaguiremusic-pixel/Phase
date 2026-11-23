#include "KickProcessor.h"

#include <algorithm>
#include <cmath>

namespace pc
{

void KickProcessor::prepare (double sampleRate, int numChannels)
{
    fs = sampleRate > 0 ? sampleRate : 44100.0;
    channelsPrepared = std::clamp (numChannels, 1, maxChannels);

    const auto coefs = HilbertDesigner::design (fs);
    for (auto& h : hilbert)
    {
        h.setCoefficients (coefs);
        h.reset();
    }

    for (auto& d : delays)
        d.prepare ((int) std::ceil (fs * maxDelayMs / 1000.0) + 4);

    polarity.reset (fs, 0.010);
    rotateMix.reset (fs, 0.020);
    theta.reset (fs, 0.030);
    delaySamples.reset (fs, 0.050);
    reset();
}

void KickProcessor::reset()
{
    for (auto& h : hilbert) h.reset();
    for (auto& d : delays) d.reset();

    polarity.setCurrentAndTarget (polarity.getTarget() < 0 ? -1.0 : 1.0);
    rotateMix.setCurrentAndTarget (rotateMix.getTarget());
    theta.setCurrentAndTarget (theta.getTarget());
    delaySamples.setCurrentAndTarget (delaySamples.getTarget());
    lastTheta = theta.getCurrent();
    cosT = std::cos (lastTheta);
    sinT = std::sin (lastTheta);
}

void KickProcessor::setTargets (bool invert, double delayMs, double rotateDegrees) noexcept
{
    polarity.setTarget (invert ? -1.0 : 1.0);

    const double clampedDelay = std::clamp (delayMs, 0.0, maxDelayMs);
    delaySamples.setTarget (clampedDelay * fs / 1000.0);

    const double deg = std::clamp (rotateDegrees, 0.0, 180.0);
    if (deg > 0.0)
    {
        // Coming out of bypass: start the angle at the new value so the
        // crossfade blends dry -> rotated(target) without an extra sweep.
        if (exactlyEqual (rotateMix.getCurrent(), 0.0) && ! rotateMix.isRamping())
            theta.setCurrentAndTarget (deg * 3.14159265358979323846 / 180.0);
        else
            theta.setTarget (deg * 3.14159265358979323846 / 180.0);
        rotateMix.setTarget (1.0);
    }
    else
    {
        // Returning to 0 deg: hold the angle and fade to the dry (bypassed) signal.
        rotateMix.setTarget (0.0);
    }
}

void KickProcessor::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    numChannels = std::min (numChannels, channelsPrepared);

    for (int i = 0; i < numSamples; ++i)
    {
        const double mix = rotateMix.next();
        const double th = theta.next();
        const double pol = polarity.next();
        const double dly = delaySamples.next();

        if (! exactlyEqual (th, lastTheta))
        {
            lastTheta = th;
            cosT = std::cos (th);
            sinT = std::sin (th);
        }

        const bool rotating = mix > 0.0;
        const bool delaying = dly > 0.0;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* data = channels[ch];
            const float x = data[i];

            // The all-pass pair always runs so it is warm when the knob leaves 0 deg.
            double I, Q;
            hilbert[(size_t) ch].process ((double) x, I, Q);

            float y = x;
            if (rotating)
            {
                const double r = I * cosT - Q * sinT;
                y = (float) ((double) x + mix * (r - (double) x));
            }

            if (! exactlyEqual (pol, 1.0))
                y = (float) ((double) y * pol);

            auto& dl = delays[(size_t) ch];
            dl.push (y);
            if (delaying)
                y = dl.read (dly);

            data[i] = y;
        }
    }
}

} // namespace pc
