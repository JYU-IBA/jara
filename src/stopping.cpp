/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#include "stopping.h"
#include "elements.h"

#include <algorithm>
#include <cmath>
#include <limits>

extern "C" {
#include <jibal.h>
}

namespace {
constexpr double MaxRelativeLossPerStep = 0.02; // In slowing down
constexpr int FoilTablePoints = 400;
constexpr double FoilTableMinEnergy = 1 * Units::Kiloelectronvolt;
constexpr int MaxFoilTableExtensions = 50;

// JIBAL's own choice of the electronic stopping file of a pair: an assignment from its configuration, otherwise the
// first file with the pair. jibal_gsto_auto_assign() would keep a file chosen for the pair earlier instead.
gsto_file_t *automaticFile(jibal_gsto *gsto, int z1, int z2) {
    if(gsto->overrides) {
        for(gsto_assignment *assignment = gsto->overrides; assignment->file; assignment++) {
            if(assignment->Z1 == z1 && assignment->Z2 == z2 && assignment->file->type == GSTO_STO_ELE) {
                return assignment->file;
            }
        }
    }
    for(size_t i = 0; i < gsto->n_files; i++) {
        gsto_file_t *file = &gsto->files[i];
        if(file->type == GSTO_STO_ELE && jibal_gsto_file_has_combination(file, z1, z2)) {
            return file;
        }
    }
    return nullptr;
}

bool hasData(const gsto_file_t *file, int z1, int z2) {
    return file->data && jibal_gsto_file_get_data(file, z1, z2);
}

// Assigns the file of the pair from the physics settings to it in JIBAL and loads its data when needed. The
// assignment lasts until another pair of settings assigns another file, so the stopping is read right after this.
bool assignStopping(int z1, int z2, const PhysicsSettings &physics, QString &errorMessage) {
    jibal_gsto *gsto = Elements::library()->gsto;
    const StoppingCorrection *correction = physics.correctionFor(z1, z2);
    const QString name = correction && !correction->stoppingFile.isEmpty() ? correction->stoppingFile : physics.stoppingFile;
    gsto_file_t *file = nullptr;
    if(name.isEmpty()) {
        file = automaticFile(gsto, z1, z2);
        if(!file) {
            errorMessage = QString("JIBAL has no stopping data for %1 in %2.").arg(Elements::symbol(z1), Elements::symbol(z2));
            return false;
        }
    } else {
        file = jibal_gsto_get_file(gsto, name.toUtf8().constData());
        if(!file || file->type != GSTO_STO_ELE) {
            errorMessage = QString("JIBAL has no electronic stopping file %1.").arg(name);
            return false;
        }
        if(!jibal_gsto_file_has_combination(file, z1, z2)) {
            errorMessage = QString("The stopping file %1 has no stopping of %2 in %3.").arg(name, Elements::symbol(z1), Elements::symbol(z2));
            return false;
        }
    }
    if(!jibal_gsto_assign(gsto, z1, z2, file)) {
        errorMessage = QString("JIBAL could not use the stopping file %1 for %2 in %3.")
                               .arg(QString::fromUtf8(file->name), Elements::symbol(z1), Elements::symbol(z2));
        return false;
    }
    if(!hasData(file, z1, z2)) {
        jibal_gsto_load(gsto, 0, file); // Reads the data of all pairs assigned to the file
    }
    if(!hasData(file, z1, z2)) {
        errorMessage = QString("JIBAL could not read the stopping of %1 in %2 from %3.")
                               .arg(Elements::symbol(z1), Elements::symbol(z2), QString::fromUtf8(file->name));
        return false;
    }
    return true;
}
}

QString stoppingNumerics() {
    return QString("Electronic stopping from the stopping files and universal nuclear stopping at the energies of the points "
                   "of the files, interpolated between them as the physics settings say. Slowing down by fourth-order "
                   "Runge-Kutta, losing at most %1 % of the energy in a step.")
            .arg(MaxRelativeLossPerStep * 100);
}

