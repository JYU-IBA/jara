#include "depthprofile.h"
#include "elements.h"
#include "energyspectrum.h"
#include "stopping.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

#include <QStringList>

extern "C" {
#include <jibal_cross_section.h>
}

namespace {
constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
constexpr double MinEfficiency = 1e-6;
constexpr size_t MaxStoppingBins = 20000;
constexpr size_t MaxProfileBins = 1000000;
constexpr double AutoDepthMargin = 0.1; // Of the deepest event, left after it with an automatic maximum depth
constexpr int AutoDepthMarginBins = 5; // Stopping bins also left after it
constexpr double AutoDepthCutLevel = 0.8; // The bins are cut when fewer than this fraction of them are needed
constexpr int MaxBinsAboveSurface = 100; // Events higher above the surface are left out of the profile
constexpr int GridPoints = 250; // Of the energy grid of the exit energy tables, evenly spaced on a logarithmic scale
constexpr double GridRange = 100; // Highest energy of the grid divided by the lowest
constexpr int MaxSteps = 50; // Of slowing down, particles losing more stop or nearly, the exact energy hardly matters
constexpr int AutoNormalizationSmoothing = 2; // Bins on each side in the moving average of the total
constexpr double AutoNormalizationTolerance = 0.1; // Relative difference of the total from its typical level allowed
constexpr double AutoNormalizationDeviations = 3; // Statistical deviations allowed, when more than the above

jibal_isotope isotope(int z, int a, double mass) {
    jibal_isotope result{};
    result.Z = z;
    result.A = a;
    result.mass = mass;
    return result;
}
}

DepthProfileInputs depthProfileInputs(const Measurement &measurement, const DepthProfile &profile) {
    DepthProfileInputs inputs;
    if(const ExperimentalSettings *settings = measurement.settings) {
        inputs.hasSettings = true;
        inputs.beamZ = settings->beamZ;
        inputs.beamA = settings->beamA;
        inputs.beamEnergy = settings->beamEnergy;
        inputs.incidentAngle = settings->incidentAngle;
    }
    if(const DetectorSettings *detector = measurement.detector) {
        inputs.hasDetector = true;
        inputs.scatteringAngle = detector->scatteringAngle;
        inputs.tofLength = detector->tofLength;
        inputs.tofSlope = detector->tofSlope;
        inputs.tofOffset = detector->tofOffset;
        inputs.foilMaterial = detector->foilMaterial;
        inputs.foilThickness = detector->foilThickness;
        inputs.efficiencies = detector->efficiencies;
    }
    inputs.physics = measurement.physicsOrDefault();
    inputs.physics.name.clear();
    inputs.stoppingBinWidth = profile.stoppingBinWidth;
    inputs.maxDepth = profile.maxDepth;
    inputs.autoMaxDepth = profile.autoMaxDepth;
    inputs.minStoppingBinEvents = profile.minStoppingBinEvents;
    for(const Cut &cut : measurement.cuts) {
        if(profile.cutIds.contains(cut.id)) {
            inputs.cuts.append({cut.id, cut.z, cut.a, cut.type, cut.polygon, cut.representsElement,
                                abundanceCorrection(cut), cut.weight});
        }
    }
    return inputs;
}

QString elementOverlapWarning(const DepthProfileInputs &inputs) {
    QStringList warnings;
    for(qsizetype i = 0; i < inputs.cuts.size(); i++) {
        const auto &first = inputs.cuts[i];
        auto sameElement = [&first](const DepthProfileInputs::CutInput &cut) {
            return cut.z == first.z && cut.type == first.type;
        };
        if(std::any_of(inputs.cuts.begin(), inputs.cuts.begin() + i, sameElement)) {
            continue; // Already checked with the first selection of the element
        }
        QStringList representing; // Selections standing for the whole element
        qsizetype count = 0;
        for(qsizetype j = i; j < inputs.cuts.size(); j++) {
            const auto &cut = inputs.cuts[j];
            if(!sameElement(cut)) {
                continue;
            }
            count++;
            if(cut.a == 0) {
                representing.append(QStringLiteral("the selection of the element"));
            } else if(cut.representsElement) {
                representing.append(cutLabel(cut.z, cut.a, cut.type) + QStringLiteral(" representing the element"));
            }
        }
        if(count > 1 && !representing.isEmpty()) {
            warnings.append(QString("%1 is counted more than once: %2 already includes all of its isotopes, but other "
                                    "selections of it are included as well.")
                                    .arg(cutLabel(first.z, 0, first.type), representing.join(QStringLiteral(" and "))));
        }
    }
    return warnings.join(QLatin1Char(' '));
}

