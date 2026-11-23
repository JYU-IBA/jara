/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#include "nuclearstopping.h"
#include "elements.h"

#include <cmath>

extern "C" {
#include <jibal_units.h>
}

namespace {
using namespace NuclearStoppingTable;

// Reduced stopping and its derivative
void lowEnergy(double epsilon, double &f, double &df) {
    const double l = std::log(1 + 1.1383 * epsilon);
    const double p = std::pow(epsilon, 0.21226);
    const double s = std::sqrt(epsilon);
    const double d = epsilon + 0.01321 * p + 0.19593 * s;
    const double dd = 1 + 0.01321 * 0.21226 * p / epsilon + 0.19593 * 0.5 / s;
    f = l / (2 * d);
    df = (1.1383 / (1 + 1.1383 * epsilon) * d - l * dd) / (2 * d * d);
}

void highEnergy(double epsilon, double &f, double &df) {
    f = std::log(epsilon) / (2 * epsilon);
    df = (1 - std::log(epsilon)) / (2 * epsilon * epsilon);
}

std::vector<double> makeTable() {
    std::vector<double> table(4 * Segments);
    for(int e = ExponentMin; e < ExponentMax; e++) {
        const double h = std::ldexp(1.0, e - SegmentBits); // Segment width
        for(int j = 0; j < (1 << SegmentBits); j++) {
            const double x0 = std::ldexp(1.0, e) + j * h;
            const double x1 = x0 + h;
            double f0, df0, f1, df1;
            if(x0 + h / 2 > EpsilonHigh) { // Both ends of a segment with the same formula
                highEnergy(x0, f0, df0);
                highEnergy(x1, f1, df1);
            } else {
                lowEnergy(x0, f0, df0);
                lowEnergy(x1, f1, df1);
            }
            double *c = table.data() + 4 * (((e - ExponentMin) << SegmentBits) + j);
            c[0] = f0;
            c[1] = h * df0;
            c[2] = 3 * (f1 - f0) - h * (2 * df0 + df1);
            c[3] = 2 * (f0 - f1) + h * (df0 + df1);
        }
    }
    return table;
}
}

// Made when the program starts
const std::vector<double> NuclearStoppingTable::coefficients = makeTable();

double reducedNuclearStoppingExact(double epsilon) {
    double f, df;
    if(epsilon <= EpsilonHigh) {
        lowEnergy(epsilon, f, df);
    } else {
        highEnergy(epsilon, f, df);
    }
    return f;
}

NuclearStopping::NuclearStopping(int z1, double m1, int z2, bool isotopes) {
    auto term = [z1, m1, z2](double weight, double m2) {
        const double zPower = std::pow(z1, 0.23) + std::pow(z2, 0.23);
        const double screeningLength = 0.8854 * C_BOHR_RADIUS / zPower;
        const double gamma = 4 * m1 * m2 / ((m1 + m2) * (m1 + m2));
        const double eps0 = 32.53 * m2 / (z1 * z2 * (m1 + m2) * zPower) / C_KEV;
        return Term{eps0, weight * C_PI * screeningLength * screeningLength * gamma / eps0};
    };
    std::vector<Term> terms;
    if(isotopes) {
        for(const Elements::Isotope &isotope : Elements::isotopes(z2)) {
            if(isotope.abundance > 0) {
                terms.push_back(term(isotope.abundance, isotope.mass));
            }
        }
    }
    if(terms.empty()) { // Average mass, also for an element without natural isotopes
        terms.push_back(term(1, Elements::mass(z2, 0)));
    }
    m_first = terms.front();
    m_others.assign(terms.begin() + 1, terms.end());
}
