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

#include <limits>
#include <span>
#include <vector>

#include <QList>
#include <QString>
#include <QStringList>

#include "nuclearstopping.h"
#include "project.h"

// Names of JIBAL's GSTO files of electronic stopping, which physics settings can choose from
QStringList stoppingFileNames();
QStringList stoppingFileNames(int z1, int z2); // Of the files with stopping of Z1 in Z2, or in some elements if AnyElement

// Stopping cross sections of a particle in elements with the physics settings: electronic stopping from the GSTO file
// chosen for each pair of elements, or by JIBAL, and universal nuclear stopping (see nuclearstopping.h), each multiplied
// by its correction factors.
//
// When the table is made, the electronic stopping is copied from the file at the energies of the file's own points, and
// the nuclear stopping is calculated at the same energies. Their sum is interpolated between the points as the physics
// settings say: linearly in energy, as JIBAL does, or with a cubic spline in the logarithm of the energy. The interval
// of an energy is calculated directly when the file's points are evenly spaced in energy or in its logarithm, and
// searched otherwise.
//
// The stopping is known from minEnergy() to maxEnergy(), where the files of all the elements have data. Outside it the
// nuclear stopping is calculated, and electronic stopping is taken proportional to the velocity below it and as the
// last value of the file above it; the users of the table leave out or refuse such energies.
class StoppingTable {
public:
    StoppingTable() = default; // Empty, not valid
    StoppingTable(int z, double mass, const QList<int> &elements, const PhysicsSettings &physics);

    bool isValid() const { return m_errorMessage.isEmpty() && !m_elementData.empty(); }
    const QString &errorMessage() const { return m_errorMessage; } // Why it is not valid
    const QList<int> &elements() const { return m_elements; }
    // Lowest and highest energy in J where the electronic stopping files of all the elements have data. Outside them
    // the stopping of some element is not known.
    double minEnergy() const { return m_minEnergy; }
    double maxEnergy() const { return m_maxEnergy; }
    // The element whose file ends at maxEnergy(), as an index of elements()
    qsizetype maxEnergyElement() const { return m_maxEnergyElement; }
    // Name of the electronic stopping file used for each element, in the order of elements()
    const QStringList &fileNames() const { return m_fileNames; }
    // Stopping cross section in J m² in a mixture of the elements, atomic fractions in the order of elements()
    double stopping(std::span<const double> fractions, double energy) const;

private:
    enum class Spacing { Logarithmic, Linear, Arbitrary };
    // Where an energy is in a grid
    struct Location {
        int region; // -1 below the grid, 0 in it, 1 above it
        size_t i; // Interval from energies[i] to energies[i + 1]
        double t; // Position in the interval in energy, from 0 to 1, for linear interpolation
        double u; // Position in the interval in the logarithm of energy, for cubic interpolation
    };
    // Energies of the points of a stopping file, shared by the elements that use the file
    struct Grid {
        const void *file; // Identifies the file
        std::vector<double> energies; // J, increasing
        std::vector<double> logEnergies; // ln(energies / J)
        Spacing spacing = Spacing::Arbitrary;
        double invStep = 0; // 1 / the step in energy or in its logarithm when evenly spaced
        std::vector<size_t> elements; // Indices of the elements using it
        Location locate(double energy, bool cubic) const;
    };
    struct ElementStopping {
        std::vector<double> values; // J m², electronic and nuclear stopping with their factors at the grid energies
        std::vector<double> coefficients; // Of the cubic in u of each interval, four for each, with cubic interpolation
        double firstElectronic = 0; // J m², with the factor, at the first and last energy of the grid
        double lastElectronic = 0;
        double nuclearFactor = 1;
        NuclearStopping nuclear;
    };
    QString m_errorMessage;
    QList<int> m_elements;
    double m_minEnergy = 0;
    double m_maxEnergy = 0;
    qsizetype m_maxEnergyElement = 0;
    QStringList m_fileNames;
    bool m_cubic = false;
    std::vector<Grid> m_grids;
    std::vector<ElementStopping> m_elementData; // Of each element
};

// How the stopping tables and slowing down are calculated, as text for logs
QString stoppingNumerics();

// Energy in J after slowing down along a path, as atoms/m², in a mixture of the table's elements. Zero when the
// particle stops. At most maxSteps Runge-Kutta steps are taken, particles that would need more stop or nearly.
double slowDown(const StoppingTable &table, std::span<const double> fractions, double energy, double pathLength,
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
    // Lowest and highest energy in J where the stopping in the foil is known, see StoppingTable::minEnergy(). Without
    // a foil, zero and infinity.
    double minEnergy() const { return m_loss ? m_table.minEnergy() : 0; }
    double maxEnergy() const { return m_loss ? m_table.maxEnergy() : std::numeric_limits<double>::infinity(); }

    // Energy before the foil from the energy after it, interpolated in a table. Call tabulate() first with the
    // highest energy after the foil that is needed. Higher energies are extrapolated with the loss at the top of the
    // table.
    // The table ends at maxEnergy(). Energies after the foil above what it reaches give NaN.
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
    bool m_limited = false; // The table ends at maxEnergy() before reaching the energy asked for
};

#endif // JARA_STOPPING_H
