/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#include "depthprofile.h"
#include "anglecalibration.h"
#include "defaults.h"
#include "elements.h"
#include "energyspectrum.h"
#include "split.h"
#include "stopping.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>

#include <QStringList>

extern "C" {
#include <jibal.h>
#include <jibal_cross_section.h>
}

namespace {
constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
constexpr double MinEfficiency = 1e-6;
constexpr size_t MaxStoppingBins = 20000;
constexpr size_t MaxProfileBins = 1000000;
constexpr double AutoDepthMargin = 0.1; // Of the deepest event, left after it with an automatic maximum depth
constexpr int AutoDepthMarginBins = 5; // Stopping bins also left after it
constexpr double AutoDepthCutLevel = 0.8; // The bins are cut when fewer than this fraction of them are needed
constexpr int MaxBinsAboveSurface = 100; // Events higher above the surface are left out of the profile
constexpr int GridPoints = 250; // Of the energy grid of the exit energy tables, evenly spaced on a logarithmic scale
constexpr double GridRange = 100; // Highest energy of the grid divided by the lowest
constexpr int MaxSteps = 50; // Of slowing down, particles losing more stop or nearly, the exact energy hardly matters
constexpr int AutoNormalizationSmoothing = 2; // Bins on each side in the moving average of the total
constexpr double AutoNormalizationTolerance = 0.1; // Relative difference of the total from its typical level allowed
constexpr double AutoNormalizationDeviations = 3; // Statistical deviations allowed, when more than the above

jibal_isotope isotope(int z, int a, double mass) {
    jibal_isotope result{};
    result.Z = z;
    result.A = a;
    result.mass = mass;
    return result;
}
}

DepthProfileInputs depthProfileInputs(const Measurement &measurement, const DepthProfile &profile) {
    DepthProfileInputs inputs;
    if(const ExperimentalSettings *settings = measurement.settings) {
        inputs.hasSettings = true;
        inputs.beamZ = settings->beamZ;
        inputs.beamA = settings->beamA;
        inputs.beamEnergy = settings->beamEnergy;
        inputs.incidentAngle = settings->incidentAngle;
    }
    if(const DetectorSettings *detector = measurement.detector) {
        inputs.hasDetector = true;
        inputs.scatteringAngle = detector->scatteringAngle;
        inputs.tofLength = detector->tofLength;
        inputs.tofSlope = detector->tofSlope;
        inputs.tofOffset = detector->tofOffset;
        inputs.foilMaterial = detector->foilMaterial;
        inputs.foilThickness = detector->foilThickness;
        inputs.efficiencies = detector->efficiencies;
        if(profile.kinematicCorrection && detector->hasAngleCalibration()) {
            inputs.kinematicCorrection = true;
            inputs.angleCalibration = detector->angleCalibration;
            inputs.detectorDistance = detector->detectorDistance;
            inputs.windowWidth = detector->windowWidth;
        }
    }
    inputs.physics = measurement.physicsOrDefault();
    inputs.physics.name.clear();
    inputs.stoppingBinWidth = profile.stoppingBinWidth;
    inputs.maxDepth = profile.maxDepth;
    inputs.autoMaxDepth = profile.autoMaxDepth;
    inputs.minStoppingBinEvents = profile.minStoppingBinEvents;
    inputs.referencePolygon = measurement.referencePolygon();
    inputs.rangeMin = profile.rangeMin;
    inputs.rangeMax = profile.rangeMax;
    for(const Cut &cut : measurement.cuts) {
        if(profile.cutIds.contains(cut.id) && isAnalyzed(cut)) {
            // A part of the profile's range for some selections
            const auto [min, max] = profile.cutRanges.value(cut.id, {0.0, 1.0});
            const double width = profile.rangeMax - profile.rangeMin;
            inputs.cuts.append({cut.id, cut.z, cut.a, cut.type, cut.polygon, cut.representsElement,
                                abundanceCorrection(cut), cut.weight, profile.rangeMin + min * width,
                                profile.rangeMin + max * width});
        }
    }
    return inputs;
}

QString elementOverlapWarning(const DepthProfileInputs &inputs) {
    QStringList warnings;
    for(qsizetype i = 0; i < inputs.cuts.size(); i++) {
        const auto &first = inputs.cuts[i];
        auto sameElement = [&first](const DepthProfileInputs::CutInput &cut) {
            return cut.z == first.z && cut.type == first.type;
        };
        if(std::any_of(inputs.cuts.begin(), inputs.cuts.begin() + i, sameElement)) {
            continue; // Already checked with the first selection of the element
        }
        QStringList representing; // Selections standing for the whole element
        qsizetype count = 0;
        for(qsizetype j = i; j < inputs.cuts.size(); j++) {
            const auto &cut = inputs.cuts[j];
            if(!sameElement(cut)) {
                continue;
            }
            count++;
            if(cut.a == 0) {
                representing.append(QStringLiteral("the selection of the element"));
            } else if(cut.representsElement) {
                representing.append(cutLabel(cut.z, cut.a, cut.type) + QStringLiteral(" representing the element"));
            }
        }
        if(count > 1 && !representing.isEmpty()) {
            warnings.append(QString("%1 is counted more than once: %2 already includes all of its isotopes, but other "
                                    "selections of it are included as well.")
                                    .arg(cutLabel(first.z, 0, first.type), representing.join(QStringLiteral(" and "))));
        }
    }
    return warnings.join(QLatin1Char(' '));
}

namespace {
// Why the stopping of a particle is not known at an energy above the range of its stopping data
QString stoppingRangeError(const StoppingTable &table, int z, double energy, const char *what) {
    const qsizetype element = table.maxEnergyElement();
    return QString("The stopping of the %1 %2 in %3 is known up to %4 MeV (%5), but energies up to %6 MeV are needed.")
            .arg(QString::fromLatin1(what), Elements::symbol(z), Elements::symbol(table.elements().at(element)))
            .arg(table.maxEnergy() / Units::Megaelectronvolt, 0, 'g', 4)
            .arg(table.fileNames().at(element))
            .arg(energy / Units::Megaelectronvolt, 0, 'g', 4);
}
}