DepthProfileCalculation::DepthProfileCalculation(const std::vector<ToFEEvent> &events, const DepthProfileInputs &inputs)
    : m_inputs(inputs), m_lastChange(NaN) {
    if(!inputs.hasSettings || inputs.beamZ <= 0 || inputs.beamEnergy <= 0) {
        m_errorMessage = QStringLiteral("The experimental settings of the measurement with the incident ion and the beam "
                                        "energy are needed.");
        return;
    }
    if(!inputs.hasDetector || inputs.tofLength <= 0 || inputs.tofSlope <= 0) {
        m_errorMessage = QStringLiteral("The detector settings of the measurement with the ToF length and calibration are "
                                        "needed.");
        return;
    }
    const double exitAngle = inputs.scatteringAngle - inputs.incidentAngle;
    if(inputs.incidentAngle <= 0 || exitAngle <= 0) {
        m_errorMessage = QStringLiteral("The incident and exit angles must be above the sample surface.");
        return;
    }
    if(inputs.cuts.isEmpty()) {
        m_errorMessage = QStringLiteral("Include some selections in the depth profile.");
        return;
    }
    if(inputs.stoppingBinWidth <= 0 || inputs.maxDepth <= 0) {
        m_errorMessage = QStringLiteral("The stopping bin width and the maximum depth must be positive.");
        return;
    }
    m_binWidth = inputs.stoppingBinWidth;
    m_bins = static_cast<size_t>(std::ceil(inputs.maxDepth / m_binWidth - 1e-9));
    if(m_bins > MaxStoppingBins) {
        m_errorMessage = QString("There would be more than %1 stopping bins, make them wider.").arg(MaxStoppingBins);
        return;
    }
    m_beamMass = Elements::mass(inputs.beamZ, inputs.beamA);
    if(m_beamMass <= 0) {
        m_errorMessage = QString("The mass of %1 is not known.").arg(Elements::label(inputs.beamZ, inputs.beamA));
        return;
    }
    m_beam.z = inputs.beamZ;
    m_beam.mass = m_beamMass;

    // Particles leaving the sample: recoils, and the incident ions for scattered ions
    const double angle = inputs.scatteringAngle;
    for(const auto &cut : inputs.cuts) {
        if(!m_elements.contains(cut.z)) {
            m_elements.append(cut.z);
        }
        const double targetMass = Elements::mass(cut.z, cut.a);
        if(targetMass <= 0) {
            m_errorMessage = QString("The mass of %1 is not known.").arg(Elements::label(cut.z, cut.a));
            return;
        }
        const bool scattered = cut.type == CutType::Scattered;
        const int z = scattered ? inputs.beamZ : cut.z;
        const double mass = scattered ? m_beamMass : targetMass;
        SelectionPhysics physics{};
        physics.targetMass = targetMass;
        const double m1 = m_beamMass;
        const double m2 = targetMass;
        if(scattered) {
            const double root = m2 * m2 - m1 * m1 * std::sin(angle) * std::sin(angle);
            if(root < 0) {
                m_errorMessage = QString("%1 cannot scatter from %2 to the detector angle.")
                                         .arg(Elements::label(inputs.beamZ, inputs.beamA), Elements::label(cut.z, cut.a));
                return;
            }
            const double k = (std::sqrt(root) + m1 * std::cos(angle)) / (m1 + m2);
            physics.kinematicFactor = k * k;
        } else {
            if(angle >= 90 * Units::Degree) {
                m_errorMessage = QStringLiteral("Recoils cannot reach a recoil angle of 90° or more.");
                return;
            }
            physics.kinematicFactor = 4 * m1 * m2 * std::cos(angle) * std::cos(angle) / ((m1 + m2) * (m1 + m2));
        }
        auto same = [z, mass](const Particle &particle) { return particle.z == z && particle.mass == mass; };
        auto found = std::find_if(m_particles.begin(), m_particles.end(), same);
        if(found == m_particles.end()) {
            Particle particle;
            particle.z = z;
            particle.mass = mass;
            m_particles.push_back(particle);
            found = m_particles.end() - 1;
        }
        physics.particle = static_cast<size_t>(found - m_particles.begin());
        m_physics.push_back(physics);
    }
    m_beam.stopping = StoppingTable(m_beam.z, m_beam.mass, m_elements, inputs.physics);
    if(!m_beam.stopping.isValid()) {
        m_errorMessage = m_beam.stopping.errorMessage();
        return;
    }
    for(Particle &particle : m_particles) {
        particle.stopping = StoppingTable(particle.z, particle.mass, m_elements, inputs.physics);
        if(!particle.stopping.isValid()) {
            m_errorMessage = particle.stopping.errorMessage();
            return;
        }
    }

    // Energy grids of the exit energy tables, up to the highest energy the particles can get
    for(size_t i = 0; i < m_particles.size(); i++) {
        double maxEnergy = 0;
        for(size_t s = 0; s < m_physics.size(); s++) {
            if(m_physics[s].particle == i) {
                maxEnergy = std::max(maxEnergy, m_physics[s].kinematicFactor * inputs.beamEnergy);
            }
        }
        maxEnergy *= 1.01;
        Particle &particle = m_particles[i];
        const double ratio = std::pow(GridRange, 1.0 / (GridPoints - 1));
        double energy = maxEnergy / GridRange;
        for(int k = 0; k < GridPoints; k++, energy *= ratio) {
            particle.grid.push_back(energy);
        }
    }

    // Detected energies and efficiencies of the events. The time of flight is spread within its channel, as in energy
    // spectra.
    for(qsizetype s = 0; s < inputs.cuts.size(); s++) {
        const auto &cut = inputs.cuts[s];
        const Particle &particle = m_particles[m_physics[static_cast<size_t>(s)].particle];
        Selection selection;
        selection.cutId = cut.id;
        selection.z = cut.z;
        selection.a = cut.a;
        selection.type = cut.type;
        selection.representsElement = cut.representsElement;
        // An isotope representing its element stands for all of its isotopes
        if(!std::isfinite(cut.abundanceCorrection) || cut.abundanceCorrection <= 0) {
            m_errorMessage = QString("%1 does not occur naturally, it cannot represent the element.")
                                     .arg(Elements::label(cut.z, cut.a));
            return;
        }
        std::mt19937 random(static_cast<std::mt19937::result_type>(cut.id));
        std::uniform_real_distribution<double> dither(-0.5, 0.5);
        std::vector<double> energies;
        double maxEnergy = 0;
        forEachEventIn(events, cut.polygon, [&](const ToFEEvent &event) {
            selection.events++;
            const double energy = tofToEnergy(event.tof + dither(random), particle.mass, inputs.tofLength,
                                              inputs.tofSlope, inputs.tofOffset);
            if(std::isnan(energy)) {
                selection.lowEfficiencyEvents++; // No energy, as if not detected
                return;
            }
            energies.push_back(energy);
            maxEnergy = std::max(maxEnergy, energy);
        });
        const bool scattered = cut.type == CutType::Scattered;
        const EfficiencyCurve *curve = findEfficiency(inputs.efficiencies, scattered ? inputs.beamZ : cut.z,
                                                      scattered ? inputs.beamA : cut.a);
        FoilEnergyLoss foil(particle.z, particle.mass, inputs.foilMaterial, inputs.foilThickness, inputs.physics);
        if(!foil.isValid()) {
            m_errorMessage = QString("Energy loss of %1 in the timing detector foil: %2")
                                     .arg(cutLabel(cut.z, cut.a, cut.type, false), foil.errorMessage());
            return;
        }
        foil.tabulate(maxEnergy);
        for(const double energyAfterFoil : energies) {
            const double energy = foil.energyBefore(energyAfterFoil);
            const double eff = curve ? efficiency(*curve, energy) : 1;
            if(eff < MinEfficiency) {
                selection.lowEfficiencyEvents++;
                continue;
            }
            selection.energies.push_back(energy);
            selection.efficiencyWeights.push_back(cut.weight * cut.abundanceCorrection / eff);
        }
        selection.depths.assign(selection.energies.size(), 0);
        selection.weights.assign(selection.energies.size(), 0);
        m_selections.push_back(std::move(selection));
    }
    initialComposition();
}

