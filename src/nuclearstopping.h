/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#ifndef JARA_NUCLEARSTOPPING_H
#define JARA_NUCLEARSTOPPING_H

#include <cstdint>
#include <cstring>
#include <vector>

// Universal (ZBL) nuclear stopping. The reduced stopping S(ε) is a function of the reduced energy ε only. It is
// tabulated once for the whole program as cubic Hermite polynomials, 32 segments for each power of two of ε from 2⁻²⁰
// to 2²⁴, from the formula and its derivative. A segment is found from the binary exponent and mantissa of ε. The
// relative difference from the formula is below 1e-7. Outside the table the formula is used.
//
// For an incident ion (Z1, m1) in a target atom (Z2, m2), ε = E · ε0 and the stopping cross section is k · S(ε), with
// ε0 and k depending only on the pair. The formulas are the same as in JIBAL's jibal_gsto_stop_nuclear_universal().

namespace NuclearStoppingTable {
constexpr double EpsilonHigh = 30; // Above this the high energy formula is used, it is a segment end
constexpr int SegmentBits = 5; // 2^5 segments for each power of two
constexpr int ExponentMin = -20;
constexpr int ExponentMax = 24;
constexpr int Segments = (ExponentMax - ExponentMin) << SegmentBits;
extern const std::vector<double> coefficients; // Four for each segment, of the cubic in t from 0 to 1
}

double reducedNuclearStoppingExact(double epsilon); // From the formula

// From the table, inline as it is called for every element in every step of slowing down
inline double reducedNuclearStopping(double epsilon) {
    using namespace NuclearStoppingTable;
    if(!(epsilon > 0)) {
        return 0;
    }
    std::uint64_t bits;
    std::memcpy(&bits, &epsilon, sizeof(bits));
    const int exponent = static_cast<int>(bits >> 52) - 1023; // Epsilon is positive, the sign bit is zero
    if(exponent < ExponentMin || exponent >= ExponentMax) {
        return reducedNuclearStoppingExact(epsilon);
    }
    const std::uint64_t mantissaBits = (bits & 0x000FFFFFFFFFFFFFULL) | 0x3FF0000000000000ULL; // In [1, 2)
    double mantissa;
    std::memcpy(&mantissa, &mantissaBits, sizeof(mantissa));
    const double u = (mantissa - 1) * (1 << SegmentBits);
    const int j = static_cast<int>(u); // Segment within the power of two
    const double t = u - j;
    const double *c = coefficients.data() + 4 * (((exponent - ExponentMin) << SegmentBits) + j);
    return c[0] + t * (c[1] + t * (c[2] + t * c[3]));
}

// Nuclear stopping of an ion in the atoms of one element: with the average mass of the element, or as the sum over its
// natural isotopes weighted by their abundances
class NuclearStopping {
public:
    NuclearStopping() = default; // No stopping
    NuclearStopping(int z1, double m1, int z2, bool isotopes); // Masses in kg
    double stopping(double energy) const { // J m², energy in J
        double sum = m_first.k * reducedNuclearStopping(energy * m_first.eps0);
        for(const Term &term : m_others) {
            sum += term.k * reducedNuclearStopping(energy * term.eps0);
        }
        return sum;
    }

private:
    struct Term {
        double eps0 = 0; // Reduced energy per J
        double k = 0; // J m², times the abundance of an isotope
    };
    Term m_first; // The average mass or the first isotope, without a vector for the common case
    std::vector<Term> m_others; // The other isotopes
};

#endif // JARA_NUCLEARSTOPPING_H
