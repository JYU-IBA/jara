/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#ifndef JARA_CROSSSECTION_H
#define JARA_CROSSSECTION_H

// Rutherford cross sections in the laboratory frame, m²/sr, of incident ions (z1, mass m1 in kg) with energy E (J) and
// target atoms (z2, m2), optionally with the Andersen screening correction. The same as JIBAL's
// jibal_cross_section_rbs() and jibal_cross_section_erd(). Pure functions, safe to call from several threads.

// Ions scattered to the angle theta (rad)
double rbsCrossSection(int z1, double m1, int z2, double m2, double theta, double energy, bool andersen);
// Target atoms recoiled to the angle phi (rad)
double erdCrossSection(int z1, double m1, int z2, double m2, double phi, double energy, bool andersen);
// Andersen's screening correction factor, centre-of-mass energy (J) and scattering angle (rad)
double andersenCorrection(int z1, int z2, double energyCm, double thetaCm);

#endif // JARA_CROSSSECTION_H
