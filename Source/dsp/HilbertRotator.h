#pragma once

#include <array>
#include <cmath>
#include <vector>

namespace pc
{

//==============================================================================
/** Designs the coefficients of a 90-degree phase-difference all-pass pair.

    The design is the classic polyphase-IIR / elliptic half-band method (as used
    by Olli Niemitalo's well-known Hilbert pair and Laurent de Soras' HIIR),
    computed at run time so the coefficients are right for every sample rate.

    With 12 coefficients and a 15 Hz lower edge the phase difference stays within
    0.3 degrees of 90 from 20 Hz to 20 kHz at 44.1 - 192 kHz.
*/
struct HilbertDesigner
{
    static constexpr int numCoefs = 12;
    static constexpr double lowEdgeHz = 15.0;

    static std::array<double, numCoefs> design (double sampleRate);
    static std::vector<double> designGeneric (int numCoefs, double transition);
};

//==============================================================================
/** One channel of the all-pass pair. Each path is a cascade of second-order
    all-pass sections H(z) = (c - z^-2) / (1 - c z^-2); path B has an extra
    one-sample delay. Output I (path A) leads Q (path B) by 90 degrees.
*/
class HilbertPair
{
public:
    void setCoefficients (const std::array<double, HilbertDesigner::numCoefs>& c) noexcept { coefs = c; }

    void reset() noexcept
    {
        for (auto& s : stages) s = {};
        prevInput = 0.0;
    }

    inline void process (double x, double& outI, double& outQ) noexcept
    {
        double a = x;
        double b = prevInput;
        prevInput = x;

        for (int i = 0; i < HilbertDesigner::numCoefs; i += 2)
        {
            a = stages[(size_t) i].tick (a, coefs[(size_t) i]);
            b = stages[(size_t) i + 1].tick (b, coefs[(size_t) i + 1]);
        }

        outI = a;
        outQ = b;
    }

private:
    struct Stage
    {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

        inline double tick (double x, double c) noexcept
        {
            const double y = c * (x + y2) - x2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return y;
        }
    };

    std::array<double, HilbertDesigner::numCoefs> coefs {};
    std::array<Stage, HilbertDesigner::numCoefs> stages {};
    double prevInput = 0.0;
};

//==============================================================================
/** Offline helper: rotate a whole buffer by a fixed angle (used by the analysis
    to predict what a given phase-rotate setting would do). theta == 0 returns
    the input unchanged, exactly like the real-time path which bypasses at 0 deg.
*/
void rotateBuffer (const float* in, float* out, int n, double sampleRate, double thetaDegrees);

} // namespace pc
