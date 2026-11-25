/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#include "crosssection.h"
#include "units.h"

#include <cmath>

namespace {
double square(double x) {
    return x * x;
}
}

double rbsCrossSection(int z1, double m1, int z2, double m2, double theta, double energy, bool andersen) {
    const double energyCm = m2 * energy / (m1 + m2);
    const double r = m1 / m2;
    const double thetaCm = theta + std::asin(r * std::sin(theta));
    const double sigmaCm = square(z1 * C_E * z2 * C_E / (4 * C_PI * C_EPSILON0)) * square(1 / (4 * energyCm))
                           / square(square(std::sin(thetaCm / 2)));
    const double sigma = sigmaCm * std::pow(1 + r * r + 2 * r * std::cos(thetaCm), 1.5) / (1 + r * std::cos(thetaCm));
    return andersen ? andersenCorrection(z1, z2, energyCm, thetaCm) * sigma : sigma;
}

double erdCrossSection(int z1, double m1, int z2, double m2, double phi, double energy, bool andersen) {
    const double cosine = std::cos(phi);
    const double sigma = square(z1 * C_E * z2 * C_E / (8 * C_PI * C_EPSILON0 * energy)) * square(1 + m1 / m2)
                         / (cosine * cosine * cosine);
    if(!andersen) {
        return sigma;
    }
    const double energyCm = m2 * energy / (m1 + m2);
    return andersenCorrection(z1, z2, energyCm, C_PI - 2 * phi) * sigma;
}

double andersenCorrection(int z1, int z2, double energyCm, double thetaCm) {
    const double rVE = 48.73 * C_EV * z1 * z2 * std::sqrt(std::pow(z1, 2.0 / 3.0) + std::pow(z2, 2.0 / 3.0)) / energyCm;
    return square(1 + 0.5 * rVE) / square(1 + rVE + square(0.5 * rVE / std::sin(thetaCm / 2)));
}