DepthProfileCalculation::DepthProfileCalculation(const std::vector<ToFEEvent> &events, const DepthProfileInputs &inputs)
    : m_inputs(inputs), m_lastChange(NaN), m_measurementEvents(events.size()) {
    if(!inputs.hasSettings || inputs.beamZ <= 0 || inputs.beamEnergy <= 0) {
        m_errorMessage = QStringLiteral("The experimental settings of the measurement with the incident ion and the beam "
                                        "energy are needed.");
        return;
    }
    if(!inputs.hasDetector || inputs.tofLength <= 0 || inputs.tofSlope <= 0) {
        m_errorMessage = QStringLiteral("The detector settings of the measurement with the ToF length and calibration are "
                                        "needed.");
        return;
    }
    const double exitAngle = inputs.scatteringAngle - inputs.incidentAngle;
    if(inputs.incidentAngle <= 0 || exitAngle <= 0) {
        m_errorMessage = QStringLiteral("The incident and exit angles must be above the sample surface.");
        return;
    }
    if(inputs.cuts.isEmpty()) {
        m_errorMessage = QStringLiteral("Include some selections in the depth profile.");
        return;
    }
    if(inputs.stoppingBinWidth <= 0 || inputs.maxDepth <= 0) {
        m_errorMessage = QStringLiteral("The stopping bin width and the maximum depth must be positive.");
        return;
    }
    m_binWidth = inputs.stoppingBinWidth;
    m_bins = static_cast<size_t>(std::ceil(inputs.maxDepth / m_binWidth - 1e-9));
    if(m_bins > MaxStoppingBins) {
        m_errorMessage = QString("There would be more than %1 stopping bins, make them wider.").arg(MaxStoppingBins);
        return;
    }
    m_beamMass = Elements::mass(inputs.beamZ, inputs.beamA);
    if(m_beamMass <= 0) {
        m_errorMessage = QString("The mass of %1 is not known.").arg(Elements::label(inputs.beamZ, inputs.beamA));
        return;
    }
    m_beam.z = inputs.beamZ;
    m_beam.mass = m_beamMass;

    // Particles leaving the sample: recoils, and the incident ions for scattered ions
    const double angle = inputs.scatteringAngle;
    for(const auto &cut : inputs.cuts) {
        if(!m_elements.contains(cut.z)) {
            m_elements.append(cut.z);
        }
        const double targetMass = Elements::mass(cut.z, cut.a);
        if(targetMass <= 0) {
            m_errorMessage = QString("The mass of %1 is not known.").arg(Elements::label(cut.z, cut.a));
            return;
        }
        const bool scattered = cut.type == CutType::Scattered;
        const int z = scattered ? inputs.beamZ : cut.z;
        const double mass = scattered ? m_beamMass : targetMass;
        SelectionPhysics physics{};
        physics.targetMass = targetMass;
        physics.kinematicFactor = kinematicFactor(cut.type, m_beamMass, targetMass, angle);
        if(std::isnan(physics.kinematicFactor)) {
            m_errorMessage = scattered ? QString("%1 cannot scatter from %2 to the detector angle.")
                                                 .arg(Elements::label(inputs.beamZ, inputs.beamA), Elements::label(cut.z, cut.a))
                                       : QStringLiteral("Recoils cannot reach a recoil angle of 90° or more.");
            return;
        }
        auto same = [z, mass](const Particle &particle) { return particle.z == z && particle.mass == mass; };
        auto found = std::find_if(m_particles.begin(), m_particles.end(), same);
        if(found == m_particles.end()) {
            Particle particle;
            particle.z = z;
            particle.mass = mass;
            m_particles.push_back(particle);
            found = m_particles.end() - 1;
        }
        physics.particle = static_cast<size_t>(found - m_particles.begin());
        m_physics.push_back(physics);
    }
    m_beam.stopping = StoppingTable(m_beam.z, m_beam.mass, m_elements, inputs.physics);
    if(!m_beam.stopping.isValid()) {
        m_errorMessage = m_beam.stopping.errorMessage();
        return;
    }
    if(inputs.beamEnergy > m_beam.stopping.maxEnergy()) {
        m_errorMessage = stoppingRangeError(m_beam.stopping, m_beam.z, inputs.beamEnergy, "beam");
        return;
    }
    for(Particle &particle : m_particles) {
        particle.stopping = StoppingTable(particle.z, particle.mass, m_elements, inputs.physics);
        if(!particle.stopping.isValid()) {
            m_errorMessage = particle.stopping.errorMessage();
            return;
        }
    }

    // Energy grids of the exit energy tables, up to the highest energy the particles can get
    for(size_t i = 0; i < m_particles.size(); i++) {
        double maxEnergy = 0;
        for(size_t s = 0; s < m_physics.size(); s++) {
            if(m_physics[s].particle == i) {
                maxEnergy = std::max(maxEnergy, m_physics[s].kinematicFactor * inputs.beamEnergy);
            }
        }
        Particle &particle = m_particles[i];
        if(maxEnergy > particle.stopping.maxEnergy()) {
            m_errorMessage = stoppingRangeError(particle.stopping, particle.z, maxEnergy, "detected");
            return;
        }
        maxEnergy *= 1.01;
        const double ratio = std::pow(GridRange, 1.0 / (GridPoints - 1));
        double energy = maxEnergy / GridRange;
        for(int k = 0; k < GridPoints; k++, energy *= ratio) {
            particle.grid.push_back(energy);
        }
    }

    // Detected energies and efficiencies of the events. The time of flight is spread within its channel, as in energy
    // spectra.
    const EventDivision division(events, inputs.referencePolygon);
    m_rangeEvents = division.range(inputs.rangeMin, inputs.rangeMax).size();
    const size_t referenceCounts = division.referenceCountsIn(inputs.rangeMin, inputs.rangeMax);
    for(qsizetype s = 0; s < inputs.cuts.size(); s++) {
        const auto &cut = inputs.cuts[s];
        const Particle &particle = m_particles[m_physics[static_cast<size_t>(s)].particle];
        Selection selection;
        selection.cutId = cut.id;
        selection.z = cut.z;
        selection.a = cut.a;
        selection.type = cut.type;
        selection.representsElement = cut.representsElement;
        selection.userWeight = cut.weight;
        selection.abundanceWeight = cut.abundanceCorrection;
        // An isotope representing its element stands for all of its isotopes
        if(!std::isfinite(cut.abundanceCorrection) || cut.abundanceCorrection <= 0) {
            m_errorMessage = QString("%1 does not occur naturally, it cannot represent the element.")
                                     .arg(Elements::label(cut.z, cut.a));
            return;
        }
        // The selection's part of the measurement, standing for the profile's range
        const std::span<const ToFEEvent> range = division.range(cut.rangeMin, cut.rangeMax);
        const size_t cutReferenceCounts = division.referenceCountsIn(cut.rangeMin, cut.rangeMax);
        if(cutReferenceCounts == 0) {
            m_errorMessage = QString("There are no reference counts in the part of the measurement of %1.")
                                     .arg(cutLabel(cut.z, cut.a, cut.type, false));
            return;
        }
        selection.rangeWeight = static_cast<double>(referenceCounts) / static_cast<double>(cutReferenceCounts);
        std::mt19937 random(static_cast<std::mt19937::result_type>(cut.id));
        std::uniform_real_distribution<double> dither(-0.5, 0.5);
        // The timing smoothing of the angle calibration, with a generator of its own so that the ToF is spread the same
        // with and without the correction
        std::mt19937 timingRandom(static_cast<std::mt19937::result_type>(cut.id));
        const double smoothing = inputs.angleCalibration.timingSmoothing;
        std::uniform_real_distribution<double> timingSpread(-smoothing, smoothing);
        std::vector<double> energies;
        std::vector<double> corrections; // Of the energies, with the kinematic correction
        double maxEnergy = 0;
        const SelectionPhysics &selectionPhysics = m_physics[static_cast<size_t>(s)];
        forEachEventIn(range, cut.polygon, [&](const ToFEEvent &event) {
            selection.events++;
            const double energy = tofToEnergy(event.tof + dither(random), particle.mass, inputs.tofLength,
                                              inputs.tofSlope, inputs.tofOffset);
            if(std::isnan(energy)) {
                selection.lowEfficiencyEvents++; // No energy, as if not detected
                return;
            }
            energies.push_back(energy);
            maxEnergy = std::max(maxEnergy, energy);
            if(inputs.kinematicCorrection) {
                ToFEEvent smoothed = event;
                if(smoothing > 0) {
                    smoothed.timing += timingSpread(timingRandom);
                }
                const double offset = angleOffset(inputs.angleCalibration, inputs.detectorDistance, inputs.windowWidth, smoothed);
                const double k = kinematicFactor(cut.type, m_beamMass, selectionPhysics.targetMass, angle + offset);
                corrections.push_back(k > 0 ? selectionPhysics.kinematicFactor / k : 1);
            }
        });
        const bool scattered = cut.type == CutType::Scattered;
        const EfficiencyCurve *curve = findEfficiency(inputs.efficiencies, scattered ? inputs.beamZ : cut.z,
                                                      scattered ? inputs.beamA : cut.a);
        FoilEnergyLoss foil(particle.z, particle.mass, inputs.foilMaterial, inputs.foilThickness, inputs.physics);
        if(!foil.isValid()) {
            m_errorMessage = QString("Energy loss of %1 in the timing detector foil: %2")
                                     .arg(cutLabel(cut.z, cut.a, cut.type, false), foil.errorMessage());
            return;
        }
        foil.tabulate(maxEnergy);
        selection.minEnergy = particle.stopping.minEnergy();
        selection.maxEnergy = particle.stopping.maxEnergy();
        selection.foilMinEnergy = foil.minEnergy();
        selection.foilMaxEnergy = foil.maxEnergy();
        for(size_t i = 0; i < energies.size(); i++) {
            // The particle has its lowest energies after the foil and, in the sample, at the surface. The foil
            // correction is NaN above the stopping data of the foil.
            if(energies[i] < foil.minEnergy()) {
                selection.outsideStoppingEvents++;
                continue;
            }
            const double energy = foil.energyBefore(energies[i]);
            if(std::isnan(energy) || energy < particle.stopping.minEnergy() || energy > particle.stopping.maxEnergy()) {
                selection.outsideStoppingEvents++;
                continue;
            }
            const double eff = curve ? efficiency(*curve, energy) : 1;
            if(eff < MinEfficiency) {
                selection.lowEfficiencyEvents++;
                continue;
            }
            selection.energies.push_back(corrections.empty() ? energy : energy * corrections[i]);
            selection.efficiencyWeights.push_back(cut.weight * selection.rangeWeight * cut.abundanceCorrection / eff);
        }
        selection.depths.assign(selection.energies.size(), 0);
        selection.weights.assign(selection.energies.size(), 0);
        m_selections.push_back(std::move(selection));
    }
    // With n_s reference counts in the part of selection s, n_st in the overlap of two parts and R in the range,
    // cov(n_s / R, n_t / R) = (n_st - n_s n_t / R) / R², so the relative covariance of R / n_s and R / n_t is
    // n_st / (n_s n_t) - 1 / R
    for(qsizetype s = 0; s < inputs.cuts.size(); s++) {
        const auto &cut = inputs.cuts[s];
        const auto counts = static_cast<double>(division.referenceCountsIn(cut.rangeMin, cut.rangeMax));
        for(const auto &other : inputs.cuts) {
            const double overlapMin = std::max(cut.rangeMin, other.rangeMin);
            const double overlapMax = std::min(cut.rangeMax, other.rangeMax);
            const auto overlap = overlapMax > overlapMin ? static_cast<double>(division.referenceCountsIn(overlapMin, overlapMax)) : 0.0;
            const auto otherCounts = static_cast<double>(division.referenceCountsIn(other.rangeMin, other.rangeMax));
            m_selections[static_cast<size_t>(s)].rangeCovariances.push_back(overlap / (counts * otherCounts)
                                                                            - 1 / static_cast<double>(referenceCounts));
        }
    }
    initialComposition();
}

