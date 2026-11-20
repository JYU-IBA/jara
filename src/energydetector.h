#ifndef JARA_ENERGYDETECTOR_H
#define JARA_ENERGYDETECTOR_H

#include <map>
#include <utility>
#include <vector>

#include "project.h"
#include "stopping.h"

// Response of the energy detector, a gas ionization chamber. A particle reaches it with the energy from its time of
// flight, loses energy in the foil of the stop timing detector and in the entrance window, and gives the rest to the
// gas. Only the part lost in electronic stopping ionizes, the part lost in nuclear stopping does not (the larger part
// of the pulse height defect, larger for heavier and slower ions). The W-value of the gas also depends on the ion, it is
// taken linear in Z with a fitted slope:
//
//     E channel = offset + gain × (1 + zSlope × (Z - 6)) × ionizing energy(window thickness × windowScale)
//
// The window and the dead layer behind it are often thicker in effect than the window itself, so the thickness of the
// window is scaled by a fitted factor. The energy after a layer is found by the difference of ranges. The ionizing energy of each element is tabulated against the energy after the
// first timing foil, with its average mass. Stopping depends on the velocity, so a particle of another mass m has the
// ionizing energy (m / m0) × ionizing(E × m0 / m) of the table; nuclear stopping does not scale quite so, but closely.

class EnergyDetectorModel {
public:
    EnergyDetectorModel(const DetectorSettings &detector, const PhysicsSettings &physics);

    // Ionizing energy in J of a particle of atomic number z and mass in kg with the energy after the first timing foil,
    // the window scaled by the factor. Zero when it stops before the gas, NaN when the stopping cannot be calculated.
    double ionizingEnergy(int z, double mass, double energy, double windowScale);

    // E channel of a particle at the ToF channel, NaN when not known
    double energyChannel(const EnergyDetectorCalibration &calibration, int z, double mass, double tofChannel);

    const QString &errorMessage() const { return m_errorMessage; } // Of the last particle that could not be tabulated
    const DetectorSettings &detector() const { return m_detector; }

private:
    struct Particle {
        bool valid = false;
        std::vector<double> stopFoilRange; // Ranges in the stop foil and the window at the energies of the grid, atoms/m²
        std::vector<double> windowRange;
        std::vector<double> nuclearLoss; // In the gas when stopping from the energies of the grid
        std::vector<double> nuclearFraction; // Of the stopping in the gas at the energies of the grid
        double windowScale = -1; // Of the table below
        std::vector<double> ionizing; // At the energies of the grid
    };
    Particle &particle(int z, double mass, double windowScale);
    double ionizingInGas(const Particle &particle, double energy) const;
    double energyAfter(const std::vector<double> &range, double energy, double path) const; // By the range difference

    DetectorSettings m_detector;
    PhysicsSettings m_physics;
    std::vector<double> m_grid; // Energies after the first timing foil, J, evenly on a logarithmic scale
    std::vector<double> m_stopFoilFractions;
    std::vector<double> m_windowFractions;
    double m_stopFoilPath = 0; // atoms/m²
    double m_windowPath = 0;
    std::map<int, Particle> m_particles; // By Z, with the average mass of the element
    QString m_errorMessage;
};

// Fits the calibration to points of particles of known Z and mass: E channel at a ToF channel, with weights. The slope
// in Z is fitted only with particles of three different Z, the offset and the window scale only with two different
// particles.
struct EnergyDetectorPoint {
    int z;
    double mass; // kg
    double tofChannel;
    double energyChannel;
    double weight;
};

struct EnergyDetectorFit {
    EnergyDetectorCalibration calibration;
    double rmsResidual = 0; // Relative, of the E channels
    int particles = 0; // Different particles of the points
    int minZ = 0; // Of the points, the response is less certain beyond them
    int maxZ = 0;
    bool ok = false;
    QString errorMessage;
};

EnergyDetectorFit fitEnergyDetector(EnergyDetectorModel &model, const std::vector<EnergyDetectorPoint> &points);

// "Gain 622 ch/MeV, offset 21 ch, W-value slope 0.50 %/Z, window × 2.15"
QString energyCalibrationText(const EnergyDetectorCalibration &calibration);

#endif // JARA_ENERGYDETECTOR_H
