#ifndef JARA_STOPPING_H
#define JARA_STOPPING_H

#include <memory>
#include <vector>

#include <QList>
#include <QString>

#include "project.h"

// Loads the stopping of a particle (atomic number z) in the target elements when it is not loaded yet. False with an
// error message when JIBAL has no stopping data for some of them.
bool loadStopping(int z, const QList<int> &targets, QString &errorMessage);

// Stopping cross section in J m² of a particle (atomic number z, mass in kg) with the energy in J in an element,
// electronic and nuclear. The stopping must have been loaded.
double stoppingCrossSection(int z, double mass, int target, double energy);

// Energy loss of a particle in a foil, with stopping from JIBAL. Stopping data is loaded when a combination of the
// particle and an element of the foil is first needed.
class FoilEnergyLoss {
public:
    // Particle with atomic number z and mass in kg, foil thickness as areal density in kg/m². A foil without thickness
    // causes no loss.
    FoilEnergyLoss(int z, double mass, const Material &material, double thickness);
    ~FoilEnergyLoss();
    FoilEnergyLoss(const FoilEnergyLoss &) = delete;
    FoilEnergyLoss &operator=(const FoilEnergyLoss &) = delete;

    bool isValid() const { return m_errorMessage.isEmpty(); }
    const QString &errorMessage() const { return m_errorMessage; } // Why it is not valid
    double energyAfter(double energy) const; // J, zero when the particle stops in the foil

    // Energy before the foil from the energy after it, interpolated in a table. Call tabulate() first with the
    // highest energy after the foil that is needed. Higher energies are extrapolated with the loss at the top of the
    // table.
    void tabulate(double maxEnergyAfter);
    double energyBefore(double energyAfter) const;

private:
    struct Layer; // JIBAL layer and the particle
    QString m_errorMessage;
    std::unique_ptr<Layer> m_layer; // nullptr when there is no loss
    std::vector<double> m_before; // Energies before the foil, increasing
    std::vector<double> m_after; // Energies after the foil of the above, increasing
};

#endif // JARA_STOPPING_H