double DepthProfileCalculation::crossSection(const Selection &selection, double beamEnergy) const {
    const SelectionPhysics &physics = m_physics[static_cast<size_t>(&selection - m_selections.data())];
    const jibal_isotope beam = isotope(m_inputs.beamZ, m_inputs.beamA, m_beamMass);
    const jibal_isotope target = isotope(selection.z, selection.a, physics.targetMass);
    const jibal_cross_section_type type = m_inputs.physics.screening == ScreeningModel::Andersen ? JIBAL_CS_ANDERSEN
                                                                                                : JIBAL_CS_RUTHERFORD;
    if(selection.type == CutType::Scattered) {
        return jibal_cross_section_rbs(&beam, &target, m_inputs.scatteringAngle, beamEnergy, type);
    }
    return jibal_cross_section_erd(&beam, &target, m_inputs.scatteringAngle, beamEnergy, type);
}

void DepthProfileCalculation::initialComposition() {
    // The same at all depths, from the weights of all events with cross sections at the beam energy
    std::vector<double> weights(static_cast<size_t>(m_elements.size()), 0);
    for(const Selection &selection : m_selections) {
        const double sigma = crossSection(selection, m_inputs.beamEnergy);
        double sum = 0;
        for(const double weight : selection.efficiencyWeights) {
            sum += weight;
        }
        weights[static_cast<size_t>(m_elements.indexOf(selection.z))] += sum / sigma;
    }
    double total = 0;
    for(const double weight : weights) {
        total += weight;
    }
    if(total <= 0) {
        m_errorMessage = QStringLiteral("The selections have no events.");
        return;
    }
    for(double &weight : weights) {
        weight /= total;
    }
    m_composition.assign(m_bins, weights);
}

double DepthProfileCalculation::traverse(const Particle &particle, size_t bin, double energy, double pathLength) const {
    return slowDown(particle.stopping, m_composition[bin], energy, pathLength, MaxSteps);
}

double DepthProfileCalculation::exitEnergy(const Particle &particle, size_t boundary, double energy) const {
    // Cubic interpolation in the logarithm of the energy, the grid is evenly spaced in it
    const std::vector<double> &grid = particle.grid;
    const std::vector<double> &table = particle.exitEnergies[boundary];
    if(!(energy > 0)) {
        return 0;
    }
    if(energy <= grid.front()) {
        return table.front() * energy / grid.front(); // Slow particles, hardly matters
    }
    const double position = std::log(energy / grid.front()) / std::log(grid[1] / grid.front());
    const auto last = static_cast<int>(grid.size()) - 1;
    if(position >= last) {
        return table.back() + (energy - grid.back()) * (table[last] - table[last - 1]) / (grid[last] - grid[last - 1]);
    }
    const int i = std::clamp(static_cast<int>(std::floor(position)) - 1, 0, last - 3);
    const double t = position - i;
    // Lagrange polynomial through the points i ... i + 3, t from 0 to 3
    const double y0 = table[i], y1 = table[i + 1], y2 = table[i + 2], y3 = table[i + 3];
    return y0 * (t - 1) * (t - 2) * (t - 3) / -6 + y1 * t * (t - 2) * (t - 3) / 2 + y2 * t * (t - 1) * (t - 3) / -2
           + y3 * t * (t - 1) * (t - 2) / 6;
}

void DepthProfileCalculation::calculateBeamEnergies() {
    const double path = m_binWidth / std::sin(m_inputs.incidentAngle);
    m_beamEnergies.assign(m_bins + 1, 0);
    m_beamEnergies[0] = m_inputs.beamEnergy;
    for(size_t j = 0; j < m_bins; j++) {
        m_beamEnergies[j + 1] = traverse(m_beam, j, m_beamEnergies[j], path);
    }
}

void DepthProfileCalculation::calculateExitEnergies(Particle &particle) {
    // Energy at the surface of a particle that starts from a boundary with a grid energy. From the boundary j + 1 the
    // particle crosses the bin j to the boundary j, then continues from there as tabulated for it.
    const double path = m_binWidth / std::sin(m_inputs.scatteringAngle - m_inputs.incidentAngle);
    particle.exitEnergies.assign(m_bins + 1, {});
    particle.exitEnergies[0] = particle.grid;
    for(size_t j = 0; j < m_bins; j++) {
        std::vector<double> &next = particle.exitEnergies[j + 1];
        next.resize(particle.grid.size());
        for(size_t k = 0; k < particle.grid.size(); k++) {
            next[k] = exitEnergy(particle, j, traverse(particle, j, particle.grid[k], path));
        }
    }
}

double DepthProfileCalculation::beamEnergyAt(double depth) const {
    if(depth <= 0) {
        return m_beamEnergies.front();
    }
    const double position = depth / m_binWidth;
    const auto j = static_cast<size_t>(position);
    if(j >= m_bins) {
        return m_beamEnergies.back();
    }
    const double t = position - static_cast<double>(j);
    return m_beamEnergies[j] + t * (m_beamEnergies[j + 1] - m_beamEnergies[j]);
}

