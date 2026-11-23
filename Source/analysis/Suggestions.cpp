#include "AnalysisCore.h"
#include "AnalysisUtils.h"

#include <cmath>
#include <cstdio>

namespace pc
{

namespace
{
    const std::string degree = "\xC2\xB0";
    const std::string arrow = "\xE2\x86\x92";
    const std::string ellipsis = "\xE2\x80\xA6";

    std::string ms (double v) { return formatFixed (std::abs (v), 1); }

    std::string hz (double v)
    {
        char buf[32];
        std::snprintf (buf, sizeof (buf), "%.0f", v);
        return buf;
    }

    const char* inPhaseText = "Kick and sidechain are aligned and in phase. Low end sums cleanly.";
}

std::string buildHitSuggestion (const HitResult& h, const KickState& s, bool sidechainLive)
{
    if (! sidechainLive)
        return "No sidechain signal. Route the other track into Phase Check's Sidechain input and press play.";
    if (! h.hasData)
        return "Waiting for kick hits" + ellipsis;

    const double c = h.correlation;
    const bool inv = s.invert;
    const bool dly = s.effectiveDelayMs() > 0.0;
    const bool rot = s.rotateDeg > 0.0;
    const bool early = h.offsetClear && h.offsetMs < -0.05;
    const bool late = h.offsetClear && h.offsetMs > 0.05 && h.predictedDelayValid;
    const std::string delayText = ms (h.predictedDelayMs);

    std::string t;

    if (early)
    {
        t = "The sidechain is " + ms (h.offsetMs) + " ms early. The kick can't be moved earlier: delay the sidechain track by "
            + ms (h.offsetMs) + " ms in Studio Pro instead (Track Delay).";
        if (c < 0 && ! rot)
            t += " Meanwhile, flipping polarity takes the correlation to " + formatSigned (h.predictedFlip) + ".";
    }
    else if (rot)
    {
        t = "Phase rotate " + std::to_string ((int) std::lround (s.rotateDeg)) + degree + ": correlation "
            + formatSigned (c) + ". Rotation shifts the kick low-end phase without moving its timing. Best for kick vs. bass.";
    }
    else if (! inv && ! dly)
    {
        if (c < 0)
        {
            t = "Flip polarity first: correlation rises to " + formatSigned (h.predictedFlip) + ".";
            if (late)
                t += " Then delay the kick " + delayText + " ms to line up the onsets (" + formatSigned (h.predictedBoth) + ").";
        }
        else if (late)
            t = "Delay the kick " + delayText + " ms so both hits start together: correlation goes to " + formatSigned (h.predictedDelay) + ".";
        else if (c >= 0.5)
            t = inPhaseText;
        else
            t = "Partly in phase (" + formatSigned (c) + "). Flipping polarity would make it worse; phase rotation is the better tool here.";
    }
    else if (inv && ! dly)
    {
        if (late)
            t = "Good. Now delay the kick " + delayText + " ms so both hits start together: correlation goes to " + formatSigned (h.predictedDelay) + ".";
        else if (c >= 0.5)
            t = inPhaseText;
        else if (c < 0)
            t = "With the polarity flipped it is still out of phase (" + formatSigned (c) + "). Turn Invert off and try phase rotation.";
        else
            t = "Partly in phase (" + formatSigned (c) + "). Fine-tune with phase rotate.";
    }
    else if (dly && ! inv)
    {
        if (c < 0)
            t = "Onsets are aligned but polarity is opposite, so the low end cancels harder. Flip polarity.";
        else if (c >= 0.5)
            t = inPhaseText;
        else
            t = "Onsets are aligned and partly in phase (" + formatSigned (c) + "). Fine-tune with phase rotate.";
    }
    else
    {
        if (c >= 0.5)
            t = inPhaseText;
        else if (c < 0)
            t = "Still out of phase (" + formatSigned (c) + "). Try turning the polarity flip off.";
        else
            t = "Aligned and flipped, partly in phase (" + formatSigned (c) + "). Fine-tune with phase rotate.";
    }

    if (h.predictedBestRotate - c > 0.1 && h.bestRotateDeg > 0
        && std::abs (h.bestRotateDeg - s.rotateDeg) >= 5.0)
        t += " Try about " + std::to_string (h.bestRotateDeg) + degree + " phase rotation (" + formatSigned (h.predictedBestRotate) + ").";

    if (h.correlationStdDev > 0.3)
        t += " Readings vary from hit to hit (normal against bass). Trust your ears in mono.";

    return t;
}

std::string buildOverlapSuggestion (const OverlapResult& o, const KickState&, bool sidechainLive)
{
    if (! sidechainLive)
        return "No sidechain signal. Route the bass into Phase Check's Sidechain input and press play.";
    if (! o.hasBar)
        return "Waiting for a full bar" + ellipsis + " Overlap mode needs the song playing.";
    if (o.noOverlap)
        return "Kick and bass don't overlap: no phase interaction. Nothing to fix.";

    std::string t;
    if (o.aValid)
    {
        const double a = o.aCorrelation;
        const std::string n = std::to_string (o.bestRotateValid ? o.bestRotateDeg : 0) + degree;
        if (a < -0.2)
            t = "At the kick-tail " + arrow + " bass handover they are out of phase (" + formatSigned (a) + "). Flip polarity, or rotate toward " + n + ".";
        else if (a < 0.5)
            t = "Handover is partly in phase (" + formatSigned (a) + "). Fine-tune with phase rotate (try " + n + ").";
        else
            t = "Handover sums cleanly (" + formatSigned (a) + ").";
    }

    for (const auto& note : o.notes)
    {
        if (note.matched || ! note.pitchValid || ! o.kickPitchValid) continue;
        if (! t.empty()) t += " ";
        t += "On " + note.position + " the " + note.name + " note beats against the " + hz (o.kickTailHz)
             + " Hz kick tail: shorten the tail or retune that note.";
    }

    if (o.bCount > 0 && o.bDurationMs > 30.0)
    {
        if (! t.empty()) t += " ";
        t += "The bass is still sounding when the kick hits (B zones avg " + std::to_string ((int) std::lround (o.bDurationMs))
             + " ms): shorten the bass release or duck it harder.";
    }

    if (t.empty())
        t = o.noBass ? "No bass notes detected in this bar." : "The overlaps this bar are all beating notes: polarity or rotation can't fix them.";

    return t;
}

} // namespace pc
