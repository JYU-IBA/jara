#include "energydetector.h"
#include "elements.h"
#include "energyspectrum.h"
#include "stopping.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <gsl/gsl_multifit.h>

namespace {
constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
constexpr double GridMin = 1 * Units::Kiloelectronvolt;
constexpr double GridMax = 500 * Units::Megaelectronvolt;
constexpr int GridPoints = 300;
constexpr int ReferenceZ = 6; // Of the slope of the W-value
constexpr double MinWindowScale = 0.25;
constexpr double MaxWindowScale = 8;
}

EnergyDetectorModel::EnergyDetectorModel(const DetectorSettings &detector, const PhysicsSettings &physics)
    : m_detector(detector), m_physics(physics) {
    const double step = std::pow(GridMax / GridMin, 1.0 / (GridPoints - 1));
    double energy = GridMin;
    for(int i = 0; i < GridPoints; i++, energy *= step) {
        m_grid.push_back(energy);
    }
    for(const MaterialElement &element : detector.stopFoilMaterial.elements) {
        m_stopFoilFractions.push_back(element.fraction);
    }
    for(const MaterialElement &element : detector.windowMaterial.elements) {
        m_windowFractions.push_back(element.fraction);
    }
    const double stopFoilAtom = averageAtomMass(detector.stopFoilMaterial);
    const double windowAtom = averageAtomMass(detector.windowMaterial);
    m_stopFoilPath = stopFoilAtom > 0 ? detector.stopFoilThickness / stopFoilAtom : 0;
    m_windowPath = windowAtom > 0 ? detector.windowThickness / windowAtom : 0;
}

namespace {
QList<int> elementsOf(const Material &material) {
    QList<int> elements;
    for(const MaterialElement &element : material.elements) {
        elements.append(element.z);
    }
    return elements;
}

// Linear interpolation in a table on the logarithmic grid
double interpolateGrid(const std::vector<double> &grid, const std::vector<double> &values, double energy) {
    const double position = std::log(energy / grid.front()) / std::log(grid[1] / grid[0]);
    const auto i = std::min(static_cast<size_t>(std::max(0.0, position)), grid.size() - 2);
    const double f = std::min(1.0, position - static_cast<double>(i));
    return values[i] + f * (values[i + 1] - values[i]);
}
}

double EnergyDetectorModel::ionizingInGas(const Particle &particle, double energy) const {
    if(energy <= m_grid.front()) {
        return energy * (1 - particle.nuclearFraction.front());
    }
    return energy - interpolateGrid(m_grid, particle.nuclearLoss, energy);
}

double EnergyDetectorModel::energyAfter(const std::vector<double> &range, double energy, double path) const {
    if(path <= 0 || energy <= 0) {
        return energy;
    }
    // The range left, and the energy that has it
    const double left = interpolateGrid(m_grid, range, energy) - path;
    if(left <= 0) {
        return 0;
    }
    if(left <= range.front()) {
        const double ratio = left / range.front();
        return m_grid.front() * ratio * ratio; // Range proportional to the velocity below the grid
    }
    const auto upper = std::lower_bound(range.begin(), range.end(), left);
    if(upper == range.end()) {
        return m_grid.back();
    }
    const auto i = static_cast<size_t>(upper - range.begin());
    const double f = (left - range[i - 1]) / (range[i] - range[i - 1]);
    return m_grid[i - 1] * std::pow(m_grid[i] / m_grid[i - 1], f);
}

