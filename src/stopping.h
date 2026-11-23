/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#ifndef JARA_STOPPING_H
#define JARA_STOPPING_H

#include <vector>

#include <QList>
#include <QString>
#include <QStringList>

#include "project.h"

// Names of JIBAL's GSTO files of electronic stopping, which physics settings can choose from
QStringList stoppingFileNames();
QStringList stoppingFileNames(int z1, int z2); // Of the files with stopping of Z1 in Z2, or in some elements if AnyElement

// Stopping cross sections of a particle in elements, tabulated with the physics settings: electronic stopping from the
// GSTO file chosen for each pair of elements, or by JIBAL, and universal nuclear stopping, each multiplied by its
// correction factors. The table covers energies from 1 keV to 2 GeV evenly on a logarithmic scale and is
// interpolated linearly. Below it electronic stopping is taken proportional to the velocity, above it the last values
// are used.
class StoppingTable {
public:
    StoppingTable() = default; // Empty, not valid
    StoppingTable(int z, double mass, const QList<int> &elements, const PhysicsSettings &physics);

    bool isValid() const { return m_errorMessage.isEmpty() && !m_values.empty(); }
    const QString &errorMessage() const { return m_errorMessage; } // Why it is not valid
    const QList<int> &elements() const { return m_elements; }
    // Stopping cross section in J m² in a mixture of the elements, atomic fractions in the order of elements()
    double stopping(const std::vector<double> &fractions, double energy) const;

private:
    QString m_errorMessage;
    QList<int> m_elements;
    std::vector<std::vector<double>> m_values; // [element][point]
};

// Energy in J after slowing down along a path, as atoms/m², in a mixture of the table's elements. Zero when the
// particle stops. At most maxSteps Runge-Kutta steps are taken, particles that would need more stop or nearly.
double slowDown(const StoppingTable &table, const std::vector<double> &fractions, double energy, double pathLength,
                int maxSteps = 1000);

// Energy loss of a particle in a foil, with stopping from the physics settings
class FoilEnergyLoss {
public:
    // Particle with atomic number z and mass in kg, foil thickness as areal density in kg/m². A foil without thickness
    // causes no loss.
    FoilEnergyLoss(int z, double mass, const Material &material, double thickness, const PhysicsSettings &physics);

    bool isValid() const { return m_errorMessage.isEmpty(); }
    const QString &errorMessage() const { return m_errorMessage; } // Why it is not valid
    double energyAfter(double energy) const; // J, zero when the particle stops in the foil

    // Energy before the foil from the energy after it, interpolated in a table. Call tabulate() first with the
    // highest energy after the foil that is needed. Higher energies are extrapolated with the loss at the top of the
    // table.
    void tabulate(double maxEnergyAfter);
    double energyBefore(double energyAfter) const;

private:
    QString m_errorMessage;
    bool m_loss = false; // The foil has a thickness
    StoppingTable m_table;
    std::vector<double> m_fractions; // Of the foil's elements
    double m_pathLength = 0; // atoms/m²
    std::vector<double> m_before; // Energies before the foil, increasing
    std::vector<double> m_after; // Energies after the foil of the above, increasing
};

#endif // JARA_STOPPING_H