void DepthProfileCalculation::iterate() {
    if(!isValid()) {
        return;
    }
    calculateBeamEnergies();
    for(Particle &particle : m_particles) {
        calculateExitEnergies(particle);
    }
    const size_t bins = m_bins;

    // Depths and weights of the events. Each event is shared between the two stopping bins with the nearest centres,
    // in proportion to its distance from them, so that the composition changes smoothly when events move.
    const size_t elementCount = static_cast<size_t>(m_elements.size());
    std::vector<std::vector<double>> binWeights(m_bins, std::vector<double>(elementCount, 0));
    std::vector<double> binCounts(m_bins, 0);
    std::vector<double> allWeights(elementCount, 0);
    for(size_t s = 0; s < m_selections.size(); s++) {
        Selection &selection = m_selections[s];
        const SelectionPhysics &physics = m_physics[s];
        const Particle &particle = m_particles[physics.particle];
        // Detected energy of particles from each boundary, decreasing with depth
        std::vector<double> detected(m_bins + 1);
        for(size_t j = 0; j <= m_bins; j++) {
            detected[j] = exitEnergy(particle, j, physics.kinematicFactor * m_beamEnergies[j]);
            if(j > 0) {
                detected[j] = std::min(detected[j], detected[j - 1]);
            }
        }
        const auto element = static_cast<size_t>(m_elements.indexOf(selection.z));
        selection.tooDeepEvents = 0;
        selection.lowBeamEnergyEvents = 0;
        for(size_t i = 0; i < selection.energies.size(); i++) {
            const double energy = selection.energies[i];
            double depth;
            if(energy <= detected[m_bins] || detected[0] <= 0) {
                selection.depths[i] = NaN;
                selection.weights[i] = 0;
                selection.tooDeepEvents++;
                continue;
            }
            if(energy >= detected[0]) {
                // Above the surface, from the depth resolution. The slope of the first bin is extended.
                const double slope = (detected[0] - detected[1]) / m_binWidth;
                depth = slope > 0 ? -(energy - detected[0]) / slope : 0;
            } else {
                // The first boundary below the energy, the one before it is above or at it
                const auto below = std::upper_bound(detected.begin(), detected.end(), energy, std::greater<>());
                const auto j = static_cast<size_t>(below - detected.begin()) - 1;
                const double drop = detected[j] - detected[j + 1];
                depth = (static_cast<double>(j) + (drop > 0 ? (detected[j] - energy) / drop : 0)) * m_binWidth;
            }
            const double beamEnergy = beamEnergyAt(depth);
            if(beamEnergy < m_beam.stopping.minEnergy()) {
                selection.depths[i] = NaN;
                selection.weights[i] = 0;
                selection.lowBeamEnergyEvents++;
                continue;
            }
            const double sigma = crossSection(selection, beamEnergy);
            const double weight = sigma > 0 ? selection.efficiencyWeights[i] / sigma : 0;
            selection.depths[i] = depth;
            selection.weights[i] = weight;
            const double centres = std::clamp(depth / m_binWidth - 0.5, 0.0, static_cast<double>(m_bins - 1));
            const auto lower = static_cast<size_t>(centres);
            const double upperShare = centres - static_cast<double>(lower);
            binWeights[lower][element] += (1 - upperShare) * weight;
            binCounts[lower] += 1 - upperShare;
            if(upperShare > 0) {
                binWeights[lower + 1][element] += upperShare * weight;
                binCounts[lower + 1] += upperShare;
            }
            allWeights[element] += weight;
        }
    }

    // Composition of the next iteration. Each bin gets the minimum number of events as if they had the composition of
    // the bin before it, or of all events for the first bin. Bins with few events follow the bins before them, bins
    // with many are hardly affected.
    double allTotal = 0;
    for(const double weight : allWeights) {
        allTotal += weight;
    }
    if(allTotal <= 0) {
        m_lastChange = 0; // No events at any depth, the composition stays
        recordIteration(bins);
        m_iterations++;
        return;
    }
    std::vector<double> previous(elementCount);
    for(size_t e = 0; e < elementCount; e++) {
        previous[e] = allWeights[e] / allTotal;
    }
    const double pseudoEvents = std::max(1, m_inputs.minStoppingBinEvents);
    std::vector<std::vector<double>> composition(m_bins, std::vector<double>(elementCount, 0));
    double change = 0;
    for(size_t j = 0; j < m_bins; j++) {
        double total = 0;
        for(const double weight : binWeights[j]) {
            total += weight;
        }
        for(size_t e = 0; e < elementCount; e++) {
            const double own = total > 0 ? binWeights[j][e] / total : 0;
            composition[j][e] = (binCounts[j] * own + pseudoEvents * previous[e]) / (binCounts[j] + pseudoEvents);
            change = std::max(change, std::abs(composition[j][e] - m_composition[j][e]));
        }
        previous = composition[j];
    }
    m_lastChange = change;
    m_composition = std::move(composition);
    if(m_inputs.autoMaxDepth && adjustMaxDepth()) {
        m_lastChange = NaN; // Not converged, the next iteration uses other bins
    }
    recordIteration(bins);
    m_iterations++;
}

std::vector<DepthProfileCalculation::ParticleStopping> DepthProfileCalculation::particleStopping() const {
    std::vector<ParticleStopping> result{{m_beam.z, m_beam.mass, true, &m_beam.stopping}};
    for(const Particle &particle : m_particles) {
        result.push_back({particle.z, particle.mass, false, &particle.stopping});
    }
    return result;
}

void DepthProfileCalculation::recordIteration(size_t bins) {
    IterationStatistics statistics{m_lastChange, bins, m_beamEnergies.back(), 0, 0, 0};
    for(const Selection &selection : m_selections) {
        for(const double depth : selection.depths) {
            statistics.events += std::isnan(depth) ? 0 : 1;
        }
        statistics.tooDeepEvents += selection.tooDeepEvents;
        statistics.lowBeamEnergyEvents += selection.lowBeamEnergyEvents;
    }
    m_history.push_back(statistics);
}

bool DepthProfileCalculation::adjustMaxDepth() {
    size_t tooDeep = 0;
    double deepest = 0;
    for(const Selection &selection : m_selections) {
        tooDeep += selection.tooDeepEvents;
        for(const double depth : selection.depths) {
            if(!std::isnan(depth)) {
                deepest = std::max(deepest, depth);
            }
        }
    }
    size_t bins = m_bins;
    if(tooDeep > 0 && m_beamEnergies.back() > m_beam.stopping.minEnergy()) {
        // Events come from deeper, as far as the beam reaches with known stopping
        bins = std::min(MaxStoppingBins, m_bins + std::max<size_t>(1, m_bins / 2));
    } else if(tooDeep == 0) {
        // Some room after the deepest events, the bins are cut when much more is left
        const double needed = deepest * (1 + AutoDepthMargin) + AutoDepthMarginBins * m_binWidth;
        const auto neededBins = static_cast<size_t>(std::ceil(needed / m_binWidth));
        if(neededBins < m_bins * AutoDepthCutLevel) {
            bins = std::max<size_t>(1, neededBins);
        }
    }
    if(bins == m_bins) {
        return false;
    }
    m_composition.resize(bins, m_composition.back()); // New bins continue the composition of the last one
    m_bins = bins;
    return true;
}

double DepthProfileCalculation::massDepth(double depth) const {
    auto averageMass = [this](size_t bin) {
        double mass = 0;
        for(qsizetype e = 0; e < m_elements.size(); e++) {
            mass += m_composition[bin][static_cast<size_t>(e)] * Elements::mass(m_elements[e], 0);
        }
        return mass;
    };
    if(m_bins == 0 || m_composition.empty()) {
        return 0;
    }
    if(depth <= 0) {
        return depth * averageMass(0);
    }
    double mass = 0;
    double remaining = depth;
    for(size_t j = 0; j < m_bins && remaining > 0; j++) {
        const double width = j + 1 < m_bins ? std::min(remaining, m_binWidth) : remaining; // The last one continues
        mass += width * averageMass(j);
        remaining -= width;
    }
    return mass;
}

void snapToBins(double &min, double &max, double binWidth) {
    if(min > max) {
        std::swap(min, max);
    }
    min = std::round(min / binWidth) * binWidth;
    max = std::max(min + binWidth, std::round(max / binWidth) * binWidth);
}

