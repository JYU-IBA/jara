#ifndef JARA_MASSSELECTION_H
#define JARA_MASSSELECTION_H

#include <limits>
#include <span>
#include <vector>

#include <QList>
#include <QPointF>
#include <QString>

#include "energydetector.h"
#include "project.h"
#include "tofedata.h"

// Automatic selections from the mass bands of a ToF-E histogram.
//
// The mass of a particle is m = 2 E t² / L², but the E detector is not linear in energy and its response depends on
// the mass (pulse height defect). The proto-mass ln(E_ch × t²), with t from the ToF calibration in ns, still separates
// the masses: each mass is a band that runs nearly level against ToF. The bands are found as ridges of the proto-mass
// distribution in slices of ToF, followed from slice to slice.

// ln(E channel × (t / ns)²), NaN when the time of flight or the E channel is not positive
double logProtoMass(double tofChannel, double energyChannel, double tofSlope, double tofOffset);

// A band of one mass
struct MassBand {
    std::vector<QPointF> ridge; // ToF channel and proto-mass of the peak in each slice, increasing ToF
    std::vector<double> widths; // Standard deviation of the proto-mass in each slice
    std::vector<double> counts; // Events of the band in each slice
    double edgeTof = 0; // Channel of the leading (short ToF) edge, NaN when it was not found
    double edgeSigma = 0; // Channels
    bool edgeFitted = false; // Otherwise the edge is a first guess, or where a layer begins
    size_t events = 0; // Within the band, summed over the slices
    // A band whose events rise again sharply deeper in the sample is split there into layers: e.g. a film of Al on a Si
    // substrate, whose masses are too near to be told apart. The deeper layer has the index of the one above it, which
    // ends where it begins. NaN and -1 for bands that are not split.
    double layerEnd = std::numeric_limits<double>::quiet_NaN(); // Channel
    int layerAbove = -1;
    std::vector<size_t> profile; // Events near the ridge in each ToF channel, only while the bands are found
    double protoMassAt(double tof) const; // Interpolated along the ridge, extended level beyond its ends
    double widthAt(double tof) const; // Same as above
    double startTof() const { return ridge.empty() ? 0 : ridge.front().x(); }
    double endTof() const { return ridge.empty() ? 0 : ridge.back().x(); }
};

struct BandSearchOptions {
    double tofSliceWidth = 40; // Channels
    int window = 2; // Slices on each side added to a slice for finding its peaks, faint bands need more events
    double binWidth = 0.004; // Of the proto-mass
    double smoothing = 2; // Bins, standard deviation of the Gaussian smoothing of the distribution in a slice
    double minPeakCounts = 15; // Events of a peak in a slice
    double tolerance = 0.04; // Largest change of the proto-mass of a ridge from one slice to the next
    int maxGap = 3; // Slices a ridge may miss before it ends
    double mergeGap = 800; // Channels of ToF between the end of a ridge and the start of another that continues it
    int minSlices = 4; // Of a band
};

// Bands sorted by proto-mass at their leading edge, lightest first
std::vector<MassBand> findMassBands(std::span<const ToFEEvent> events, double tofSlope, double tofOffset,
                                    const BandSearchOptions &options = {});

// What a band is: recoils of an isotope, or the incident ions scattered from it. Recoils of an element whose isotopes
// are not resolved are one band, its identity is then the most abundant isotope, but its selection is of the element.
struct BandIdentity {
    int z = 0; // Zero when not identified
    int a = 0;
    CutType type = CutType::Recoil;
    bool isKnown() const { return z > 0; }
    bool operator==(const BandIdentity &) const = default;
};

// Atomic number and mass in kg of the particles of the band: the isotope, or the incident ion for scattered ions. Zero
// when not known.
int detectedZ(const BandIdentity &identity, const ExperimentalSettings *settings);
double detectedMass(const BandIdentity &identity, const ExperimentalSettings *settings);

// How far the ridge of a band is from the E channels the energy detector gives particles of the identity, see
// energydetector.h: the root mean square of the relative difference, weighted by the events, from the leading edge on.
// NaN when it cannot be calculated.
double bandDeviation(const MassBand &band, const BandIdentity &identity, const ExperimentalSettings &settings,
                     EnergyDetectorModel &model, const EnergyDetectorCalibration &calibration);

