#include "HilbertRotator.h"

#include <algorithm>

namespace pc
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    void transitionParams (double transition, double& k, double& q)
    {
        k = std::tan ((1.0 - transition * 2.0) * pi / 4.0);
        k *= k;
        const double kksqrt = std::pow (1.0 - k * k, 0.25);
        const double e = 0.5 * (1.0 - kksqrt) / (1.0 + kksqrt);
        const double e2 = e * e;
        const double e4 = e2 * e2;
        q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));
    }

    double accNum (double q, int order, int c)
    {
        double acc = 0.0, term = 0.0;
        int i = 0, j = 1;
        do
        {
            term = std::pow (q, (double) (i * (i + 1))) * std::sin ((i * 2 + 1) * c * pi / order) * j;
            acc += term;
            j = -j;
            ++i;
        } while (std::abs (term) > 1e-100 && i < 1000);
        return acc;
    }

    double accDen (double q, int order, int c)
    {
        double acc = 0.0, term = 0.0;
        int i = 1, j = -1;
        do
        {
            term = std::pow (q, (double) (i * i)) * std::cos (i * 2 * c * pi / order) * j;
            acc += term;
            j = -j;
            ++i;
        } while (std::abs (term) > 1e-100 && i < 1000);
        return acc;
    }
}

std::vector<double> HilbertDesigner::designGeneric (int n, double transition)
{
    double k = 0, q = 0;
    transitionParams (transition, k, q);
    const int order = n * 2 + 1;

    std::vector<double> out ((size_t) n);
    for (int idx = 0; idx < n; ++idx)
    {
        const int c = idx + 1;
        const double num = accNum (q, order, c) * std::pow (q, 0.25);
        const double den = accDen (q, order, c) + 0.5;
        const double ww = num / den;
        const double wwsq = ww * ww;
        const double x = std::sqrt ((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
        out[(size_t) idx] = (1.0 - x) / (1.0 + x);
    }
    return out;
}

std::array<double, HilbertDesigner::numCoefs> HilbertDesigner::design (double sampleRate)
{
    const double fs = std::max (8000.0, sampleRate);
    const double transition = std::clamp (2.0 * lowEdgeHz / fs, 1.0e-5, 0.45);
    const auto v = designGeneric (numCoefs, transition);

    std::array<double, numCoefs> out {};
    std::copy (v.begin(), v.end(), out.begin());
    return out;
}

void rotateBuffer (const float* in, float* out, int n, double sampleRate, double thetaDegrees)
{
    if (std::abs (thetaDegrees) < 1.0e-9)
    {
        if (in != out)
            std::copy (in, in + n, out);
        return;
    }

    HilbertPair pair;
    pair.setCoefficients (HilbertDesigner::design (sampleRate));
    const double th = thetaDegrees * pi / 180.0;
    const double c = std::cos (th), s = std::sin (th);

    for (int i = 0; i < n; ++i)
    {
        double I, Q;
        pair.process ((double) in[i], I, Q);
        out[i] = (float) (I * c - Q * s);
    }
}

} // namespace pc