QStringList stoppingFileNames() {
    QStringList names;
    if(const jibal *library = Elements::library()) {
        for(size_t i = 0; i < library->gsto->n_files; i++) {
            const gsto_file_t &file = library->gsto->files[i];
            if(file.type == GSTO_STO_ELE) {
                names.append(QString::fromUtf8(file.name));
            }
        }
    }
    return names;
}

QStringList stoppingFileNames(int z1, int z2) {
    QStringList names;
    if(const jibal *library = Elements::library()) {
        for(size_t i = 0; i < library->gsto->n_files; i++) {
            gsto_file_t *file = &library->gsto->files[i];
            // With any target, the file must have the incident element
            const bool has = z2 == AnyElement ? file->Z1_min == JIBAL_ANY_Z || (z1 >= file->Z1_min && z1 <= file->Z1_max)
                                              : jibal_gsto_file_has_combination(file, z1, z2);
            if(file->type == GSTO_STO_ELE && has) {
                names.append(QString::fromUtf8(file->name));
            }
        }
    }
    return names;
}

namespace {
// Coefficients of the cubic spline through the points, four for each interval: the cubic in u from 0 to 1 over the
// interval. The spline has continuous first and second derivatives, and not-a-knot ends (the first two and the last
// two intervals are each one cubic). It can reach above the highest point near a maximum that falls between points, as
// the stopping does near its maximum. With fewer than four points it is the polynomial through them.
std::vector<double> splineCoefficients(const std::vector<double> &x, const std::vector<double> &y) {
    const size_t n = x.size();
    std::vector<double> coefficients;
    if(n < 2) {
        return coefficients;
    }
    std::vector<double> h(n - 1), d(n - 1);
    for(size_t k = 0; k + 1 < n; k++) {
        h[k] = x[k + 1] - x[k];
        d[k] = (y[k + 1] - y[k]) / h[k];
    }
    std::vector<double> m(n, 0); // Second derivatives at the points
    if(n == 3) { // The parabola through the points
        m[0] = m[1] = m[2] = 2 * (d[1] - d[0]) / (h[0] + h[1]);
    } else if(n >= 4) {
        // Equations of M1 ... M(n-2), with M0 and M(n-1) eliminated with the not-a-knot conditions
        const size_t k = n - 2;
        std::vector<double> lower(k), diagonal(k), upper(k), right(k);
        for(size_t r = 0; r < k; r++) {
            const size_t i = r + 1;
            lower[r] = h[i - 1];
            diagonal[r] = 2 * (h[i - 1] + h[i]);
            upper[r] = h[i];
            right[r] = 6 * (d[i] - d[i - 1]);
        }
        // M0 = ((h0 + h1) M1 - h0 M2) / h1
        diagonal[0] += lower[0] * (h[0] + h[1]) / h[1];
        upper[0] -= lower[0] * h[0] / h[1];
        // M(n-1) = ((h(n-2) + h(n-3)) M(n-2) - h(n-2) M(n-3)) / h(n-3)
        diagonal[k - 1] += upper[k - 1] * (h[n - 2] + h[n - 3]) / h[n - 3];
        lower[k - 1] -= upper[k - 1] * h[n - 2] / h[n - 3];
        // Tridiagonal elimination
        for(size_t r = 1; r < k; r++) {
            const double w = lower[r] / diagonal[r - 1];
            diagonal[r] -= w * upper[r - 1];
            right[r] -= w * right[r - 1];
        }
        m[k] = right[k - 1] / diagonal[k - 1];
        for(size_t r = k - 1; r-- > 0;) {
            m[r + 1] = (right[r] - upper[r] * m[r + 2]) / diagonal[r];
        }
        m[0] = ((h[0] + h[1]) * m[1] - h[0] * m[2]) / h[1];
        m[n - 1] = ((h[n - 2] + h[n - 3]) * m[n - 2] - h[n - 2] * m[n - 3]) / h[n - 3];
    }
    for(size_t i = 0; i + 1 < n; i++) {
        // Slopes at the ends of the interval, then the cubic as a Hermite polynomial in u
        const double left = d[i] - h[i] * (2 * m[i] + m[i + 1]) / 6;
        const double right = d[i] + h[i] * (m[i] + 2 * m[i + 1]) / 6;
        coefficients.insert(coefficients.end(), {y[i], h[i] * left, 3 * (y[i + 1] - y[i]) - h[i] * (2 * left + right),
                                                 2 * (y[i] - y[i + 1]) + h[i] * (left + right)});
    }
    return coefficients;
}

// The spacing of the points of a file in energy. A file evenly spaced on a logarithmic scale is so in energy whatever
// its unit, a linear scale only when the unit is proportional to energy (not velocity). It is checked from the energies.
bool evenlySpaced(const std::vector<double> &x) {
    const double step = (x.back() - x.front()) / static_cast<double>(x.size() - 1);
    for(size_t i = 0; i < x.size(); i++) {
        if(std::abs(x[i] - (x.front() + static_cast<double>(i) * step)) > 1e-9 * std::abs(step)) {
            return false;
        }
    }
    return step > 0;
}
}

