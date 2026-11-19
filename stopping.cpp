#include "stopping.h"
#include "elements.h"

#include <algorithm>
#include <cmath>
#include <limits>

extern "C" {
#include <jibal.h>
#include <jibal_stop.h>
}

namespace {
constexpr double StoppingMinEnergy = 1 * Units::Kiloelectronvolt; // Of the stopping tables
constexpr double StoppingMaxEnergy = 2000 * Units::Megaelectronvolt;
constexpr int StoppingPoints = 1500;
constexpr double MaxRelativeLossPerStep = 0.02; // In slowing down
constexpr int FoilTablePoints = 400;
constexpr double FoilTableMinEnergy = 1 * Units::Kiloelectronvolt;
constexpr int MaxFoilTableExtensions = 50;

double stoppingLogStep() {
    return std::log(StoppingMaxEnergy / StoppingMinEnergy) / (StoppingPoints - 1);
}

// JIBAL's own choice of the electronic stopping file of a pair: an assignment from its configuration, otherwise the
// first file with the pair. jibal_gsto_auto_assign() would keep a file chosen for the pair earlier instead.
gsto_file_t *automaticFile(jibal_gsto *gsto, int z1, int z2) {
    if(gsto->overrides) {
        for(gsto_assignment *assignment = gsto->overrides; assignment->file; assignment++) {
            if(assignment->Z1 == z1 && assignment->Z2 == z2 && assignment->file->type == GSTO_STO_ELE) {
                return assignment->file;
            }
        }
    }
    for(size_t i = 0; i < gsto->n_files; i++) {
        gsto_file_t *file = &gsto->files[i];
        if(file->type == GSTO_STO_ELE && jibal_gsto_file_has_combination(file, z1, z2)) {
            return file;
        }
    }
    return nullptr;
}

bool hasData(const gsto_file_t *file, int z1, int z2) {
    return file->data && jibal_gsto_file_get_data(file, z1, z2);
}

// Assigns the file of the pair from the physics settings to it in JIBAL and loads its data when needed. The
// assignment lasts until another pair of settings assigns another file, so the stopping is read right after this.
bool assignStopping(int z1, int z2, const PhysicsSettings &physics, QString &errorMessage) {
    jibal_gsto *gsto = Elements::library()->gsto;
    const StoppingCorrection *correction = physics.correctionFor(z1, z2);
    const QString name = correction && !correction->stoppingFile.isEmpty() ? correction->stoppingFile : physics.stoppingFile;
    gsto_file_t *file = nullptr;
    if(name.isEmpty()) {
        file = automaticFile(gsto, z1, z2);
        if(!file) {
            errorMessage = QString("JIBAL has no stopping data for %1 in %2.").arg(Elements::symbol(z1), Elements::symbol(z2));
            return false;
        }
    } else {
        file = jibal_gsto_get_file(gsto, name.toUtf8().constData());
        if(!file || file->type != GSTO_STO_ELE) {
            errorMessage = QString("JIBAL has no electronic stopping file %1.").arg(name);
            return false;
        }
        if(!jibal_gsto_file_has_combination(file, z1, z2)) {
            errorMessage = QString("The stopping file %1 has no stopping of %2 in %3.").arg(name, Elements::symbol(z1), Elements::symbol(z2));
            return false;
        }
    }
    if(!jibal_gsto_assign(gsto, z1, z2, file)) {
        errorMessage = QString("JIBAL could not use the stopping file %1 for %2 in %3.")
                               .arg(QString::fromUtf8(file->name), Elements::symbol(z1), Elements::symbol(z2));
        return false;
    }
    if(!hasData(file, z1, z2)) {
        jibal_gsto_load(gsto, 0, file); // Reads the data of all pairs assigned to the file
    }
    if(!hasData(file, z1, z2)) {
        errorMessage = QString("JIBAL could not read the stopping of %1 in %2 from %3.")
                               .arg(Elements::symbol(z1), Elements::symbol(z2), QString::fromUtf8(file->name));
        return false;
    }
    return true;
}
}

QStringList stoppingFileNames() {
    QStringList names;
    if(const jibal *library = Elements::library()) {
        for(size_t i = 0; i < library->gsto->n_files; i++) {
            const gsto_file_t &file = library->gsto->files[i];
            if(file.type == GSTO_STO_ELE) {
                names.append(QString::fromUtf8(file.name));
            }
        }
    }
    return names;
}

QStringList stoppingFileNames(int z1, int z2) {
    QStringList names;
    if(const jibal *library = Elements::library()) {
        for(size_t i = 0; i < library->gsto->n_files; i++) {
            gsto_file_t *file = &library->gsto->files[i];
            // With any target, the file must have the incident element
            const bool has = z2 == AnyElement ? file->Z1_min == JIBAL_ANY_Z || (z1 >= file->Z1_min && z1 <= file->Z1_max)
                                              : jibal_gsto_file_has_combination(file, z1, z2);
            if(file->type == GSTO_STO_ELE && has) {
                names.append(QString::fromUtf8(file->name));
            }
        }
    }
    return names;
}

