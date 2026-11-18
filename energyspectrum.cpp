#include "energyspectrum.h"
#include "elements.h"

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
    }
    inputs.binWidth = spectrum.binWidth;
    for(const Cut &cut : measurement.cuts) {
        if(spectrum.cutIds.contains(cut.id)) {
            inputs.cuts.append({cut.id, cut.z, cut.a, cut.type, cut.polygon});
        }
    }
    return inputs;
}

double tofToEnergy(double tofChannel, double mass, double tofLength, double tofSlope, double tofOffset) {
    const double t = tofSlope * tofChannel + tofOffset;
    if(t <= 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double v = tofLength / t;
    return 0.5 * mass * v * v;
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
        const double mass = scattered ? Elements::mass(inputs.beamZ, inputs.beamA) : Elements::mass(cut.z, cut.a);
        if(mass <= 0) {
            result.errorMessage = QString("The mass of %1 is not known.")
                                          .arg(scattered ? Elements::label(inputs.beamZ, inputs.beamA) : Elements::label(cut.z, cut.a));
            return result;
        }
        EnergyHistogram histogram;
        histogram.cutId = cut.id;
        histogram.z = cut.z;
        histogram.a = cut.a;
        histogram.type = cut.type;
        std::vector<double> &cutEnergies = energies.emplace_back();
        std::mt19937 random(static_cast<std::mt19937::result_type>(cut.id));
        std::uniform_real_distribution<double> dither(-0.5, 0.5);
        forEachEventIn(events, cut.polygon, [&](const ToFEEvent &event) {
            histogram.events++;
            const double tof = event.tof + dither(random); // Anywhere within the channel
            const double energy = tofToEnergy(tof, mass, inputs.tofLength, inputs.tofSlope, inputs.tofOffset);
            if(std::isnan(energy)) {
                histogram.rejectedEvents++;
                return;
            }
            cutEnergies.push_back(energy);
            maxEnergy = std::max(maxEnergy, energy);
        });
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