StoppingTable::StoppingTable(int z, double mass, const QList<int> &elements, const PhysicsSettings &physics)
    : m_elements(elements), m_cubic(physics.interpolation == StoppingInterpolation::Cubic) {
    if(!Elements::library()) {
        m_errorMessage = QStringLiteral("JIBAL is not initialized.");
        return;
    }
    jibal_gsto *gsto = Elements::library()->gsto;
    m_maxEnergy = std::numeric_limits<double>::infinity();
    for(const int element : elements) {
        if(!assignStopping(z, element, physics, m_errorMessage)) {
            m_elementData.clear();
            m_grids.clear();
            return;
        }
        // The data of the file, copied so that other assignments of files in JIBAL do not change it. The file is
        // tabulated in energy per mass, in SI units.
        const gsto_file_t *file = jibal_gsto_get_assigned_file(gsto, GSTO_STO_ELE, z, element);
        auto grid = std::find_if(m_grids.begin(), m_grids.end(), [file](const Grid &g) { return g.file == file; });
        if(grid == m_grids.end()) {
            Grid &g = m_grids.emplace_back();
            g.file = file;
            for(size_t i = 0; i < file->xpoints; i++) {
                g.energies.push_back(file->em[i] * mass);
                g.logEnergies.push_back(std::log(g.energies.back()));
            }
            const size_t n = g.energies.size();
            if(n >= 2 && file->xscale == GSTO_XSCALE_LOG10 && evenlySpaced(g.logEnergies)) {
                g.spacing = Spacing::Logarithmic;
                g.invStep = static_cast<double>(n - 1) / (g.logEnergies.back() - g.logEnergies.front());
            } else if(n >= 2 && file->xscale == GSTO_XSCALE_LINEAR && file->xunit != GSTO_X_UNIT_M_S
                      && evenlySpaced(g.energies)) {
                g.spacing = Spacing::Linear;
                g.invStep = static_cast<double>(n - 1) / (g.energies.back() - g.energies.front());
            }
            grid = m_grids.end() - 1;
        }
        const StoppingCorrection *correction = physics.correctionFor(z, element);
        const double electronicFactor = physics.electronicFactor * (correction ? correction->electronicFactor : 1);
        ElementStopping &data = m_elementData.emplace_back();
        data.nuclearFactor = physics.nuclearFactor * (correction ? correction->nuclearFactor : 1);
        data.nuclear = NuclearStopping(z, mass, element, physics.nuclearMass == NuclearStoppingMass::Isotopes);
        const double *electronic = jibal_gsto_file_get_data(file, z, element);
        const double unit = file->stounit == GSTO_STO_UNIT_EV15CM2 ? C_EV_TFU : 1;
        for(size_t i = 0; i < grid->energies.size(); i++) {
            data.values.push_back(electronicFactor * electronic[i] * unit
                                  + data.nuclearFactor * data.nuclear.stopping(grid->energies[i]));
        }
        data.firstElectronic = electronicFactor * electronic[0] * unit;
        data.lastElectronic = electronicFactor * electronic[grid->energies.size() - 1] * unit;
        if(m_cubic) {
            data.coefficients = splineCoefficients(grid->logEnergies, data.values);
        }
        grid->elements.push_back(m_elementData.size() - 1);
        m_fileNames.append(QString::fromUtf8(file->name));
        m_minEnergy = std::max(m_minEnergy, grid->energies.front());
        if(grid->energies.back() < m_maxEnergy) {
            m_maxEnergy = grid->energies.back();
            m_maxEnergyElement = static_cast<qsizetype>(m_elementData.size()) - 1;
        }
    }
}