StoppingTable::StoppingTable(int z, double mass, const QList<int> &elements, const PhysicsSettings &physics)
    : m_elements(elements) {
    if(!Elements::library()) {
        m_errorMessage = QStringLiteral("JIBAL is not initialized.");
        return;
    }
    jibal_gsto *gsto = Elements::library()->gsto;
    const double logStep = stoppingLogStep();
    for(const int element : elements) {
        if(!assignStopping(z, element, physics, m_errorMessage)) {
            m_values.clear();
            return;
        }
        const StoppingCorrection *correction = physics.correctionFor(z, element);
        const double electronicFactor = physics.electronicFactor * (correction ? correction->electronicFactor : 1);
        const double nuclearFactor = physics.nuclearFactor * (correction ? correction->nuclearFactor : 1);
        const double targetMass = Elements::mass(element, 0);
        std::vector<double> &values = m_values.emplace_back(StoppingPoints);
        for(int k = 0; k < StoppingPoints; k++) {
            const double energy = StoppingMinEnergy * std::exp(k * logStep);
            const double electronic = jibal_gsto_get_em(gsto, GSTO_STO_ELE, z, element, energy / mass);
            const double nuclear = jibal_gsto_stop_nuclear_universal(energy, z, mass, element, targetMass);
            values[static_cast<size_t>(k)] = electronicFactor * electronic + nuclearFactor * nuclear;
        }
    }
}

double StoppingTable::stopping(const std::vector<double> &fractions, double energy) const {
    const double position = std::log(energy / StoppingMinEnergy) / stoppingLogStep();
    size_t i = 0;
    double t = 0;
    double scale = 1;
    if(position < 0) {
        scale = std::sqrt(energy / StoppingMinEnergy);
    } else if(position >= StoppingPoints - 1) {
        i = StoppingPoints - 2;
        t = 1;
    } else {
        i = static_cast<size_t>(position);
        t = position - static_cast<double>(i);
    }
    double sum = 0;
    for(size_t e = 0; e < fractions.size() && e < m_values.size(); e++) {
        if(fractions[e] > 0) {
            const std::vector<double> &values = m_values[e];
            sum += fractions[e] * (values[i] + t * (values[i + 1] - values[i]));
        }
    }
    return sum * scale;
}

double slowDown(const StoppingTable &table, const std::vector<double> &fractions, double energy, double pathLength,
                int maxSteps) {
    // Runge-Kutta, with steps short enough that the particle loses at most a few per cent of its energy in each
    if(!(energy > 0)) {
        return 0;
    }
    const double initialLoss = table.stopping(fractions, energy) * pathLength;
    const int steps = std::clamp(static_cast<int>(std::ceil(initialLoss / (MaxRelativeLossPerStep * energy))), 1, maxSteps);
    const double h = pathLength / steps;
    double e = energy;
    for(int i = 0; i < steps; i++) {
        const double k1 = -table.stopping(fractions, e);
        const double e2 = e + h / 2 * k1;
        if(!(e2 > 0)) {
            return 0;
        }
        const double k2 = -table.stopping(fractions, e2);
        const double e3 = e + h / 2 * k2;
        if(!(e3 > 0)) {
            return 0;
        }
        const double k3 = -table.stopping(fractions, e3);
        const double e4 = e + h * k3;
        if(!(e4 > 0)) {
            return 0;
        }
        const double k4 = -table.stopping(fractions, e4);
        e += h / 6 * (k1 + 2 * k2 + 2 * k3 + k4);
        if(!(e > 0)) {
            return 0;
        }
    }
    return e;
}

FoilEnergyLoss::FoilEnergyLoss(int z, double mass, const Material &material, double thickness,
                               const PhysicsSettings &physics) {
    if(thickness <= 0) {
        return;
    }
    if(!isValidMaterial(material)) {
        m_errorMessage = QStringLiteral("The concentrations of the foil material do not sum up to 100 %.");
        return;
    }
    QList<int> elements;
    for(const MaterialElement &element : material.elements) {
        elements.append(element.z);
        m_fractions.push_back(element.fraction);
    }
    m_table = StoppingTable(z, mass, elements, physics);
    if(!m_table.isValid()) {
        m_errorMessage = m_table.errorMessage();
        return;
    }
    m_pathLength = thickness / averageAtomMass(material); // Atoms per m²
    m_loss = true;
}

double FoilEnergyLoss::energyAfter(double energy) const {
    return m_loss ? slowDown(m_table, m_fractions, energy, m_pathLength) : energy;
}

void FoilEnergyLoss::tabulate(double maxEnergyAfter) {
    m_before.clear();
    m_after.clear();
    if(!m_loss) {
        return;
    }
    // The table reaches energies before the foil that give at least the highest energy after it
    const double target = std::max(maxEnergyAfter, 2 * FoilTableMinEnergy) * 1.05;
    double maxEnergy = 1.1 * target + 100 * Units::Kiloelectronvolt;
    for(int i = 0; i < MaxFoilTableExtensions && energyAfter(maxEnergy) < target; i++) {
        maxEnergy *= 1.5;
    }
    // Evenly spaced on a logarithmic scale
    const double ratio = std::pow(maxEnergy / FoilTableMinEnergy, 1.0 / (FoilTablePoints - 1));
    double energy = FoilTableMinEnergy;
    for(int i = 0; i < FoilTablePoints; i++, energy *= ratio) {
        m_before.push_back(energy);
        // Never decreasing, so that the table can be searched
        m_after.push_back(std::max(energyAfter(energy), m_after.empty() ? 0.0 : m_after.back()));
    }
}

double FoilEnergyLoss::energyBefore(double energyAfter) const {
    if(!m_loss) {
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