double DepthProfileCalculation::crossSection(const Selection &selection, double beamEnergy) const {
    const SelectionPhysics &physics = m_physics[static_cast<size_t>(&selection - m_selections.data())];
    const jibal_isotope beam = isotope(m_inputs.beamZ, m_inputs.beamA, m_beamMass);
    const jibal_isotope target = isotope(selection.z, selection.a, physics.targetMass);
    const jibal_cross_section_type type = m_inputs.physics.screening == ScreeningModel::Andersen ? JIBAL_CS_ANDERSEN
                                                                                                : JIBAL_CS_RUTHERFORD;
    if(selection.type == CutType::Scattered) {
        return jibal_cross_section_rbs(&beam, &target, m_inputs.scatteringAngle, beamEnergy, type);
    }
    return jibal_cross_section_erd(&beam, &target, m_inputs.scatteringAngle, beamEnergy, type);
}

void DepthProfileCalculation::initialComposition() {
    // The same at all depths, from the weights of all events with cross sections at the beam energy
    std::vector<double> weights(static_cast<size_t>(m_elements.size()), 0);
    for(const Selection &selection : m_selections) {
        const double sigma = crossSection(selection, m_inputs.beamEnergy);
        double sum = 0;
        for(const double weight : selection.efficiencyWeights) {
            sum += weight;
        }
        weights[static_cast<size_t>(m_elements.indexOf(selection.z))] += sum / sigma;
    }
    double total = 0;
    for(const double weight : weights) {
        total += weight;
    }
    if(total <= 0) {
        m_errorMessage = QStringLiteral("The selections have no events.");
        return;
    }
    for(double &weight : weights) {
        weight /= total;
    }
    m_composition.assign(m_bins, weights);
}