DepthProfileResult binDepthProfile(const DepthProfileCalculation &calculation, const DepthProfile &profile) {
    DepthProfileResult result;
    const double width = profile.binWidth;
    result.binWidth = width;
    if(width <= 0) {
        result.errorMessage = QStringLiteral("The bin width of the profile must be positive.");
        return result;
    }
    // From the highest event above the surface, at most a number of bins, to the maximum depth
    double minDepth = 0;
    for(const auto &selection : calculation.selections()) {
        for(const double depth : selection.depths) {
            if(!std::isnan(depth)) {
                minDepth = std::min(minDepth, depth);
            }
        }
    }
    const double maxDepth = calculation.stoppingBinWidth() * static_cast<double>(calculation.composition().size());
    const double firstBin = std::max(-static_cast<double>(MaxBinsAboveSurface), std::floor(minDepth / width));
    const double binCount = std::ceil(maxDepth / width) - firstBin;
    if(binCount > static_cast<double>(MaxProfileBins)) {
        result.errorMessage = QString("There would be more than %1 bins in the profile, make them wider.").arg(MaxProfileBins);
        return result;
    }
    const auto bins = static_cast<size_t>(std::max(1.0, binCount));
    result.firstBinStart = firstBin * width;

    // Sums of the weights of the events and of their squares, the latter are the variances of the former
    std::vector<std::vector<double>> weights;
    std::vector<std::vector<double>> variances;
    for(const auto &selection : calculation.selections()) {
        DepthProfileCurve curve;
        curve.cutId = selection.cutId;
        curve.cutIds = {selection.cutId};
        curve.z = selection.z;
        curve.a = selection.a;
        curve.type = selection.type;
        curve.representsElement = selection.representsElement;
        curve.counts.assign(bins, 0);
        std::vector<double> &binWeights = weights.emplace_back(bins, 0);
        std::vector<double> &binVariances = variances.emplace_back(bins, 0);
        for(size_t i = 0; i < selection.depths.size(); i++) {
            const double position = std::floor((selection.depths[i] - result.firstBinStart) / width);
            if(std::isnan(position) || position < 0 || position >= static_cast<double>(bins)) {
                continue;
            }
            binWeights[static_cast<size_t>(position)] += selection.weights[i];
            binVariances[static_cast<size_t>(position)] += selection.weights[i] * selection.weights[i];
            curve.counts[static_cast<size_t>(position)]++;
            curve.events++;
        }
        result.curves.push_back(std::move(curve));
    }
    // The selections' own weights and the curves they are in, for the uncertainties of the range weights
    const std::vector<std::vector<double>> selectionWeights = weights;
    std::vector<size_t> curveOf(weights.size());
    std::iota(curveOf.begin(), curveOf.end(), size_t{0});
    // Selections of the same element and type summed into a curve of the element. Their events are independent, so
    // the variances add up as well.
    if(!profile.separateIsotopes) {
        std::vector<DepthProfileCurve> curves;
        std::vector<std::vector<double>> curveWeights;
        std::vector<std::vector<double>> curveVariances;
        for(size_t c = 0; c < result.curves.size(); c++) {
            DepthProfileCurve &curve = result.curves[c];
            const auto same = std::find_if(curves.begin(), curves.end(), [&curve](const DepthProfileCurve &other) {
                return other.z == curve.z && other.type == curve.type;
            });
            if(same == curves.end()) {
                curveOf[c] = curves.size();
                curves.push_back(std::move(curve));
                curveWeights.push_back(std::move(weights[c]));
                curveVariances.push_back(std::move(variances[c]));
                continue;
            }
            const auto index = static_cast<size_t>(same - curves.begin());
            curveOf[c] = index;
            same->a = 0;
            same->representsElement = false;
            same->cutIds.append(curve.cutId);
            same->events += curve.events;
            for(size_t i = 0; i < bins; i++) {
                same->counts[i] += curve.counts[i];
                curveWeights[index][i] += weights[c][i];
                curveVariances[index][i] += variances[c][i];
            }
        }
        result.curves = std::move(curves);
        weights = std::move(curveWeights);
        variances = std::move(curveVariances);
    }
    std::vector<double> totalWeights(bins, 0);
    std::vector<size_t> totalCounts(bins, 0);
    for(size_t c = 0; c < weights.size(); c++) {
        for(size_t i = 0; i < bins; i++) {
            totalWeights[i] += weights[c][i];
            totalCounts[i] += result.curves[c].counts[i];
        }
    }

    // Bins in an interval snapped to them, inclusive
    auto binsIn = [&result, bins, width](double &min, double &max) {
        snapToBins(min, max, width);
        const double first = std::max(0.0, std::round((min - result.firstBinStart) / width));
        const double last = std::min(static_cast<double>(bins), std::round((max - result.firstBinStart) / width));
        std::vector<size_t> inside;
        for(double i = first; i < last; i++) {
            inside.push_back(static_cast<size_t>(i));
        }
        return inside;
    };

    // The interval where the total is 100 % on average. Found automatically, it is the longest run of bins where the
    // moving average of the total stays near its typical level, the median weighted by the events. Edges at the
    // surface, at interfaces and at the end of the profile, smeared by the depth resolution, end the run. Sparse bins
    // hardly affect the typical level.
    std::vector<size_t> normalizationBins;
    if(profile.autoNormalization) {
        std::vector<double> smoothed(bins, 0);
        std::vector<double> smoothedCounts(bins, 0); // Events in the moving average
        for(size_t i = 0; i < bins; i++) {
            const size_t first = i >= AutoNormalizationSmoothing ? i - AutoNormalizationSmoothing : 0;
            const size_t last = std::min(bins - 1, i + AutoNormalizationSmoothing);
            for(size_t j = first; j <= last; j++) {
                smoothed[i] += totalWeights[j];
                smoothedCounts[i] += static_cast<double>(totalCounts[j]);
            }
            smoothed[i] /= static_cast<double>(last - first + 1);
        }
        std::vector<std::pair<double, double>> levels; // Smoothed total and events of the bins with events
        double allEvents = 0;
        for(size_t i = 0; i < bins; i++) {
            if(totalCounts[i] > 0) {
                levels.emplace_back(smoothed[i], static_cast<double>(totalCounts[i]));
                allEvents += static_cast<double>(totalCounts[i]);
            }
        }
        std::sort(levels.begin(), levels.end());
        double typical = 0;
        double events = 0;
        for(const auto &[level, count] : levels) {
            events += count;
            if(events >= allEvents / 2) {
                typical = level;
                break;
            }
        }
        size_t bestFirst = 0;
        size_t bestLength = 0;
        size_t runFirst = 0;
        size_t runLength = 0;
        for(size_t i = 0; i < bins; i++) {
            const double allowed = smoothedCounts[i] > 0
                    ? std::max(AutoNormalizationTolerance * typical, AutoNormalizationDeviations * smoothed[i] / std::sqrt(smoothedCounts[i]))
                    : 0;
            if(totalCounts[i] > 0 && std::abs(smoothed[i] - typical) <= allowed) {
                if(runLength == 0) {
                    runFirst = i;
                }
                runLength++;
                if(runLength > bestLength) {
                    bestFirst = runFirst;
                    bestLength = runLength;
                }
            } else {
                runLength = 0;
            }
        }
        for(size_t i = bestFirst; i < bestFirst + bestLength; i++) {
            normalizationBins.push_back(i);
        }
    } else {
        double min = profile.normalizationMin;
        double max = profile.normalizationMax;
        normalizationBins = binsIn(min, max);
    }
    double scale = 0;
    for(const size_t i : normalizationBins) {
        scale += totalWeights[i];
    }
    if(normalizationBins.empty() || scale <= 0) {
        result.errorMessage = QStringLiteral("There are no events in the normalization interval.");
        return result;
    }
    scale /= static_cast<double>(normalizationBins.size());
    result.normalizationMin = result.binStart(normalizationBins.front());
    result.normalizationMax = result.binStart(normalizationBins.back()) + width;

    // Statistical uncertainties come from the variances of the summed weights, propagated through the normalization.
    // The scale is the average total in the normalization interval, so bins in it are correlated with the scale: a
    // single selection normalized over the interval it is averaged over has no uncertainty.
    //
    // The range weight of a selection using a part of the measurement scales all of its weights, so its uncertainty is
    // correlated over the bins and with the scale. With d_s the derivative of a value (in weights) by the logarithm of
    // the range weight of selection s, the variance gets sum over s, t of d_s d_t cov(s, t), see Selection.
    const auto &selections = calculation.selections();
    std::vector<std::pair<size_t, size_t>> correlated; // Selections with a non-zero covariance
    for(size_t s = 0; s < selections.size(); s++) {
        for(size_t t = 0; t < selections.size(); t++) {
            if(std::abs(selections[s].rangeCovariances[t]) > 1e-15) {
                correlated.emplace_back(s, t);
            }
        }
    }
    auto rangeVariance = [&selections, &correlated](const std::vector<double> &derivatives) {
        double variance = 0;
        for(const auto &[s, t] : correlated) {
            variance += derivatives[s] * derivatives[t] * selections[s].rangeCovariances[t];
        }
        return variance;
    };
    std::vector<double> selectionScales(selections.size(), 0); // Each selection's part of the scale
    for(size_t s = 0; s < selections.size(); s++) {
        for(const size_t i : normalizationBins) {
            selectionScales[s] += selectionWeights[s][i];
        }
        selectionScales[s] /= static_cast<double>(normalizationBins.size());
    }
    std::vector<double> derivatives(selections.size());

    const auto normalizationCount = static_cast<double>(normalizationBins.size());
    std::vector<bool> inNormalization(bins, false);
    double normalizationVariance = 0; // Of the sum of the weights of all selections in the normalization bins
    for(const size_t i : normalizationBins) {
        inNormalization[i] = true;
        for(const auto &binVariances : variances) {
            normalizationVariance += binVariances[i];
        }
    }
    result.totals.assign(bins, 0);
    for(size_t c = 0; c < result.curves.size(); c++) {
        DepthProfileCurve &curve = result.curves[c];
        curve.concentrations.resize(bins);
        curve.uncertainties.resize(bins);
        for(size_t i = 0; i < bins; i++) {
            const double concentration = weights[c][i] / scale;
            // c = W / S: the bin's own weight enters both, the other normalization bins only the scale
            const double share = inNormalization[i] ? concentration / normalizationCount : 0;
            double variance = variances[c][i] * ((1 - share) * (1 - share) - share * share)
                              + concentration * concentration * normalizationVariance / (normalizationCount * normalizationCount);
            for(size_t s = 0; s < selections.size(); s++) {
                derivatives[s] = (curveOf[s] == c ? selectionWeights[s][i] : 0) - concentration * selectionScales[s];
            }
            variance += rangeVariance(derivatives);
            curve.concentrations[i] = concentration;
            curve.uncertainties[i] = curve.counts[i] > 0 ? std::sqrt(std::max(0.0, variance)) / scale : 0.0; // Exact 0
            result.totals[i] += concentration;
        }
    }

    // Averages over the averaging interval: the mean of the selection's weights divided by the scale
    result.averageMin = profile.averageFollowsNormalization ? result.normalizationMin : profile.averageMin;
    result.averageMax = profile.averageFollowsNormalization ? result.normalizationMax : profile.averageMax;
    const std::vector<size_t> averageBins = binsIn(result.averageMin, result.averageMax);
    const auto averageCount = static_cast<double>(averageBins.size());
    std::vector<bool> inAverage(bins, false);
    for(const size_t i : averageBins) {
        inAverage[i] = true;
    }
    std::vector<double> averageVariances(result.curves.size(), 0); // Of the mean weights over the averaging interval
    std::vector<double> selectionAverages(selections.size(), 0); // Mean weights of the selections
    for(size_t s = 0; s < selections.size(); s++) {
        for(const size_t i : averageBins) {
            selectionAverages[s] += selectionWeights[s][i];
        }
        selectionAverages[s] /= std::max(1.0, averageCount);
    }
    for(size_t c = 0; c < result.curves.size(); c++) {
        DepthProfileResult::Average average;
        for(const size_t i : averageBins) {
            average.concentration += result.curves[c].concentrations[i];
            average.counts += result.curves[c].counts[i];
            averageVariances[c] += variances[c][i];
        }
        if(!averageBins.empty()) {
            average.concentration /= averageCount;
            averageVariances[c] /= averageCount * averageCount;
            // Every weight in either interval affects the average: d average / d W(j, k), with the variances of W
            double variance = 0;
            for(size_t j = 0; j < result.curves.size(); j++) {
                for(size_t i = 0; i < bins; i++) {
                    if(!inAverage[i] && !inNormalization[i]) {
                        continue;
                    }
                    const double derivative = ((j == c && inAverage[i]) ? 1 / averageCount : 0)
                                              - (inNormalization[i] ? average.concentration / normalizationCount : 0);
                    variance += derivative * derivative * variances[j][i];
                }
            }
            for(size_t s = 0; s < selections.size(); s++) {
                derivatives[s] = (curveOf[s] == c ? selectionAverages[s] : 0) - average.concentration * selectionScales[s];
            }
            variance += rangeVariance(derivatives);
            average.uncertainty = average.counts > 0 ? std::sqrt(std::max(0.0, variance)) / scale : 0.0; // Exact 0
        }
        result.averages.push_back(average);
    }

    // Renormalized to 100 % among the selections that are not left out, r = A / T where A is the mean weight of the
    // selection and T the sum of them over the included selections. The scale cancels out.
    double includedSum = 0;
    for(size_t c = 0; c < result.curves.size(); c++) {
        if(!result.curves[c].allIn(profile.cutIdsExcludedFromAverage)) {
            includedSum += result.averages[c].concentration * scale;
        }
    }
    for(size_t c = 0; c < result.curves.size(); c++) {
        DepthProfileResult::Average &average = result.averages[c];
        if(includedSum <= 0) {
            average.renormalized = NaN;
            average.renormalizedUncertainty = NaN;
            continue;
        }
        const double renormalized = average.concentration * scale / includedSum;
        double variance = 0;
        for(size_t j = 0; j < result.curves.size(); j++) {
            const bool included = !result.curves[j].allIn(profile.cutIdsExcludedFromAverage);
            const double derivative = (j == c ? 1.0 : 0.0) - (included ? renormalized : 0.0); // Times 1 / T
            variance += derivative * derivative * averageVariances[j];
        }
        for(size_t s = 0; s < selections.size(); s++) {
            const bool included = !result.curves[curveOf[s]].allIn(profile.cutIdsExcludedFromAverage);
            derivatives[s] = ((curveOf[s] == c ? 1.0 : 0.0) - (included ? renormalized : 0.0)) * selectionAverages[s];
        }
        variance += rangeVariance(derivatives);
        // Left out selections have no renormalized value
        const bool excluded = result.curves[c].allIn(profile.cutIdsExcludedFromAverage);
        average.renormalized = excluded ? NaN : renormalized;
        const double uncertainty = average.counts > 0 ? std::sqrt(std::max(0.0, variance)) : 0.0; // Exact 0
        average.renormalizedUncertainty = excluded ? NaN : uncertainty / includedSum;
    }
    return result;
}

