#include "stopping.h"
#include "elements.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

#include <QStringList>

extern "C" {
#include <jibal.h>
#include <jibal_layer.h>
#include <jibal_stop.h>
}

namespace {
constexpr int TablePoints = 400;
constexpr double TableMinEnergy = 1 * Units::Kiloelectronvolt;
constexpr int MaxTableExtensions = 50;

// Combinations of the incident atomic number and the target atomic number with stopping loaded
std::set<std::pair<int, int>> loadedStopping;

bool hasStoppingData(jibal_gsto *gsto, int z1, int z2) {
    const gsto_file_t *file = jibal_gsto_get_assigned_file(gsto, GSTO_STO_ELE, z1, z2);
    return file && file->data && jibal_gsto_file_get_data(file, z1, z2);
}

QList<int> elementsOf(const Material &material) {
    QList<int> elements;
    for(const MaterialElement &element : material.elements) {
        elements.append(element.z);
    }
    return elements;
}

// The material as a JIBAL formula, e.g. "C0.95 H0.05"
QByteArray formula(const Material &material) {
    QStringList parts;
    for(const MaterialElement &element : material.elements) {
        parts.append(QString("%1%2").arg(Elements::symbol(element.z)).arg(element.fraction, 0, 'g', 17));
    }
    return parts.join(' ').toLatin1();
}
}

bool loadStopping(int z, const QList<int> &targets, QString &errorMessage) {
    jibal *library = Elements::library();
    if(!library) {
        errorMessage = QStringLiteral("JIBAL is not initialized.");
        return false;
    }
    QList<int> missing;
    for(const int target : targets) {
        if(!loadedStopping.contains({z, target}) && !missing.contains(target)) {
            missing.append(target);
        }
    }
    if(missing.isEmpty()) {
        return true;
    }
    QStringList unavailable;
    for(const int target : missing) {
        if(!jibal_gsto_auto_assign(library->gsto, z, target)) {
            unavailable.append(Elements::symbol(target));
        }
    }
    if(unavailable.isEmpty()) {
        jibal_gsto_load_all(library->gsto);
        for(const int target : missing) {
            if(!hasStoppingData(library->gsto, z, target)) {
                unavailable.append(Elements::symbol(target));
            }
        }
    }
    if(!unavailable.isEmpty()) {
        errorMessage = QString("JIBAL has no stopping data for %1 in %2.").arg(Elements::symbol(z), unavailable.join(", "));
        return false;
    }
    for(const int target : missing) {
        loadedStopping.insert({z, target});
    }
    return true;
}

double stoppingCrossSection(int z, double mass, int target, double energy) {
    const double electronic = jibal_gsto_get_em(Elements::library()->gsto, GSTO_STO_ELE, z, target, energy / mass);
    const double nuclear = jibal_gsto_stop_nuclear_universal(energy, z, mass, target, Elements::mass(target, 0));
    return electronic + nuclear;
}

struct FoilEnergyLoss::Layer {
    jibal_isotope particle{};
    jibal_layer *layer = nullptr;
    ~Layer() { jibal_layer_free(layer); } // Also frees the material
};

FoilEnergyLoss::FoilEnergyLoss(int z, double mass, const Material &material, double thickness) {
    if(thickness <= 0) {
        return;
    }
    if(!isValidMaterial(material)) {
        m_errorMessage = QStringLiteral("The concentrations of the foil material do not sum up to 100 %.");
        return;
    }
    if(!loadStopping(z, elementsOf(material), m_errorMessage)) {
        return;
    }
    jibal_material *jibalMaterial = jibal_material_create(Elements::library()->elements, formula(material).constData());
    if(!jibalMaterial) {
        m_errorMessage = QString("JIBAL did not accept the foil material %1.").arg(materialText(material));
        return;
    }
    m_layer = std::make_unique<Layer>();
    m_layer->particle.Z = z;
    m_layer->particle.mass = mass;
    m_layer->layer = jibal_layer_new(jibalMaterial, thickness / averageAtomMass(material)); // Atoms per m²
}

FoilEnergyLoss::~FoilEnergyLoss() = default;

double FoilEnergyLoss::energyAfter(double energy) const {
    if(!m_layer) {
        return energy;
    }
    const double after = jibal_layer_energy_loss(Elements::library()->gsto, &m_layer->particle, m_layer->layer, energy, -1.0);
    return std::max(0.0, after);
}

void FoilEnergyLoss::tabulate(double maxEnergyAfter) {
    m_before.clear();
    m_after.clear();
    if(!m_layer) {
        return;
    }
    // The table reaches energies before the foil that give at least the highest energy after it
    const double target = std::max(maxEnergyAfter, 2 * TableMinEnergy) * 1.05;
    double maxEnergy = 1.1 * target + 100 * Units::Kiloelectronvolt;
    for(int i = 0; i < MaxTableExtensions && energyAfter(maxEnergy) < target; i++) {
        maxEnergy *= 1.5;
    }
    // Evenly spaced on a logarithmic scale
    const double ratio = std::pow(maxEnergy / TableMinEnergy, 1.0 / (TablePoints - 1));
    double energy = TableMinEnergy;
    for(int i = 0; i < TablePoints; i++, energy *= ratio) {
        m_before.push_back(energy);
        // Never decreasing, so that the table can be searched
        m_after.push_back(std::max(energyAfter(energy), m_after.empty() ? 0.0 : m_after.back()));
    }
}

double FoilEnergyLoss::energyBefore(double energyAfter) const {
    if(!m_layer) {
        return energyAfter;
    }
    if(m_after.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if(energyAfter >= m_after.back()) {
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