EnergyDetectorModel::Particle &EnergyDetectorModel::particle(int z, double mass, double windowScale) {
    Particle &particle = m_particles[z];
    if(particle.windowScale < 0) {
        // Stopping, once for each particle
        const StoppingTable stopFoil(z, mass, elementsOf(m_detector.stopFoilMaterial), m_physics);
        const StoppingTable window(z, mass, elementsOf(m_detector.windowMaterial), m_physics);
        const QList<int> gas = elementsOf(m_detector.gasMaterial);
        std::vector<double> gasFractions;
        for(const MaterialElement &element : m_detector.gasMaterial.elements) {
            gasFractions.push_back(element.fraction);
        }
        PhysicsSettings electronicOnly = m_physics;
        electronicOnly.nuclearFactor = 0;
        PhysicsSettings nuclearOnly = m_physics;
        nuclearOnly.electronicFactor = 0;
        const StoppingTable electronic(z, mass, gas, electronicOnly);
        const StoppingTable nuclear(z, mass, gas, nuclearOnly);
        const std::vector<const StoppingTable *> tables{&stopFoil, &window, &electronic, &nuclear};
        for(const StoppingTable *table : tables) {
            if(!table->isValid()) {
                m_errorMessage = table->errorMessage();
                particle.windowScale = 0;
                return particle;
            }
        }
        // Ranges, the stopping below the grid taken proportional to the velocity
        auto ranges = [this](const StoppingTable &table, const std::vector<double> &fractions) {
            std::vector<double> range(m_grid.size());
            double before = table.stopping(fractions, m_grid[0]);
            range[0] = 2 * m_grid[0] / before;
            for(size_t i = 1; i < m_grid.size(); i++) {
                const double stopping = table.stopping(fractions, m_grid[i]);
                range[i] = range[i - 1] + 0.5 * (1 / before + 1 / stopping) * (m_grid[i] - m_grid[i - 1]);
                before = stopping;
            }
            return range;
        };
        particle.stopFoilRange = ranges(stopFoil, m_stopFoilFractions);
        particle.windowRange = ranges(window, m_windowFractions);
        // Energy lost in nuclear stopping in the gas by a particle stopping from each energy of the grid, integrated
        // over the fraction of the stopping that is nuclear
        for(const double energy : m_grid) {
            const double e = electronic.stopping(gasFractions, energy);
            const double n = nuclear.stopping(gasFractions, energy);
            particle.nuclearFraction.push_back(e + n > 0 ? n / (e + n) : 0);
        }
        particle.nuclearLoss.assign(m_grid.size(), 0);
        particle.nuclearLoss[0] = particle.nuclearFraction[0] * m_grid[0];
        for(size_t i = 1; i < m_grid.size(); i++) {
            particle.nuclearLoss[i] = particle.nuclearLoss[i - 1]
                    + 0.5 * (particle.nuclearFraction[i - 1] + particle.nuclearFraction[i]) * (m_grid[i] - m_grid[i - 1]);
        }
        particle.valid = true;
    }
    if(particle.valid && particle.windowScale != windowScale) {
        particle.windowScale = windowScale;
        particle.ionizing.resize(m_grid.size());
        for(size_t i = 0; i < m_grid.size(); i++) {
            double energy = energyAfter(particle.stopFoilRange, m_grid[i], m_stopFoilPath);
            energy = energyAfter(particle.windowRange, energy, m_windowPath * windowScale);
            particle.ionizing[i] = energy > 0 ? ionizingInGas(particle, energy) : 0;
        }
    }
    return particle;
}

double EnergyDetectorModel::ionizingEnergy(int z, double mass, double energy, double windowScale) {
    const double elementMass = Elements::mass(z, 0);
    if(elementMass <= 0 || mass <= 0) {
        return NaN;
    }
    const Particle &p = particle(z, elementMass, windowScale);
    if(!p.valid || !(energy > 0)) {
        return NaN;
    }
    // The energy of the element at the same velocity, and back to the mass
    const double scale = mass / elementMass;
    const double scaled = energy / scale;
    if(scaled <= m_grid.front()) {
        return p.ionizing.front() * energy / m_grid.front();
    }
    return scale * interpolateGrid(m_grid, p.ionizing, scaled);
}

double EnergyDetectorModel::energyChannel(const EnergyDetectorCalibration &calibration, int z, double mass,
                                          double tofChannel) {
    const double energy = tofToEnergy(tofChannel, mass, m_detector.tofLength, m_detector.tofSlope, m_detector.tofOffset);
    const double ionizing = ionizingEnergy(z, mass, energy, calibration.windowScale);
    if(!calibration.valid || !std::isfinite(ionizing)) {
        return NaN;
    }
    return calibration.offset + calibration.gain * (1 + calibration.zSlope * (z - ReferenceZ)) * ionizing;
}