double upperLimit(double value, double uncertainty) {
    if(!std::isfinite(value) || !std::isfinite(uncertainty) || uncertainty <= 0 || value >= 2 * uncertainty) {
        return NaN;
    }
    if(value <= 0) {
        return uncertainty;
    }
    const double events = (value / uncertainty) * (value / uncertainty);
    // Fewer than one event only when the uncertainty is not just counting statistics
    const double weight = std::min(uncertainty * uncertainty / value, uncertainty);
    return (events + 1 + std::sqrt(events + 0.75)) * weight;
}

double roundedUp(double value, int decimals) {
    const double step = std::pow(10.0, -decimals);
    // Not up to the next step when the value is on a step but for rounding errors
    return std::ceil(value / step * (1 - 1e-12)) * step;
}

PercentText percentText(double fraction, double uncertainty, bool rounded) {
    if(std::isnan(fraction)) {
        return {QStringLiteral("—"), QString()};
    }
    const double value = fraction * 100;
    const double deviation = uncertainty * 100;
    if(value == 0 && deviation == 0) {
        return {QStringLiteral("0"), QString()}; // Without events
    }
    if(!std::isfinite(deviation) || deviation <= 0) {
        return {QString::number(value, 'f', 2), QString()};
    }
    if(!rounded) {
        return {QString::number(value, 'f', 2), QString::number(deviation, 'f', 2)};
    }
    // One significant digit, or two when they are 15 or less
    auto decimalsOf = [](double x) {
        const int exponent = static_cast<int>(std::floor(std::log10(x)));
        const bool twoDigits = std::round(x / std::pow(10.0, exponent - 1)) <= 15;
        return (twoDigits ? 1 : 0) - exponent;
    };
    const double limit = upperLimit(value, deviation);
    if(!std::isnan(limit)) {
        const int decimals = decimalsOf(limit);
        return {QString("< %1").arg(roundedUp(limit, decimals), 0, 'f', std::max(decimals, 0)), QString()};
    }
    const int decimals = decimalsOf(deviation);
    const double step = std::pow(10.0, -decimals);
    const auto round = [&](double x) { return QString::number(std::round(x / step) * step, 'f', std::max(decimals, 0)); };
    return {round(value), round(deviation)};
}

QString percentWithUncertainty(double fraction, double uncertainty, bool rounded, bool unit) {
    const PercentText text = percentText(fraction, uncertainty, rounded);
    const QString suffix = unit && !std::isnan(fraction) ? QStringLiteral(" at.%") : QString();
    return (text.uncertainty.isEmpty() ? text.value : QString("%1 ± %2").arg(text.value, text.uncertainty)) + suffix;
}

