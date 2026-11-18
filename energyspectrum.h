#ifndef JARA_ENERGYSPECTRUM_H
#define JARA_ENERGYSPECTRUM_H

#include <vector>

#include <QList>
#include <QPointF>
#include <QString>

#include "project.h"
#include "tofedata.h"


// Energy spectra of selections, with energies from the time of flight: t = slope * channel + offset, v = length / t
// and E = m v^2 / 2. For recoils, the mass is that of the selected isotope, or the average mass of the naturally
// occurring isotopes when no isotope is selected. For scattered ions, it is the mass of the incident ion. ToF channels are integers, so each event is spread uniformly within its
// channel before the conversion. Otherwise the discrete energies of the channels would show up as spikes in bins
// narrower than a few channels. The spreading uses a fixed seed for each selection, so the same inputs always give
// the same spectrum.

// Everything a spectrum is calculated from, apart from the events. Comparing these tells whether a calculated
// spectrum is still up to date.
struct EnergySpectrumInputs {
    struct CutInput {
        int id;
        int z;
        int a;
        CutType type;
        QList<QPointF> polygon;
        bool operator==(const CutInput &) const = default;
    };
    int beamZ = 0; // Incident ion, zero when there are no experimental settings
    int beamA = 0;
    bool hasDetector = false;
    double tofLength = 0; // m
    double tofSlope = 0; // s/ch
    double tofOffset = 0; // s
    double binWidth = 0; // J
    QList<CutInput> cuts; // Included cuts in the order of the measurement's cuts
    bool operator==(const EnergySpectrumInputs &) const = default;
};

struct EnergyHistogram {
    int cutId = 0;
    int z = 0;
    int a = 0;
    CutType type = CutType::Recoil;
    std::vector<size_t> counts; // Bin i covers energies from i * binWidth to (i + 1) * binWidth
    size_t events = 0; // In the selection
    size_t rejectedEvents = 0; // ToF at or below zero, or energy above the highest bin, not in the counts
};

struct EnergySpectrumResult {
    double binWidth = 0; // J
    std::vector<EnergyHistogram> histograms; // All have the same number of bins
    QString errorMessage; // Empty when the spectrum could be calculated
    bool isValid() const { return errorMessage.isEmpty(); }
};

EnergySpectrumInputs energySpectrumInputs(const Measurement &measurement, const EnergySpectrum &spectrum);

// Energy in J of a particle with the given mass (kg), NaN when the time of flight is not positive
double tofToEnergy(double tofChannel, double mass, double tofLength, double tofSlope, double tofOffset);

EnergySpectrumResult calculateEnergySpectrum(const std::vector<ToFEEvent> &events, const EnergySpectrumInputs &inputs);

#endif // JARA_ENERGYSPECTRUM_H