double DepthProfileCalculation::traverse(const Particle &particle, size_t bin, double energy, double pathLength) const {
    return slowDown(particle.stopping, m_composition[bin], energy, pathLength, MaxSteps);
}

double DepthProfileCalculation::exitEnergy(const Particle &particle, size_t boundary, double energy) const {
    // Cubic interpolation in the logarithm of the energy, the grid is evenly spaced in it
    const std::vector<double> &grid = particle.grid;
    const std::vector<double> &table = particle.exitEnergies[boundary];
    if(!(energy > 0)) {
        return 0;
    }
    if(energy <= grid.front()) {
        return table.front() * energy / grid.front(); // Slow particles, hardly matters
    }
    const double position = std::log(energy / grid.front()) / std::log(grid[1] / grid.front());
    const auto last = static_cast<int>(grid.size()) - 1;
    if(position >= last) {
        return table.back() + (energy - grid.back()) * (table[last] - table[last - 1]) / (grid[last] - grid[last - 1]);
    }
    const int i = std::clamp(static_cast<int>(std::floor(position)) - 1, 0, last - 3);
    const double t = position - i;
    // Lagrange polynomial through the points i ... i + 3, t from 0 to 3
    const double y0 = table[i], y1 = table[i + 1], y2 = table[i + 2], y3 = table[i + 3];
    return y0 * (t - 1) * (t - 2) * (t - 3) / -6 + y1 * t * (t - 2) * (t - 3) / 2 + y2 * t * (t - 1) * (t - 3) / -2
           + y3 * t * (t - 1) * (t - 2) / 6;
}

void DepthProfileCalculation::calculateBeamEnergies() {
    const double path = m_binWidth / std::sin(m_inputs.incidentAngle);
    m_beamEnergies.assign(m_bins + 1, 0);
    m_beamEnergies[0] = m_inputs.beamEnergy;
    for(size_t j = 0; j < m_bins; j++) {
        m_beamEnergies[j + 1] = traverse(m_beam, j, m_beamEnergies[j], path);
    }
}

void DepthProfileCalculation::calculateExitEnergies(Particle &particle) {
    // Energy at the surface of a particle that starts from a boundary with a grid energy. From the boundary j + 1 the
    // particle crosses the bin j to the boundary j, then continues from there as tabulated for it.
    const double path = m_binWidth / std::sin(m_inputs.scatteringAngle - m_inputs.incidentAngle);
    particle.exitEnergies.assign(m_bins + 1, {});
    particle.exitEnergies[0] = particle.grid;
    for(size_t j = 0; j < m_bins; j++) {
        std::vector<double> &next = particle.exitEnergies[j + 1];
        next.resize(particle.grid.size());
        for(size_t k = 0; k < particle.grid.size(); k++) {
            next[k] = exitEnergy(particle, j, traverse(particle, j, particle.grid[k], path));
        }
    }
}

double DepthProfileCalculation::beamEnergyAt(double depth) const {
    if(depth <= 0) {
        return m_beamEnergies.front();
    }
    const double position = depth / m_binWidth;
    const auto j = static_cast<size_t>(position);
    if(j >= m_bins) {
        return m_beamEnergies.back();
    }
    const double t = position - static_cast<double>(j);
    return m_beamEnergies[j] + t * (m_beamEnergies[j + 1] - m_beamEnergies[j]);
}