QString depthProfileLog(const DepthProfileCalculation &calculation) {
    if(!calculation.isValid()) {
        return calculation.errorMessage();
    }
    constexpr double Barn = 1e-28; // m²
    const double tfu = Units::ThinFilmUnit;
    const double keV = Units::Kiloelectronvolt;
    auto number = [](double value, int decimals = 1) { return QString::number(value, 'f', decimals); };
    auto general = [](double value) { return QString::number(value, 'g', 6); };
    const DepthProfileInputs &inputs = calculation.inputs();
    QStringList lines;

    // Events in total
    size_t inSelections = 0;
    size_t used = 0;
    size_t noEnergy = 0;
    size_t outside = 0;
    size_t withDepth = 0;
    size_t tooDeep = 0;
    size_t lowBeam = 0;
    for(const auto &selection : calculation.selections()) {
        inSelections += selection.events;
        used += selection.energies.size();
        noEnergy += selection.lowEfficiencyEvents;
        outside += selection.outsideStoppingEvents;
        tooDeep += selection.tooDeepEvents;
        lowBeam += selection.lowBeamEnergyEvents;
        withDepth += static_cast<size_t>(std::count_if(selection.depths.begin(), selection.depths.end(),
                                                       [](double depth) { return !std::isnan(depth); }));
    }
    lines << QStringLiteral("Events");
    lines << QString("  In the measurement: %1").arg(calculation.measurementEvents());
    lines << QString("  In the profile's part of the measurement: %1").arg(calculation.rangeEvents());
    lines << QString("  In the selections: %1 (an event in several selections is counted in each)").arg(inSelections);
    lines << QString("  Included: %1").arg(withDepth);
    lines << QString("  Left out: %1").arg(inSelections - withDepth);
    lines << QString("    No energy or efficiency below 1e-6: %1").arg(noEnergy);
    lines << QString("    Energy outside the stopping data: %1").arg(outside);
    lines << QString("    Beyond the maximum depth: %1").arg(tooDeep);
    lines << QString("    From where the beam is below its stopping data: %1").arg(lowBeam);
    lines << QString();

    QStringList symbols;
    for(const int z : calculation.elements()) {
        symbols.append(Elements::symbol(z));
    }
    lines << QString("Elements of the composition: %1").arg(symbols.join(", "));
    lines << QString("Stopping bins: %1 of %2 1e15 at./cm², to %3 1e15 at./cm²")
                     .arg(calculation.composition().size())
                     .arg(number(calculation.stoppingBinWidth() / tfu))
                     .arg(number(calculation.maxDepth() / tfu));
    lines << QString("Stopping data of the beam: %1–%2 keV")
                     .arg(number(calculation.beamMinEnergy() / keV), number(calculation.beamMaxEnergy() / keV));

    lines << QString() << QStringLiteral("Iterations");
    lines << QStringLiteral("  Iteration  Change (at.%)  Stopping bins  Beam energy at the end (keV)  Events with a depth"
                            "  Too deep  Beam below its stopping data");
    const auto &history = calculation.history();
    for(size_t i = 0; i < history.size(); i++) {
        const auto &h = history[i];
        lines << QString("  %1  %2  %3  %4  %5  %6  %7")
                         .arg(i + 1, 9)
                         .arg(std::isnan(h.change) ? QStringLiteral("bins changed") : number(h.change * 100, 4), 13)
                         .arg(h.stoppingBins, 13)
                         .arg(number(h.beamEnergyAtMaxDepth / keV), 28)
                         .arg(h.events, 19)
                         .arg(h.tooDeepEvents, 8)
                         .arg(h.lowBeamEnergyEvents, 28);
    }
    if(history.empty()) {
        lines << QStringLiteral("  None yet");
    }

    double allWeights = 0;
    for(const auto &selection : calculation.selections()) {
        allWeights += std::accumulate(selection.weights.begin(), selection.weights.end(), 0.0);
    }
    lines << QString() << QStringLiteral("Selections");
    for(const auto &selection : calculation.selections()) {
        size_t withDepth = 0;
        size_t aboveSurface = 0;
        double minDepth = std::numeric_limits<double>::infinity();
        double maxDepth = -std::numeric_limits<double>::infinity();
        double weightSum = 0;
        double crossSectionSum = 0;
        size_t weighted = 0;
        for(size_t i = 0; i < selection.depths.size(); i++) {
            const double depth = selection.depths[i];
            if(std::isnan(depth)) {
                continue;
            }
            withDepth++;
            aboveSurface += depth < 0 ? 1 : 0;
            minDepth = std::min(minDepth, depth);
            maxDepth = std::max(maxDepth, depth);
            if(selection.weights[i] > 0) {
                weighted++;
                weightSum += selection.weights[i];
                crossSectionSum += selection.efficiencyWeights[i] / selection.weights[i];
            }
        }
        const auto [minEnergy, maxEnergy] = std::minmax_element(selection.energies.begin(), selection.energies.end());
        const double otherWeights = selection.userWeight * selection.abundanceWeight * selection.rangeWeight;
        const double efficiencyWeightSum = std::accumulate(selection.efficiencyWeights.begin(),
                                                           selection.efficiencyWeights.end(), 0.0);

        QString name = cutLabel(selection.z, selection.a, selection.type, false);
        if(selection.representsElement) {
            name += QString(", represents %1").arg(Elements::symbol(selection.z));
        }
        lines << QString() << QString("%1").arg(name);
        lines << QString("  Events in the selection: %1").arg(selection.events);
        lines << QString("  Left out, no energy or efficiency below 1e-6: %1").arg(selection.lowEfficiencyEvents);
        QString outsideText = QString("  Left out, energy outside the stopping data: %1 (data %2–%3 keV in the sample")
                                      .arg(selection.outsideStoppingEvents)
                                      .arg(number(selection.minEnergy / keV), number(selection.maxEnergy / keV));
        if(std::isfinite(selection.foilMaxEnergy)) {
            outsideText += QString(", %1–%2 keV in the foil")
                                   .arg(number(selection.foilMinEnergy / keV), number(selection.foilMaxEnergy / keV));
        }
        lines << outsideText + QStringLiteral(")");
        lines << QString("  Used: %1").arg(selection.energies.size());
        lines << QString("    With a depth: %1, of which above the surface: %2").arg(withDepth).arg(aboveSurface);
        lines << QString("    Beyond the maximum depth: %1").arg(selection.tooDeepEvents);
        lines << QString("    From where the beam is below its stopping data: %1").arg(selection.lowBeamEnergyEvents);
        if(!selection.energies.empty()) {
            lines << QString("  Detected energy: %1–%2 keV").arg(number(*minEnergy / keV), number(*maxEnergy / keV));
        }
        if(withDepth > 0) {
            lines << QString("  Depth: %1–%2 1e15 at./cm²").arg(number(minDepth / tfu), number(maxDepth / tfu));
        }
        lines << QStringLiteral("  Weights");
        lines << QString("    User weight: %1").arg(selection.userWeight, 0, 'g', 6);
        lines << QString("    Abundance weight (1 / natural abundance): %1").arg(selection.abundanceWeight, 0, 'g', 6);
        lines << QString("    Range weight (reference counts of the profile / of the selection): %1")
                         .arg(selection.rangeWeight, 0, 'g', 6);
        if(!selection.efficiencyWeights.empty() && otherWeights > 0) {
            lines << QString("    Average 1 / efficiency: %1")
                             .arg(efficiencyWeightSum / static_cast<double>(selection.efficiencyWeights.size()) / otherWeights, 0, 'g', 6);
        }
        if(weighted > 0) {
            lines << QString("    Average cross section at the depths of the events: %1 b/sr")
                             .arg(crossSectionSum / static_cast<double>(weighted) / Barn, 0, 'g', 6);
            lines << QString("    Average weight of an event (all of the above): %1 sr/b")
                             .arg(weightSum / static_cast<double>(weighted) * Barn, 0, 'g', 6);
        }
        if(allWeights > 0) {
            lines << QString("    Share of the weights of all events: %1 %").arg(number(weightSum / allWeights * 100, 2));
        }
    }

    lines << QString() << QStringLiteral("Parameters");
    lines << QString("  Beam: %1, %2 keV, incident angle %3° from the surface")
                     .arg(Elements::label(inputs.beamZ, inputs.beamA), general(inputs.beamEnergy / keV),
                          general(inputs.incidentAngle / Units::Degree));
    lines << QString("  Scattering/recoil angle: %1°").arg(general(inputs.scatteringAngle / Units::Degree));
    lines << QString("  ToF calibration: length %1 mm, slope %2 ns/ch, offset %3 ns")
                     .arg(general(inputs.tofLength / Units::Millimetre), general(inputs.tofSlope / Units::Nanosecond),
                          general(inputs.tofOffset / Units::Nanosecond));
    lines << QString("  Timing detector foil: %1, %2 µg/cm²")
                     .arg(inputs.foilMaterial.elements.isEmpty() ? QStringLiteral("none") : materialText(inputs.foilMaterial),
                          general(inputs.foilThickness / Units::MicrogramPerSquareCentimetre));
    if(inputs.efficiencies.isEmpty()) {
        lines << QStringLiteral("  Efficiencies: none, all events have an efficiency of 1");
    } else {
        lines << QStringLiteral("  Efficiencies:");
        for(const EfficiencyCurve &curve : inputs.efficiencies) {
            QString range;
            if(!curve.points.isEmpty()) {
                range = QString(", %1–%2 keV").arg(general(curve.points.front().x() / keV), general(curve.points.back().x() / keV));
            }
            lines << QString("    %1: %2 points%3").arg(Elements::label(curve.z, curve.a)).arg(curve.points.size()).arg(range);
        }
    }
    if(inputs.kinematicCorrection) {
        const AngleCalibration &a = inputs.angleCalibration;
        lines << QString("  Kinematic correction: on, detector distance %1 mm, window width %2 mm")
                         .arg(general(inputs.detectorDistance / Units::Millimetre), general(inputs.windowWidth / Units::Millimetre));
        lines << QString("  Angle calibration: centre %1 ticks + %2 ticks/ch × ToF, half width %3 ticks, sigma %4 ticks, "
                         "%5, timing smoothing ±%6 ticks")
                         .arg(general(a.centre), general(a.centreSlope), general(a.halfWidth), general(a.sigma),
                              a.reversed ? QStringLiteral("reversed") : QStringLiteral("not reversed"), general(a.timingSmoothing));
    } else {
        lines << QStringLiteral("  Kinematic correction: off");
    }
    lines << QString("  Part of the measurement: %1–%2 of the reference counts%3")
                     .arg(general(inputs.rangeMin), general(inputs.rangeMax),
                          inputs.referencePolygon.isEmpty() ? QStringLiteral(" (no reference selection, all events)") : QString());
    lines << QStringLiteral("  Selections:");
    for(const auto &cut : inputs.cuts) {
        lines << QString("    %1: %2 polygon points, weight %3, abundance correction %4, part %5–%6")
                         .arg(cutLabel(cut.z, cut.a, cut.type, false))
                         .arg(cut.polygon.size())
                         .arg(general(cut.weight), general(cut.abundanceCorrection), general(cut.rangeMin), general(cut.rangeMax));
    }

    const PhysicsSettings &physics = inputs.physics;
    lines << QString("  Cross sections: Rutherford, screening %1").arg(screeningModelName(physics.screening));
    lines << QString("  Stopping of all elements: electronic factor %1, nuclear factor %2, stopping file %3")
                     .arg(general(physics.electronicFactor), general(physics.nuclearFactor),
                          physics.stoppingFile.isEmpty() ? QStringLiteral("automatic (JIBAL)") : physics.stoppingFile);
    lines << QString("  Nuclear stopping: universal, target mass: %1").arg(nuclearStoppingMassName(physics.nuclearMass).toLower());
    lines << QString("  Stopping interpolation between the points of the files: %1")
                     .arg(physics.interpolation == StoppingInterpolation::Cubic
                                  ? QStringLiteral("cubic spline in the logarithm of energy")
                                  : QStringLiteral("linear in energy"));
    if(physics.corrections.isEmpty()) {
        lines << QStringLiteral("  Stopping of element pairs: none");
    } else {
        lines << QStringLiteral("  Stopping of element pairs:");
        for(const StoppingCorrection &c : physics.corrections) {
            lines << QString("    %1 in %2: electronic factor %3, nuclear factor %4, stopping file %5")
                             .arg(Elements::symbol(c.z1), c.z2 == AnyElement ? QStringLiteral("any element") : Elements::symbol(c.z2),
                                  general(c.electronicFactor), general(c.nuclearFactor),
                                  c.stoppingFile.isEmpty() ? QStringLiteral("same as all") : c.stoppingFile);
        }
    }
    lines << QStringLiteral("  Electronic stopping files used:");
    for(const auto &particle : calculation.particleStopping()) {
        QStringList files;
        const QStringList &names = particle.table->fileNames();
        for(qsizetype e = 0; e < names.size() && e < calculation.elements().size(); e++) {
            files.append(QString("%1: %2").arg(Elements::symbol(calculation.elements()[e]), names[e]));
        }
        lines << QString("    %1 %2 (%3 u) in %4; data %5–%6 keV")
                         .arg(particle.beam ? QStringLiteral("Beam") : QStringLiteral("Detected"), Elements::symbol(particle.z),
                              general(particle.mass / C_U), files.join(", "), number(particle.table->minEnergy() / keV),
                              number(particle.table->maxEnergy() / keV));
    }

    lines << QString("  Stopping bins: %1 1e15 at./cm², maximum depth %2 1e15 at./cm²%3")
                     .arg(general(inputs.stoppingBinWidth / tfu), general(inputs.maxDepth / tfu),
                          inputs.autoMaxDepth ? QStringLiteral(" at the start, automatic") : QString());
    lines << QString("  Smoothing: %1 pseudo-events in each stopping bin").arg(inputs.minStoppingBinEvents);
    const DepthProfilePreferences preferences = Defaults::depthProfilePreferences();
    lines << QString("  Convergence: no concentration changes more than %1 at.%, at most %2 iterations in a run "
                     "(preferences)").arg(general(preferences.tolerance * 100)).arg(preferences.maxIterations);
    lines << QString("  Exit energy tables: %1 energies over a factor of %2, evenly on a logarithmic scale; at most %3 "
                     "steps in slowing down through a stopping bin; at most %4 stopping bins")
                     .arg(GridPoints).arg(general(GridRange)).arg(MaxSteps).arg(MaxStoppingBins);
    lines << QString("  %1").arg(stoppingNumerics());

    const jibal *library = Elements::library();
    lines << QString() << QStringLiteral("JIBAL");
    lines << QString("  Version: %1").arg(jibal_version());
    if(library) {
        const char *configFile = jibal_config_filename(library);
        lines << QString("  Configuration file: %1").arg(configFile ? QString::fromUtf8(configFile) : QStringLiteral("none"));
        lines << QString("  Data directory: %1").arg(library->config->datadir ? QString::fromUtf8(library->config->datadir) : QString());
        lines << QString("  Masses: %1").arg(library->config->masses_file ? QString::fromUtf8(library->config->masses_file) : QString());
        lines << QString("  Abundances: %1").arg(library->config->abundances_file ? QString::fromUtf8(library->config->abundances_file) : QString());
        lines << QStringLiteral("  GSTO files:");
        for(size_t i = 0; i < library->gsto->n_files; i++) {
            const gsto_file_t &file = library->gsto->files[i];
            const QString type = file.type == GSTO_STO_ELE ? QStringLiteral("electronic stopping")
                                 : file.type == GSTO_STO_STRAGG ? QStringLiteral("straggling")
                                                                : QStringLiteral("other");
            auto zRange = [](int min, int max) {
                return min == JIBAL_ANY_Z ? QStringLiteral("any") : min == max ? QString::number(min) : QString("%1–%2").arg(min).arg(max);
            };
            lines << QString("    %1 (%2, %3): Z1 %4, Z2 %5, %6")
                             .arg(QString::fromUtf8(file.name), type,
                                  file.source ? QString::fromUtf8(file.source) : QStringLiteral("no source"),
                                  zRange(file.Z1_min, file.Z1_max), zRange(file.Z2_min, file.Z2_max),
                                  file.filename ? QString::fromUtf8(file.filename) : QString());
        }
    }
    return lines.join('\n');
}
