#ifndef JARA_DEPTHPROFILE_H
#define JARA_DEPTHPROFILE_H

#include <algorithm>
#include <vector>

#include <QList>
#include <QPointF>
#include <QString>

#include "project.h"
#include "stopping.h"
#include "tofedata.h"

// Depth profiles from single scattering (slab analysis). Each event is assumed to come from one recoil or scattering
// event at some depth, without multiple scattering. Depths are areal densities in atoms/m².
//
// The detected energy of a selection's particles is a decreasing function of the depth they come from: the beam slows
// down on the way in, the particle gets the kinematic fraction of the beam energy and slows down on the way out. Both
// depend on the composition, which is tabulated in stopping bins and calculated with Bragg's rule. Each event gets the
// depth where this function gives its energy, and a weight of 1 / (efficiency * cross section) with Rutherford cross
// sections with the Andersen screening correction at the beam energy at that depth. The weights of each element in a
// stopping bin, relative to all of them, give the composition of the next iteration. Events are shared between the two
// nearest bins, and each bin gets a number of pseudo-events with the composition of the bin before it, which keeps
// bins with few events from swinging and lets the iterations converge. The first iteration assumes the same
// composition at all depths, from the weights of all events with cross sections at the beam energy.
//
// A selection of an isotope can represent its natural element: its events are weighted by 1 / natural abundance as well.
// The natural abundance is from JIBAL unless the user has set another one. The events of a selection can also be given
// an additional weight by the user.
//
// The detected energy is the energy from the time of flight with the loss in the timing detector foil added back.
// Events with an efficiency below 1e-6, which includes energies outside the efficiency curve, are left out. The
// efficiency is that of the selection's isotope, the incident ion for scattered ions, or of the most abundant isotope
// when the selection is an element.
//
// Only the events in the range of the profile are used, see split.h. A selection can use a narrower part of it, e.g.
// hydrogen only from the beginning of the measurement before it is lost. Its events are then weighted by the reference
// counts in the profile's range / in its part, so that it stands for the same fluence as the others.
//
// With an automatic maximum depth, the stopping bins are extended when events come from deeper, and cut when the
// events end well before the maximum depth.

// Everything an iteration depends on, apart from the events. Comparing these tells whether the iterations are still
// up to date. The profile binning, normalization, averages and density only change how the results are shown.
struct DepthProfileInputs {
    struct CutInput {
        int id;
        int z;
        int a;
        CutType type;
        QList<QPointF> polygon;
        bool representsElement;
        double abundanceCorrection; // 1 / natural abundance when the isotope represents its element, otherwise 1
        double weight; // Additional weight set by the user
        // Part of the measurement used, fractions of the reference counts, the profile's range or a part of it
        double rangeMin;
        double rangeMax;
        bool operator==(const CutInput &) const = default;
    };
    bool hasSettings = false;
    int beamZ = 0;
    int beamA = 0;
    double beamEnergy = 0; // J
    double incidentAngle = 0; // rad, from the surface
    bool hasDetector = false;
    double scatteringAngle = 0; // rad
    double tofLength = 0; // m
    double tofSlope = 0; // s/ch
    double tofOffset = 0; // s
    Material foilMaterial;
    double foilThickness = 0; // kg/m²
    QList<EfficiencyCurve> efficiencies;
    PhysicsSettings physics; // Without the name, which does not matter
    double stoppingBinWidth = 0; // atoms/m²
    double maxDepth = 0; // atoms/m², where an automatic one starts
    bool autoMaxDepth = false;
    int minStoppingBinEvents = 1; // Pseudo-events from the bin before, see above
    QList<QPointF> referencePolygon; // Divides the measurement, empty when all events count, see split.h
    double rangeMin = 0; // Part of the measurement used, fractions of the reference counts
    double rangeMax = 1;
    QList<CutInput> cuts; // Included cuts in the order of the measurement's cuts
    bool operator==(const DepthProfileInputs &) const = default;
};

DepthProfileInputs depthProfileInputs(const Measurement &measurement, const DepthProfile &profile);

// Tells when an element is counted more than once: a selection of the element, or an isotope representing it, is
// included together with other selections of the same element and type. Empty when there is no such element.
QString elementOverlapWarning(const DepthProfileInputs &inputs);

// The iterations of a depth profile
class DepthProfileCalculation {
public:
    // Events of a selection with their depths and weights from the latest iteration
    struct Selection {
        int cutId = 0;
        int z = 0;
        int a = 0;
        CutType type = CutType::Recoil;
        bool representsElement = false;
        double rangeWeight = 1; // Reference counts in the profile's range / in the selection's part of it
        std::vector<double> energies; // J, detected, of the events used
        // 1 / efficiency of the events used, also / natural abundance when the isotope represents its element and times
        // the additional weight of the selection and its range weight
        std::vector<double> efficiencyWeights;
        std::vector<double> depths; // atoms/m², negative above the surface, NaN beyond the maximum depth
        std::vector<double> weights; // 1 / (efficiency * cross section), zero beyond the maximum depth
        size_t events = 0; // In the selection
        size_t lowEfficiencyEvents = 0; // Left out, the efficiency is below 1e-6
        size_t tooDeepEvents = 0; // Beyond the maximum depth in the latest iteration
    };