void DepthProfileCalculation::iterate() {
    if(!isValid()) {
        return;
    }
    calculateBeamEnergies();
    for(Particle &particle : m_particles) {
        calculateExitEnergies(particle);
    }

    // Depths and weights of the events. Each event is shared between the two stopping bins with the nearest centres,
    // in proportion to its distance from them, so that the composition changes smoothly when events move.
    const size_t elementCount = static_cast<size_t>(m_elements.size());
    std::vector<std::vector<double>> binWeights(m_bins, std::vector<double>(elementCount, 0));
    std::vector<double> binCounts(m_bins, 0);
    std::vector<double> allWeights(elementCount, 0);
    for(size_t s = 0; s < m_selections.size(); s++) {
        Selection &selection = m_selections[s];
        const SelectionPhysics &physics = m_physics[s];
        const Particle &particle = m_particles[physics.particle];
        // Detected energy of particles from each boundary, decreasing with depth
        std::vector<double> detected(m_bins + 1);
        for(size_t j = 0; j <= m_bins; j++) {
            detected[j] = exitEnergy(particle, j, physics.kinematicFactor * m_beamEnergies[j]);
            if(j > 0) {
                detected[j] = std::min(detected[j], detected[j - 1]);
            }
        }
        const auto element = static_cast<size_t>(m_elements.indexOf(selection.z));
        selection.tooDeepEvents = 0;
        for(size_t i = 0; i < selection.energies.size(); i++) {
            const double energy = selection.energies[i];
            double depth;
            if(energy <= detected[m_bins] || detected[0] <= 0) {
                selection.depths[i] = NaN;
                selection.weights[i] = 0;
                selection.tooDeepEvents++;
                continue;
            }
            if(energy >= detected[0]) {
                // Above the surface, from the depth resolution. The slope of the first bin is extended.
                const double slope = (detected[0] - detected[1]) / m_binWidth;
                depth = slope > 0 ? -(energy - detected[0]) / slope : 0;
            } else {
                // The first boundary below the energy, the one before it is above or at it
                const auto below = std::upper_bound(detected.begin(), detected.end(), energy, std::greater<>());
                const auto j = static_cast<size_t>(below - detected.begin()) - 1;
                const double drop = detected[j] - detected[j + 1];
                depth = (static_cast<double>(j) + (drop > 0 ? (detected[j] - energy) / drop : 0)) * m_binWidth;
            }
            const double sigma = crossSection(selection, beamEnergyAt(depth));
            const double weight = sigma > 0 ? selection.efficiencyWeights[i] / sigma : 0;
            selection.depths[i] = depth;
            selection.weights[i] = weight;
            const double centres = std::clamp(depth / m_binWidth - 0.5, 0.0, static_cast<double>(m_bins - 1));
            const auto lower = static_cast<size_t>(centres);
            const double upperShare = centres - static_cast<double>(lower);
            binWeights[lower][element] += (1 - upperShare) * weight;
            binCounts[lower] += 1 - upperShare;
            if(upperShare > 0) {
                binWeights[lower + 1][element] += upperShare * weight;
                binCounts[lower + 1] += upperShare;
            }
            allWeights[element] += weight;
        }
    }

    // Composition of the next iteration. Each bin gets the minimum number of events as if they had the composition of
    // the bin before it, or of all events for the first bin. Bins with few events follow the bins before them, bins
    // with many are hardly affected.
    double allTotal = 0;
    for(const double weight : allWeights) {
        allTotal += weight;
    }
    if(allTotal <= 0) {
        m_lastChange = 0; // No events at any depth, the composition stays
        m_iterations++;
        return;
    }
    std::vector<double> previous(elementCount);
    for(size_t e = 0; e < elementCount; e++) {
        previous[e] = allWeights[e] / allTotal;
    }
    const double pseudoEvents = std::max(1, m_inputs.minStoppingBinEvents);
    std::vector<std::vector<double>> composition(m_bins, std::vector<double>(elementCount, 0));
    double change = 0;
    for(size_t j = 0; j < m_bins; j++) {
        double total = 0;
        for(const double weight : binWeights[j]) {
            total += weight;
        }
        for(size_t e = 0; e < elementCount; e++) {
            const double own = total > 0 ? binWeights[j][e] / total : 0;
            composition[j][e] = (binCounts[j] * own + pseudoEvents * previous[e]) / (binCounts[j] + pseudoEvents);
            change = std::max(change, std::abs(composition[j][e] - m_composition[j][e]));
        }
        previous = composition[j];
    }
    m_lastChange = change;
    m_composition = std::move(composition);
    if(m_inputs.autoMaxDepth && adjustMaxDepth()) {
        m_lastChange = NaN; // Not converged, the next iteration uses other bins
    }
    m_iterations++;
}