// How a band was identified
enum class IdentityBasis {
    None,
    Edge, // Its leading edge is where the particles from the surface are expected
    Response, // Its ridge follows the response of the energy detector to the particles
    User
};

struct BandProposal {
    BandIdentity identity;
    IdentityBasis basis = IdentityBasis::None;
    double expectedEdge = 0; // Channel of the particles from the surface, NaN when not known
    double deviation = 0; // Of the ridge from the response of the energy detector to the identity, NaN when not known
    // Index of the band this one continues at longer ToF, -1 for none. Such a band is a piece of the other, split off
    // where the ridge was lost, and has no identity or selection of its own.
    int continues = -1;
    // Below the response of the lightest particle, H: e.g. random coincidences. No identity or selection, and not one
    // of the neighbours of the selections.
    bool artifact = false;
};

struct IdentityProposal {
    std::vector<BandProposal> bands; // Of each band
    EnergyDetectorFit calibration;
};

// Identities of the bands. Those given (by the user, known identities in the list) are kept. The others are first
// identified by their leading edges, which are compared with those of the particles from the surface of all naturally
// occurring isotopes, recoiled or the incident ion scattered; the masses must grow with the proto-mass. The bands so
// identified give the calibration of the energy detector, which is then the most likely isotope for each of the other
// bands by its ridge, its leading edge (particles from the surface are more likely, none come before them) and its
// abundance, unless the band continues another one. Without enough identified bands, the calibration of the detector
// is used when it has one. Needs the experimental settings and the ToF calibration.
IdentityProposal proposeIdentities(const std::vector<MassBand> &bands, const ExperimentalSettings &settings,
                                   const DetectorSettings &detector, EnergyDetectorModel &model,
                                   const PhysicsSettings &physics, const std::vector<BandIdentity> &given = {});

// Calibration of the energy detector from the ridges of bands of known identity, the others are left out
EnergyDetectorFit calibrateFromBands(const std::vector<MassBand> &bands, const std::vector<BandIdentity> &identities,
                                     const ExperimentalSettings &settings, EnergyDetectorModel &model);

// Selections of bands that reach as deep (long ToF) as they can without significant overlap with other bands. In slices
// of ToF, the proto-mass distribution is fitted as a sum of Gaussians, one for each band, centred on their ridges, and a
// flat background. Beyond its ridge, a band follows the response of the energy detector to its particles, or its ridge
// extended straight. The
// boundary between two neighbouring bands is where their densities are equal, at most a few widths from each centre.
// A selection starts before the leading edge of its band and ends where the expected fraction of events of other bands
// in it exceeds the limit, or where the events of its band are no longer significant above the background.
struct SelectionOptions {
    double maxContamination = 0.01; // Expected fraction of events of other bands
    double sliceWidth = 40; // Channels, slices are widened until they have enough events
    size_t minSliceEvents = 300;
    double maxSigmas = 3; // A boundary is at most this many widths from the centre of its band
};

// A band in the selections
struct SelectionBand {
    const MassBand *band;
    int z = 0; // Of the detected particles, zero when not known
    double mass = 0; // kg
    bool selected = false; // A selection is made for it, otherwise it is only one of the neighbours
};

struct BandSelection {
    bool ok = false;
    QList<QPointF> polygon; // ToF and E channels
    std::vector<QPointF> lower; // ToF channel and proto-mass of the boundaries, along the selection
    std::vector<QPointF> upper;
    double startTof = 0; // Channels
    double endTof = 0;
    double events = 0; // Expected events of the band in it
    double contamination = 0; // Expected fraction of events of other bands and the background in it
    QString endReason; // Why it ends where it does
};

// Selections of the selected bands, in the order of the bands
std::vector<BandSelection> selectBands(std::span<const ToFEEvent> events, const std::vector<SelectionBand> &bands,
                                       EnergyDetectorModel &model, const EnergyDetectorCalibration &calibration,
                                       const SelectionOptions &options = {});

#endif // JARA_MASSSELECTION_H