namespace {
// Linear fit with the window scale fixed
EnergyDetectorFit fitLinear(EnergyDetectorModel &model, const std::vector<EnergyDetectorPoint> &points, double windowScale) {
    EnergyDetectorFit fit;
    struct Row {
        double ionizing;
        int z;
        double channel;
        double weight;
    };
    std::vector<Row> rows;
    std::vector<int> zs;
    std::vector<std::pair<int, double>> particles;
    const DetectorSettings &detector = model.detector();
    for(const EnergyDetectorPoint &point : points) {
        const double energy = tofToEnergy(point.tofChannel, point.mass, detector.tofLength, detector.tofSlope, detector.tofOffset);
        const double ionizing = model.ionizingEnergy(point.z, point.mass, energy, windowScale);
        if(!(ionizing > 0) || !(point.weight > 0) || !(point.energyChannel > 0)) {
            continue;
        }
        rows.push_back({ionizing, point.z, point.energyChannel, point.weight});
        if(std::find(zs.begin(), zs.end(), point.z) == zs.end()) {
            zs.push_back(point.z);
        }
        const auto particle = std::make_pair(point.z, point.mass);
        if(std::find(particles.begin(), particles.end(), particle) == particles.end()) {
            particles.push_back(particle);
        }
    }
    if(rows.size() < 3) {
        fit.errorMessage = QStringLiteral("Too few points on identified bands.");
        return fit;
    }
    // E channel = c0 + c1 × ionizing + c2 × ionizing × (Z - 6), the offset and the slope in Z only when there are
    // particles enough to tell them apart
    std::vector<int> columns{1};
    if(particles.size() >= 2) {
        columns.insert(columns.begin(), 0);
    }
    if(zs.size() >= 3) {
        columns.push_back(2);
    }
    const size_t n = rows.size();
    const size_t p = columns.size();
    gsl_matrix *x = gsl_matrix_alloc(n, p);
    gsl_vector *y = gsl_vector_alloc(n);
    gsl_vector *w = gsl_vector_alloc(n);
    gsl_vector *c = gsl_vector_alloc(p);
    gsl_matrix *covariance = gsl_matrix_alloc(p, p);
    for(size_t i = 0; i < n; i++) {
        for(size_t k = 0; k < p; k++) {
            const double value = columns[k] == 0 ? 1 : columns[k] == 1 ? rows[i].ionizing : rows[i].ionizing * (rows[i].z - ReferenceZ);
            gsl_matrix_set(x, i, k, value);
        }
        gsl_vector_set(y, i, rows[i].channel);
        // Weighted by the events, relative to the channel so that all energies count alike
        gsl_vector_set(w, i, rows[i].weight / (rows[i].channel * rows[i].channel));
    }
    gsl_error_handler_t *handler = gsl_set_error_handler_off();
    gsl_multifit_linear_workspace *workspace = gsl_multifit_linear_alloc(n, p);
    double chiSquared = 0;
    const int status = gsl_multifit_wlinear(x, w, y, c, covariance, &chiSquared, workspace);
    double coefficients[3] = {0, 0, 0};
    for(size_t k = 0; k < p; k++) {
        coefficients[columns[k]] = gsl_vector_get(c, k);
    }
    gsl_multifit_linear_free(workspace);
    gsl_matrix_free(covariance);
    gsl_vector_free(c);
    gsl_vector_free(w);
    gsl_vector_free(y);
    gsl_matrix_free(x);
    gsl_set_error_handler(handler);
    if(status != GSL_SUCCESS || !(coefficients[1] > 0)) {
        fit.errorMessage = status != GSL_SUCCESS ? QString("The fit failed: %1.").arg(gsl_strerror(status))
                                                 : QStringLiteral("The fitted gain is not positive.");
        return fit;
    }
    fit.calibration.valid = true;
    fit.calibration.offset = coefficients[0];
    fit.calibration.gain = coefficients[1];
    fit.calibration.zSlope = coefficients[2] / coefficients[1];
    fit.calibration.windowScale = windowScale;
    fit.particles = static_cast<int>(particles.size());
    fit.minZ = *std::min_element(zs.begin(), zs.end());
    fit.maxZ = *std::max_element(zs.begin(), zs.end());
    double sum = 0;
    double weights = 0;
    for(const Row &row : rows) {
        const double predicted = fit.calibration.offset
                + fit.calibration.gain * (1 + fit.calibration.zSlope * (row.z - ReferenceZ)) * row.ionizing;
        sum += row.weight * std::pow(predicted / row.channel - 1, 2);
        weights += row.weight;
    }
    fit.rmsResidual = std::sqrt(sum / weights);
    fit.ok = true;
    return fit;
}
}

EnergyDetectorFit fitEnergyDetector(EnergyDetectorModel &model, const std::vector<EnergyDetectorPoint> &points) {
    EnergyDetectorFit best = fitLinear(model, points, 1);
    if(!best.ok || best.particles < 2) {
        return best;
    }
    // The window scale by golden section search on a logarithmic scale
    constexpr double Ratio = 0.6180339887498949;
    double low = std::log(MinWindowScale);
    double high = std::log(MaxWindowScale);
    auto rms = [&](double logScale) {
        const EnergyDetectorFit fit = fitLinear(model, points, std::exp(logScale));
        if(fit.ok && fit.rmsResidual < best.rmsResidual) {
            best = fit;
        }
        return fit.ok ? fit.rmsResidual : std::numeric_limits<double>::max();
    };
    double a = high - Ratio * (high - low);
    double b = low + Ratio * (high - low);
    double fa = rms(a);
    double fb = rms(b);
    for(int i = 0; i < 20; i++) {
        if(fa < fb) {
            high = b;
            b = a;
            fb = fa;
            a = high - Ratio * (high - low);
            fa = rms(a);
        } else {
            low = a;
            a = b;
            fa = fb;
            b = low + Ratio * (high - low);
            fb = rms(b);
        }
    }
    return best;
}

QString energyCalibrationText(const EnergyDetectorCalibration &calibration) {
    return QString("Gain %1 ch/MeV, offset %2 ch, W-value slope %3 %/Z, window × %4")
            .arg(calibration.gain * Units::Megaelectronvolt, 0, 'f', 1)
            .arg(calibration.offset, 0, 'f', 1)
            .arg(100 * calibration.zSlope, 0, 'f', 2)
            .arg(calibration.windowScale, 0, 'f', 2);
}