StoppingTable::Location StoppingTable::Grid::locate(double energy, bool cubic) const {
    const size_t n = energies.size();
    if(n < 2 || energy < energies.front()) {
        return {-1, 0, 0, 0};
    }
    if(energy > energies.back()) {
        return {1, n - 2, 1, 1};
    }
    const double logEnergy = cubic || spacing == Spacing::Logarithmic ? std::log(energy) : 0;
    size_t i;
    if(spacing == Spacing::Arbitrary) {
        i = static_cast<size_t>(std::upper_bound(energies.begin(), energies.end(), energy) - energies.begin());
        i = i > 0 ? i - 1 : 0;
    } else {
        const double position = spacing == Spacing::Logarithmic ? (logEnergy - logEnergies.front()) * invStep
                                                                : (energy - energies.front()) * invStep;
        i = static_cast<size_t>(std::max(0.0, position));
    }
    // Calculated positions are corrected for rounding, so that energies[i] <= energy < energies[i + 1]
    i = std::min(i, n - 2);
    while(i > 0 && energies[i] > energy) {
        i--;
    }
    while(i + 2 < n && energies[i + 1] <= energy) {
        i++;
    }
    const double t = (energy - energies[i]) / (energies[i + 1] - energies[i]);
    const double u = cubic ? (logEnergy - logEnergies[i]) / (logEnergies[i + 1] - logEnergies[i]) : 0;
    return {0, i, t, u};
}

double StoppingTable::stopping(std::span<const double> fractions, double energy) const {
    double sum = 0;
    for(const Grid &grid : m_grids) {
        const Location at = grid.locate(energy, m_cubic);
        for(const size_t e : grid.elements) {
            if(e >= fractions.size() || !(fractions[e] > 0)) {
                continue;
            }
            const ElementStopping &data = m_elementData[e];
            double value;
            if(at.region == 0) {
                if(m_cubic) {
                    const double *c = data.coefficients.data() + 4 * at.i;
                    value = c[0] + at.u * (c[1] + at.u * (c[2] + at.u * c[3]));
                } else {
                    value = data.values[at.i] + at.t * (data.values[at.i + 1] - data.values[at.i]);
                }
            } else {
                // Outside the data: electronic stopping proportional to the velocity below it, the last value above
                const double electronic = at.region < 0 ? data.firstElectronic * std::sqrt(std::max(0.0, energy) / grid.energies.front())
                                                        : data.lastElectronic;
                value = electronic + data.nuclearFactor * data.nuclear.stopping(energy);
            }
            sum += fractions[e] * value;
        }
    }
    return sum;
}

