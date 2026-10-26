#pragma once

#include <QList>

#include <algorithm>
#include <cmath>
#include <vector>

// Equalizer bands, low to high (Hz).
inline constexpr double BandFrequencies[] = {90, 250, 700, 2000, 5000};
inline constexpr int BandCount = 5;

// Level of each band over one window of mono samples: Goertzel amplitude (Hann-windowed),
// mapped from -60..0 dBFS to 0..1.
inline QList<double> bandLevels(const float *samples, int n, int rate)
{
    thread_local std::vector<double> hann; // the window shape, worked out once per length
    if (int(hann.size()) != n) {
        hann.resize(n);
        for (int i = 0; i < n; ++i)
            hann[i] = 0.5 - 0.5 * std::cos(2 * M_PI * i / (n - 1));
    }
    QList<double> out;
    for (double f : BandFrequencies) {
        const double coeff = 2 * std::cos(2 * M_PI * f / rate);
        double s1 = 0, s2 = 0;
        for (int i = 0; i < n; ++i) {
            const double s = samples[i] * hann[i] + coeff * s1 - s2;
            s2 = s1;
            s1 = s;
        }
        const double power = std::max(0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2);
        const double amplitude = 2 * std::sqrt(power) / (n * 0.5); // 0.5 = Hann coherent gain
        const double db = 20 * std::log10(amplitude + 1e-9);
        out << std::clamp((db + 60) / 60, 0.0, 1.0);
    }
    return out;
}

// A bar's level after `dtMs` of moving toward `target`: quick to rise, slower to fall, and the same
// result however the time is cut into steps, so uneven audio updates don't make the bars jitter.
inline double smoothLevel(double prev, double target, double dtMs)
{
    const double tau = target > prev ? 70.0 : 260.0; // ms: calm rise, slow fall, so the bars glide rather than flutter
    return target + (prev - target) * std::exp(-dtMs / tau);
}