bool DepthProfileCalculation::adjustMaxDepth() {
    size_t tooDeep = 0;
    double deepest = 0;
    for(const Selection &selection : m_selections) {
        tooDeep += selection.tooDeepEvents;
        for(const double depth : selection.depths) {
            if(!std::isnan(depth)) {
                deepest = std::max(deepest, depth);
            }
        }
    }
    size_t bins = m_bins;
    if(tooDeep > 0 && m_beamEnergies.back() > 0) {
        // Events come from deeper, as far as the beam reaches
        bins = std::min(MaxStoppingBins, m_bins + std::max<size_t>(1, m_bins / 2));
    } else if(tooDeep == 0) {
        // Some room after the deepest events, the bins are cut when much more is left
        const double needed = deepest * (1 + AutoDepthMargin) + AutoDepthMarginBins * m_binWidth;
        const auto neededBins = static_cast<size_t>(std::ceil(needed / m_binWidth));
        if(neededBins < m_bins * AutoDepthCutLevel) {
            bins = std::max<size_t>(1, neededBins);
        }
    }
    if(bins == m_bins) {
        return false;
    }
    m_composition.resize(bins, m_composition.back()); // New bins continue the composition of the last one
    m_bins = bins;
    return true;
}

double DepthProfileCalculation::massDepth(double depth) const {
    auto averageMass = [this](size_t bin) {
        double mass = 0;
        for(qsizetype e = 0; e < m_elements.size(); e++) {
            mass += m_composition[bin][static_cast<size_t>(e)] * Elements::mass(m_elements[e], 0);
        }
        return mass;
    };
    if(m_bins == 0 || m_composition.empty()) {
        return 0;
    }
    if(depth <= 0) {
        return depth * averageMass(0);
    }
    double mass = 0;
    double remaining = depth;
    for(size_t j = 0; j < m_bins && remaining > 0; j++) {
        const double width = j + 1 < m_bins ? std::min(remaining, m_binWidth) : remaining; // The last one continues
        mass += width * averageMass(j);
        remaining -= width;
    }
    return mass;
}

void snapToBins(double &min, double &max, double binWidth) {
    if(min > max) {
        std::swap(min, max);
    }
    min = std::round(min / binWidth) * binWidth;
    max = std::max(min + binWidth, std::round(max / binWidth) * binWidth);
}