double slowDown(const StoppingTable &table, std::span<const double> fractions, double energy, double pathLength,
                int maxSteps) {
    // Runge-Kutta, with steps short enough that the particle loses at most a few per cent of its energy in each
    if(!(energy > 0)) {
        return 0;
    }
    const double initialLoss = table.stopping(fractions, energy) * pathLength;
    const int steps = std::clamp(static_cast<int>(std::ceil(initialLoss / (MaxRelativeLossPerStep * energy))), 1, maxSteps);
    const double h = pathLength / steps;
    double e = energy;
    for(int i = 0; i < steps; i++) {
        const double k1 = -table.stopping(fractions, e);
        const double e2 = e + h / 2 * k1;
        if(!(e2 > 0)) {
            return 0;
        }
        const double k2 = -table.stopping(fractions, e2);
        const double e3 = e + h / 2 * k2;
        if(!(e3 > 0)) {
            return 0;
        }
        const double k3 = -table.stopping(fractions, e3);
        const double e4 = e + h * k3;
        if(!(e4 > 0)) {
            return 0;
        }
        const double k4 = -table.stopping(fractions, e4);
        e += h / 6 * (k1 + 2 * k2 + 2 * k3 + k4);
        if(!(e > 0)) {
            return 0;
        }
    }
    return e;
}

FoilEnergyLoss::FoilEnergyLoss(int z, double mass, const Material &material, double thickness,
                               const PhysicsSettings &physics) {
    if(thickness <= 0) {
        return;
    }
    if(!isValidMaterial(material)) {
        m_errorMessage = QStringLiteral("The concentrations of the foil material do not sum up to 100 %.");
        return;
    }
    QList<int> elements;
    for(const MaterialElement &element : material.elements) {
        elements.append(element.z);
        m_fractions.push_back(element.fraction);
    }
    m_table = StoppingTable(z, mass, elements, physics);
    if(!m_table.isValid()) {
        m_errorMessage = m_table.errorMessage();
        return;
    }
    m_pathLength = thickness / averageAtomMass(material); // Atoms per m²
    m_loss = true;
}

double FoilEnergyLoss::energyAfter(double energy) const {
    return m_loss ? slowDown(m_table, m_fractions, energy, m_pathLength) : energy;
}

void FoilEnergyLoss::tabulate(double maxEnergyAfter) {
    m_before.clear();
    m_after.clear();
    m_limited = false;
    if(!m_loss) {
        return;
    }
    // The table reaches energies before the foil that give at least the highest energy after it, but not beyond the
    // stopping data
    const double minEnergy = std::max(FoilTableMinEnergy, m_table.minEnergy());
    const double target = std::max(maxEnergyAfter, 2 * minEnergy) * 1.05;
    double maxEnergy = 1.1 * target + 100 * Units::Kiloelectronvolt;
    for(int i = 0; i < MaxFoilTableExtensions && maxEnergy < m_table.maxEnergy() && energyAfter(maxEnergy) < target; i++) {
        maxEnergy *= 1.5;
    }
    if(maxEnergy >= m_table.maxEnergy()) {
        maxEnergy = m_table.maxEnergy();
        m_limited = energyAfter(maxEnergy) < target;
    }
    if(maxEnergy <= minEnergy) {
        m_limited = true;
        return; // No table, every energy gives NaN
    }
    // Evenly spaced on a logarithmic scale
    const double ratio = std::pow(maxEnergy / minEnergy, 1.0 / (FoilTablePoints - 1));
    double energy = minEnergy;
    for(int i = 0; i < FoilTablePoints; i++, energy *= ratio) {
        m_before.push_back(energy);
        // Never decreasing, so that the table can be searched
        m_after.push_back(std::max(energyAfter(energy), m_after.empty() ? 0.0 : m_after.back()));
    }
}

double FoilEnergyLoss::energyBefore(double energyAfter) const {
    if(!m_loss) {
        return energyAfter;
    }
    if(m_after.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if(energyAfter > m_after.back()) {
        if(m_limited) {
            return std::numeric_limits<double>::quiet_NaN(); // Before the foil above the stopping data
        }
        return energyAfter + (m_before.back() - m_after.back());
    }
    // The last point at or below the energy, the next one is above it
    const auto above = std::upper_bound(m_after.begin(), m_after.end(), energyAfter);
    if(above == m_after.begin()) {
        return m_before.front();
    }
    const auto i = static_cast<size_t>(above - m_after.begin()) - 1;
    const double t = (energyAfter - m_after[i]) / (m_after[i + 1] - m_after[i]);
    return m_before[i] + t * (m_before[i + 1] - m_before[i]);
}
