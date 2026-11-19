#ifndef JARA_TOFCALIBRATION_H
#define JARA_TOFCALIBRATION_H

#include <vector>

#include <QList>
#include <QPointF>
#include <QString>

#include "project.h"
#include "tofedata.h"

// ToF calibration from the leading edges of ToF spectra. The edge of a selection is the short ToF (high energy) side of
// its spectrum, made by particles from the sample surface. Their energy follows from the kinematics of the incident
// ion and the target atom, without energy loss in the sample. They lose energy in the foil of the first timing
// detector, their time of flight follows from the energy after it and the ToF length. A straight line through the edge
// channels and the expected times of flight of several selections gives the calibration t = slope * channel + offset.

// Counts of the selection's events in each ToF channel. Bin i has the events in channel i, ToF channels are integers.
// Events at negative channels are left out.
std::vector<size_t> tofHistogram(const std::vector<ToFEEvent> &events, const QList<QPointF> &polygon);

// Energy in J of the selection's particles from the sample surface: the recoil energy for recoils (ERD), the energy of
// the incident ion after scattering for scattered ions (RBS). When the incident ion is heavier than the target atom,
// the higher of the two energies is used. NaN with an error message when it cannot be calculated.
double surfaceEnergy(const ExperimentalSettings *settings, const DetectorSettings &detector, const Cut &cut,
                     QString &errorMessage);

// The selection's particles from the sample surface
struct ExpectedTof {
    double energy = 0; // J, at the surface
    double foilLoss = 0; // J, in the foil of the first timing detector
    double tof = 0; // s
    QString errorMessage; // Empty when the above could be calculated
    bool isValid() const { return errorMessage.isEmpty(); }
};

ExpectedTof expectedSurfaceTof(const ExperimentalSettings *settings, const DetectorSettings &detector,
                               const PhysicsSettings &physics, const Cut &cut);

// Fit of counts = background + amplitude * Phi((channel - mu) / sigma) to the leading edge, Phi being the cumulative
// distribution function of the standard normal distribution. The fit uses the channels from windowMin to windowMax,
// both included.
struct EdgeFit {
    double mu = 0; // Channel
    double muError = 0; // Standard error of mu
    double sigma = 0; // Channels
    double amplitude = 0; // Counts
    double background = 0; // Counts
    int windowMin = 0;
    int windowMax = 0;
    bool ok = false;
    QString errorMessage; // When not ok
    double value(double channel) const; // The fitted function
};

// Guess of the edge and a fit window around it. The edge is where the counts first reach half of the level they rise
// to. Not ok when there are no counts.
EdgeFit findEdge(const std::vector<size_t> &counts);

// Fits the edge within the window of the initial guess, starting from its values. Counts are weighted as Poisson
// distributed.
EdgeFit fitEdge(const std::vector<size_t> &counts, const EdgeFit &initial);

// Guess and fit in one
EdgeFit findAndFitEdge(const std::vector<size_t> &counts);

// Straight line t = slope * channel + offset through calibration points (channel, t in s), least squares
struct LinearFit {
    double slope = 0; // s/ch
    double offset = 0; // s
    bool ok = false;
    QString errorMessage; // When not ok
};

// Needs at least two points with different channels, and gives a positive slope
LinearFit fitLine(const QList<QPointF> &points);

#endif // JARA_TOFCALIBRATION_H