DepthProfileResult binDepthProfile(const DepthProfileCalculation &calculation, const DepthProfile &profile) {
    DepthProfileResult result;
    const double width = profile.binWidth;
    result.binWidth = width;
    if(width <= 0) {
        result.errorMessage = QStringLiteral("The bin width of the profile must be positive.");
        return result;
    }
    // From the highest event above the surface, at most a number of bins, to the maximum depth
    double minDepth = 0;
    for(const auto &selection : calculation.selections()) {
        for(const double depth : selection.depths) {
            if(!std::isnan(depth)) {
                minDepth = std::min(minDepth, depth);
            }
        }
    }
    const double maxDepth = calculation.stoppingBinWidth() * static_cast<double>(calculation.composition().size());
    const double firstBin = std::max(-static_cast<double>(MaxBinsAboveSurface), std::floor(minDepth / width));
    const double binCount = std::ceil(maxDepth / width) - firstBin;
    if(binCount > static_cast<double>(MaxProfileBins)) {
        result.errorMessage = QString("There would be more than %1 bins in the profile, make them wider.").arg(MaxProfileBins);
        return result;
    }
    const auto bins = static_cast<size_t>(std::max(1.0, binCount));
    result.firstBinStart = firstBin * width;

    // Sums of the weights of the events and of their squares, the latter are the variances of the former
    std::vector<std::vector<double>> weights;
    std::vector<std::vector<double>> variances;
    for(const auto &selection : calculation.selections()) {
        DepthProfileCurve curve;
        curve.cutId = selection.cutId;
        curve.cutIds = {selection.cutId};
        curve.z = selection.z;
        curve.a = selection.a;
        curve.type = selection.type;
        curve.representsElement = selection.representsElement;
        curve.counts.assign(bins, 0);
        std::vector<double> &binWeights = weights.emplace_back(bins, 0);
        std::vector<double> &binVariances = variances.emplace_back(bins, 0);
        for(size_t i = 0; i < selection.depths.size(); i++) {
            const double position = std::floor((selection.depths[i] - result.firstBinStart) / width);
            if(std::isnan(position) || position < 0 || position >= static_cast<double>(bins)) {
                continue;
            }
            binWeights[static_cast<size_t>(position)] += selection.weights[i];
            binVariances[static_cast<size_t>(position)] += selection.weights[i] * selection.weights[i];
            curve.counts[static_cast<size_t>(position)]++;
            curve.events++;
        }
        result.curves.push_back(std::move(curve));
    }
    // Selections of the same element and type summed into a curve of the element. Their events are independent, so
    // the variances add up as well.
    if(!profile.separateIsotopes) {
        std::vector<DepthProfileCurve> curves;
        std::vector<std::vector<double>> curveWeights;
        std::vector<std::vector<double>> curveVariances;
        for(size_t c = 0; c < result.curves.size(); c++) {
            DepthProfileCurve &curve = result.curves[c];
            const auto same = std::find_if(curves.begin(), curves.end(), [&curve](const DepthProfileCurve &other) {
                return other.z == curve.z && other.type == curve.type;
            });
            if(same == curves.end()) {
                curves.push_back(std::move(curve));
                curveWeights.push_back(std::move(weights[c]));
                curveVariances.push_back(std::move(variances[c]));
                continue;
            }
            const auto index = static_cast<size_t>(same - curves.begin());
            same->a = 0;
            same->representsElement = false;
            same->cutIds.append(curve.cutId);
            same->events += curve.events;
            for(size_t i = 0; i < bins; i++) {
                same->counts[i] += curve.counts[i];
                curveWeights[index][i] += weights[c][i];
                curveVariances[index][i] += variances[c][i];
            }
        }
        result.curves = std::move(curves);
        weights = std::move(curveWeights);
        variances = std::move(curveVariances);
    }
    std::vector<double> totalWeights(bins, 0);
    std::vector<size_t> totalCounts(bins, 0);
    for(size_t c = 0; c < weights.size(); c++) {
        for(size_t i = 0; i < bins; i++) {
            totalWeights[i] += weights[c][i];
            totalCounts[i] += result.curves[c].counts[i];
        }
    }

    // Bins in an interval snapped to them, inclusive
    auto binsIn = [&result, bins, width](double &min, double &max) {
        snapToBins(min, max, width);
        const double first = std::max(0.0, std::round((min - result.firstBinStart) / width));
        const double last = std::min(static_cast<double>(bins), std::round((max - result.firstBinStart) / width));
        std::vector<size_t> inside;
        for(double i = first; i < last; i++) {
            inside.push_back(static_cast<size_t>(i));
        }
        return inside;
    };

    // The interval where the total is 100 % on average. Found automatically, it is the longest run of bins where the
    // moving average of the total stays near its typical level, the median weighted by the events. Edges at the
    // surface, at interfaces and at the end of the profile, smeared by the depth resolution, end the run. Sparse bins
    // hardly affect the typical level.
    std::vector<size_t> normalizationBins;
    if(profile.autoNormalization) {
        std::vector<double> smoothed(bins, 0);
        std::vector<double> smoothedCounts(bins, 0); // Events in the moving average
        for(size_t i = 0; i < bins; i++) {
            const size_t first = i >= AutoNormalizationSmoothing ? i - AutoNormalizationSmoothing : 0;
            const size_t last = std::min(bins - 1, i + AutoNormalizationSmoothing);
            for(size_t j = first; j <= last; j++) {
                smoothed[i] += totalWeights[j];
                smoothedCounts[i] += static_cast<double>(totalCounts[j]);
            }
            smoothed[i] /= static_cast<double>(last - first + 1);
        }
        std::vector<std::pair<double, double>> levels; // Smoothed total and events of the bins with events
        double allEvents = 0;
        for(size_t i = 0; i < bins; i++) {
            if(totalCounts[i] > 0) {
                levels.emplace_back(smoothed[i], static_cast<double>(totalCounts[i]));
                allEvents += static_cast<double>(totalCounts[i]);
            }
        }
        std::sort(levels.begin(), levels.end());
        double typical = 0;
        double events = 0;
        for(const auto &[level, count] : levels) {
            events += count;
            if(events >= allEvents / 2) {
                typical = level;
                break;
            }
        }
        size_t bestFirst = 0;
        size_t bestLength = 0;
        size_t runFirst = 0;
        size_t runLength = 0;
        for(size_t i = 0; i < bins; i++) {
            const double allowed = smoothedCounts[i] > 0
                    ? std::max(AutoNormalizationTolerance * typical, AutoNormalizationDeviations * smoothed[i] / std::sqrt(smoothedCounts[i]))
                    : 0;
            if(totalCounts[i] > 0 && std::abs(smoothed[i] - typical) <= allowed) {
                if(runLength == 0) {
                    runFirst = i;
                }
                runLength++;
                if(runLength > bestLength) {
                    bestFirst = runFirst;
                    bestLength = runLength;
                }
            } else {
                runLength = 0;
            }
        }
        for(size_t i = bestFirst; i < bestFirst + bestLength; i++) {
            normalizationBins.push_back(i);
        }
    } else {
        double min = profile.normalizationMin;
        double max = profile.normalizationMax;
        normalizationBins = binsIn(min, max);
    }
    double scale = 0;
    for(const size_t i : normalizationBins) {
        scale += totalWeights[i];
    }
    if(normalizationBins.empty() || scale <= 0) {
        result.errorMessage = QStringLiteral("There are no events in the normalization interval.");
        return result;
    }
    scale /= static_cast<double>(normalizationBins.size());
    result.normalizationMin = result.binStart(normalizationBins.front());
    result.normalizationMax = result.binStart(normalizationBins.back()) + width;

    // Statistical uncertainties come from the variances of the summed weights, propagated through the normalization.
    // The scale is the average total in the normalization interval, so bins in it are correlated with the scale: a
    // single selection normalized over the interval it is averaged over has no uncertainty.
    const auto normalizationCount = static_cast<double>(normalizationBins.size());
    std::vector<bool> inNormalization(bins, false);
    double normalizationVariance = 0; // Of the sum of the weights of all selections in the normalization bins
    for(const size_t i : normalizationBins) {
        inNormalization[i] = true;
        for(const auto &binVariances : variances) {
            normalizationVariance += binVariances[i];
        }
    }
    result.totals.assign(bins, 0);
    for(size_t c = 0; c < result.curves.size(); c++) {
        DepthProfileCurve &curve = result.curves[c];
        curve.concentrations.resize(bins);
        curve.uncertainties.resize(bins);
        for(size_t i = 0; i < bins; i++) {
            const double concentration = weights[c][i] / scale;
            // c = W / S: the bin's own weight enters both, the other normalization bins only the scale
            const double share = inNormalization[i] ? concentration / normalizationCount : 0;
            const double variance = variances[c][i] * ((1 - share) * (1 - share) - share * share)
                                    + concentration * concentration * normalizationVariance / (normalizationCount * normalizationCount);
            curve.concentrations[i] = concentration;
            curve.uncertainties[i] = std::sqrt(std::max(0.0, variance)) / scale;
            result.totals[i] += concentration;
        }
    }

    // Averages over the averaging interval: the mean of the selection's weights divided by the scale
    result.averageMin = profile.averageMin;
    result.averageMax = profile.averageMax;
    const std::vector<size_t> averageBins = binsIn(result.averageMin, result.averageMax);
    const auto averageCount = static_cast<double>(averageBins.size());
    std::vector<bool> inAverage(bins, false);
    for(const size_t i : averageBins) {
        inAverage[i] = true;
    }
    std::vector<double> averageVariances(result.curves.size(), 0); // Of the mean weights over the averaging interval
    for(size_t c = 0; c < result.curves.size(); c++) {
        DepthProfileResult::Average average;
        for(const size_t i : averageBins) {
            average.concentration += result.curves[c].concentrations[i];
            average.counts += result.curves[c].counts[i];
            averageVariances[c] += variances[c][i];
        }
        if(!averageBins.empty()) {
            average.concentration /= averageCount;
            averageVariances[c] /= averageCount * averageCount;
            // Every weight in either interval affects the average: d average / d W(j, k), with the variances of W
            double variance = 0;
            for(size_t j = 0; j < result.curves.size(); j++) {
                for(size_t i = 0; i < bins; i++) {
                    if(!inAverage[i] && !inNormalization[i]) {
                        continue;
                    }
                    const double derivative = ((j == c && inAverage[i]) ? 1 / averageCount : 0)
                                              - (inNormalization[i] ? average.concentration / normalizationCount : 0);
                    variance += derivative * derivative * variances[j][i];
                }
            }
            average.uncertainty = std::sqrt(variance) / scale;
        }
        result.averages.push_back(average);
    }

    // Renormalized to 100 % among the selections that are not left out, r = A / T where A is the mean weight of the
    // selection and T the sum of them over the included selections. The scale cancels out.
    double includedSum = 0;
    for(size_t c = 0; c < result.curves.size(); c++) {
        if(!result.curves[c].allIn(profile.cutIdsExcludedFromAverage)) {
            includedSum += result.averages[c].concentration * scale;
        }
    }
    for(size_t c = 0; c < result.curves.size(); c++) {
        DepthProfileResult::Average &average = result.averages[c];
        if(includedSum <= 0) {
            average.renormalized = NaN;
            average.renormalizedUncertainty = NaN;
            continue;
        }
        const double renormalized = average.concentration * scale / includedSum;
        double variance = 0;
        for(size_t j = 0; j < result.curves.size(); j++) {
            const bool included = !result.curves[j].allIn(profile.cutIdsExcludedFromAverage);
            const double derivative = (j == c ? 1.0 : 0.0) - (included ? renormalized : 0.0); // Times 1 / T
            variance += derivative * derivative * averageVariances[j];
        }
        // Left out selections have no renormalized value
        const bool excluded = result.curves[c].allIn(profile.cutIdsExcludedFromAverage);
        average.renormalized = excluded ? NaN : renormalized;
        average.renormalizedUncertainty = excluded ? NaN : std::sqrt(variance) / includedSum;
    }
    return result;
}
