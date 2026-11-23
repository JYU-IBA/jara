/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#include "energyspectrum.h"
#include "elements.h"
#include "split.h"
#include "stopping.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

namespace {
// Keeps memory use reasonable when a few events have a very short time of flight
constexpr size_t MaxBins = 1000000;
}

EnergySpectrumInputs energySpectrumInputs(const Measurement &measurement, const EnergySpectrum &spectrum) {
    EnergySpectrumInputs inputs;
    if(const ExperimentalSettings *settings = measurement.settings) {
        inputs.beamZ = settings->beamZ;
        inputs.beamA = settings->beamA;
    }
    if(const DetectorSettings *detector = measurement.detector) {
        inputs.hasDetector = true;
        inputs.tofLength = detector->tofLength;
        inputs.tofSlope = detector->tofSlope;
        inputs.tofOffset = detector->tofOffset;
        inputs.foilMaterial = detector->foilMaterial;
        inputs.foilThickness = detector->foilThickness;
    }
    inputs.physics = measurement.physicsOrDefault();
    inputs.physics.name.clear();
    inputs.binWidth = spectrum.binWidth;
    inputs.referencePolygon = measurement.referencePolygon();
    inputs.rangeMin = spectrum.rangeMin;
    inputs.rangeMax = spectrum.rangeMax;
    for(const Cut &cut : measurement.cuts) {
        if(spectrum.cutIds.contains(cut.id) && isAnalyzed(cut)) {
            inputs.cuts.append({cut.id, cut.z, cut.a, cut.type, cut.polygon});
        }
    }
    return inputs;
}

double particleMass(CutType type, int z, int a, int beamZ, int beamA) {
    if(type == CutType::Scattered) {
        return beamZ > 0 ? Elements::mass(beamZ, beamA) : 0;
    }
    return Elements::mass(z, a);
}

double kinematicFactor(CutType type, double m1, double m2, double angle) {
    if(type == CutType::Scattered) {
        const double sine = std::sin(angle);
        const double root = m2 * m2 - m1 * m1 * sine * sine;
        if(root < 0) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        const double k = (std::sqrt(root) + m1 * std::cos(angle)) / (m1 + m2);
        return k * k;
    }
    if(angle >= 90 * Units::Degree) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double cosine = std::cos(angle);
    return 4 * m1 * m2 * cosine * cosine / ((m1 + m2) * (m1 + m2));
}

double tofToEnergy(double tofChannel, double mass, double tofLength, double tofSlope, double tofOffset) {
    const double t = tofSlope * tofChannel + tofOffset;
    if(t <= 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double v = tofLength / t;
    return 0.5 * mass * v * v;
}

double energyToTof(double energy, double mass, double tofLength) {
    return tofLength / std::sqrt(2 * energy / mass);
}

EnergySpectrumResult calculateEnergySpectrum(const std::vector<ToFEEvent> &events, const EnergySpectrumInputs &inputs) {
    EnergySpectrumResult result;
    result.binWidth = inputs.binWidth;
    if(!inputs.hasDetector) {
        result.errorMessage = QStringLiteral("The measurement has no detector settings, the ToF calibration is needed.");
        return result;
    }
    if(inputs.tofLength <= 0 || inputs.tofSlope <= 0) {
        result.errorMessage = QStringLiteral("The ToF calibration of the detector is not set (positive length and slope are needed).");
        return result;
    }
    if(inputs.binWidth <= 0) {
        result.errorMessage = QStringLiteral("The bin width must be positive.");
        return result;
    }

    const EventDivision division(events, inputs.referencePolygon);
    const std::span<const ToFEEvent> range = division.range(inputs.rangeMin, inputs.rangeMax);

    // Energies first, the bins cover them all
    std::vector<std::vector<double>> energies;
    double maxEnergy = 0;
    for(const auto &cut : inputs.cuts) {
        const bool scattered = cut.type == CutType::Scattered;
        if(scattered && inputs.beamZ <= 0) {
            result.errorMessage = QString("%1 is a scattered ion selection, the incident ion from the experimental "
                                          "settings of the measurement is needed.").arg(cutLabel(cut.z, cut.a, cut.type, false));
            return result;
        }
        const double mass = particleMass(cut.type, cut.z, cut.a, inputs.beamZ, inputs.beamA);
        if(mass <= 0) {
            result.errorMessage = QString("The mass of %1 is not known.")
                                          .arg(scattered ? Elements::label(inputs.beamZ, inputs.beamA) : Elements::label(cut.z, cut.a));
            return result;
        }
        const int particleZ = scattered ? inputs.beamZ : cut.z;
        FoilEnergyLoss foil(particleZ, mass, inputs.foilMaterial, inputs.foilThickness, inputs.physics);
        if(!foil.isValid()) {
            result.errorMessage = QString("Energy loss of %1 in the timing detector foil: %2")
                                          .arg(cutLabel(cut.z, cut.a, cut.type, false), foil.errorMessage());
            return result;
        }
        EnergyHistogram histogram;
        histogram.cutId = cut.id;
        histogram.z = cut.z;
        histogram.a = cut.a;
        histogram.type = cut.type;
        std::vector<double> &cutEnergies = energies.emplace_back(); // After the foil until corrected below
        double maxCutEnergy = 0;
        std::mt19937 random(static_cast<std::mt19937::result_type>(cut.id));
        std::uniform_real_distribution<double> dither(-0.5, 0.5);
        forEachEventIn(range, cut.polygon, [&](const ToFEEvent &event) {
            histogram.events++;
            const double tof = event.tof + dither(random); // Anywhere within the channel
            const double energy = tofToEnergy(tof, mass, inputs.tofLength, inputs.tofSlope, inputs.tofOffset);
            if(std::isnan(energy)) {
                histogram.rejectedEvents++;
                return;
            }
            cutEnergies.push_back(energy);
            maxCutEnergy = std::max(maxCutEnergy, energy);
        });
        foil.tabulate(maxCutEnergy);
        for(double &energy : cutEnergies) {
            energy = foil.energyBefore(energy);
            maxEnergy = std::max(maxEnergy, energy);
        }
        result.histograms.push_back(std::move(histogram));
    }

    const auto bins = static_cast<size_t>(std::min(static_cast<double>(MaxBins), std::floor(maxEnergy / inputs.binWidth) + 1));
    for(size_t i = 0; i < result.histograms.size(); i++) {
        EnergyHistogram &histogram = result.histograms[i];
        histogram.counts.assign(bins, 0);
        for(const double energy : energies[i]) {
            const double bin = std::floor(energy / inputs.binWidth);
            if(bin < static_cast<double>(bins)) {
                histogram.counts[static_cast<size_t>(bin)]++;
            } else {
                histogram.rejectedEvents++;
            }
        }
    }
    return result;
}