    DepthProfileCalculation(const std::vector<ToFEEvent> &events, const DepthProfileInputs &inputs);
    bool isValid() const { return m_errorMessage.isEmpty(); }
    const QString &errorMessage() const { return m_errorMessage; }

    void iterate();
    int iterations() const { return m_iterations; }
    // Largest change of an element's atomic fraction in a stopping bin in the latest iteration, NaN before any and
    // when the maximum depth was changed
    double lastChange() const { return m_lastChange; }
    double maxDepth() const { return m_binWidth * static_cast<double>(m_bins); }

    const std::vector<Selection> &selections() const { return m_selections; }
    const QList<int> &elements() const { return m_elements; } // Atomic numbers of the composition
    double stoppingBinWidth() const { return m_binWidth; }
    // Atomic fractions of the elements in each stopping bin, used by the next iteration
    const std::vector<std::vector<double>> &composition() const { return m_composition; }
    double massDepth(double depth) const; // kg/m² from the composition, extrapolated outside the stopping bins

private:
    struct Particle {
        int z = 0;
        double mass = 0; // kg
        StoppingTable stopping; // In the elements of the composition
        std::vector<std::vector<double>> exitEnergies; // [boundary][energy grid point], see iterate()
        std::vector<double> grid; // Energies, J
    };
    struct SelectionPhysics {
        size_t particle; // Index in m_particles
        double kinematicFactor;
        double targetMass; // kg
    };

    double traverse(const Particle &particle, size_t bin, double energy, double pathLength) const;
    double exitEnergy(const Particle &particle, size_t boundary, double energy) const; // Interpolated in the table
    double crossSection(const Selection &selection, double beamEnergy) const; // m²/sr
    double beamEnergyAt(double depth) const;
    void calculateBeamEnergies();
    void calculateExitEnergies(Particle &particle);
    void initialComposition();
    bool adjustMaxDepth(); // True when the stopping bins were changed

    QString m_errorMessage;
    DepthProfileInputs m_inputs;
    double m_beamMass = 0;
    double m_binWidth = 0;
    size_t m_bins = 0;
    QList<int> m_elements;
    std::vector<std::vector<double>> m_composition; // [bin][element]
    std::vector<double> m_beamEnergies; // At the boundaries of the stopping bins
    Particle m_beam{};
    std::vector<Particle> m_particles;
    std::vector<SelectionPhysics> m_physics; // Of each selection
    std::vector<Selection> m_selections;
    int m_iterations = 0;
    double m_lastChange;
};

// The profile of a selection in depth bins. Unless the profile separates isotopes, the selections of an element with
// the same type are summed into one curve of the element.
struct DepthProfileCurve {
    int cutId = 0; // The first of the selections, e.g. for the color
    QList<int> cutIds; // All of the selections summed
    int z = 0;
    int a = 0;
    CutType type = CutType::Recoil;
    bool representsElement = false;
    // The element when the isotope represents it, otherwise as cutLabel()
    QString label(bool superscript = true) const { return cutLabel(z, representsElement ? 0 : a, type, superscript); }
    std::vector<double> concentrations; // Atomic fractions
    std::vector<double> uncertainties; // Statistical, atomic fractions, including the normalization
    std::vector<size_t> counts; // Events
    size_t events = 0; // In the profile
    // True when all of the selections are in the list, e.g. the ones left out of the averages
    bool allIn(const QList<int> &ids) const {
        return std::all_of(cutIds.begin(), cutIds.end(), [&ids](int id) { return ids.contains(id); });
    }
};

struct DepthProfileResult {
    double firstBinStart = 0; // atoms/m², bins have the width of the profile settings
    double binWidth = 0;
    std::vector<DepthProfileCurve> curves; // All have the same number of bins
    std::vector<double> totals; // Sum of the concentrations in each bin
    double normalizationMin = 0; // atoms/m², the interval used, also when found automatically
    double normalizationMax = 0;
    double averageMin = 0; // atoms/m², the averaging interval snapped to the bins
    double averageMax = 0;
    struct Average {
        double concentration = 0; // Atomic fraction, as in the profile
        double uncertainty = 0; // Statistical, including the normalization
        double renormalized = 0; // Among the selections not left out, NaN when left out
        double renormalizedUncertainty = 0; // Statistical, NaN when left out
        size_t counts = 0;
    };
    std::vector<Average> averages; // Of each curve over the averaging interval
    QString errorMessage; // Empty when the profile could be made
    size_t bins() const { return totals.size(); }
    double binStart(size_t bin) const { return firstBinStart + static_cast<double>(bin) * binWidth; }
};

// Moves the ends of a depth interval to the nearest edges of the profile bins, which all are at multiples of the bin
// width. The interval includes the bins between the edges, at least one: when both ends meet, the bin after them.
void snapToBins(double &min, double &max, double binWidth);

DepthProfileResult binDepthProfile(const DepthProfileCalculation &calculation, const DepthProfile &profile);

#endif // JARA_DEPTHPROFILE_H
