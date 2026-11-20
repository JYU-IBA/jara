#include "massselection.h"
#include "elements.h"
#include "tofcalibration.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <gsl/gsl_multifit.h>

namespace {
constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
constexpr double OutlierFraction = 0.001; // Of the events on each side, left out of the ranges
constexpr double Prominence = 4; // Standard deviations of the smoothed counts a peak rises above its valleys
constexpr double FwhmPerSigma = 2.3548;
constexpr double EdgeWidths = 2; // Events within this many widths of the ridge are used for the leading edge
constexpr double EdgeSearchSlices = 8; // The leading edge is looked for from this many slices before the ridge starts

struct Point {
    double tof;
    double protoMass;
};

double quantile(std::vector<double> values, double fraction) {
    const auto index = static_cast<size_t>(fraction * static_cast<double>(values.size() - 1));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
    return values[index];
}

std::vector<double> smoothed(const std::vector<double> &counts, double sigma) {
    const int halfWidth = static_cast<int>(std::ceil(3 * sigma));
    std::vector<double> kernel(2 * halfWidth + 1);
    double sum = 0;
    for(int i = -halfWidth; i <= halfWidth; i++) {
        kernel[i + halfWidth] = std::exp(-0.5 * i * i / (sigma * sigma));
        sum += kernel[i + halfWidth];
    }
    for(double &k : kernel) {
        k /= sum;
    }
    const auto n = static_cast<int>(counts.size());
    std::vector<double> result(counts.size(), 0);
    for(int i = 0; i < n; i++) {
        for(int j = -halfWidth; j <= halfWidth; j++) {
            if(i + j >= 0 && i + j < n) {
                result[i] += counts[i + j] * kernel[j + halfWidth];
            }
        }
    }
    return result;
}

struct Peak {
    double position; // Proto-mass
    double width; // Standard deviation
    double counts;
};

// Peaks of the proto-mass distribution of a slice that rise clearly above their valleys
std::vector<Peak> findPeaks(const std::vector<double> &counts, double origin, const BandSearchOptions &options) {
    const std::vector<double> smooth = smoothed(counts, options.smoothing);
    // Noise of the smoothed counts: the kernel averages over about 2 √π σ bins
    const double averaged = 2 * std::sqrt(M_PI) * options.smoothing;
    std::vector<Peak> peaks;
    const auto n = static_cast<int>(smooth.size());
    for(int i = 1; i + 1 < n; i++) {
        if(!(smooth[i] > smooth[i - 1] && smooth[i] >= smooth[i + 1])) {
            continue;
        }
        // Valleys: the lowest points before the counts rise above this peak on each side
        int left = i;
        double leftMin = smooth[i];
        for(int j = i - 1; j >= 0 && smooth[j] <= smooth[i]; j--) {
            if(smooth[j] < leftMin) {
                leftMin = smooth[j];
                left = j;
            }
        }
        int right = i;
        double rightMin = smooth[i];
        for(int j = i + 1; j < n && smooth[j] <= smooth[i]; j++) {
            if(smooth[j] < rightMin) {
                rightMin = smooth[j];
                right = j;
            }
        }
        const double valley = std::max(leftMin, rightMin);
        const double noise = std::sqrt(std::max(1.0, smooth[i]) / averaged);
        if(smooth[i] - valley < Prominence * noise) {
            continue;
        }
        // Events of the peak between the valleys, its position from a parabola through the top and its width from
        // the half maximum above the higher valley
        double events = 0;
        for(int j = left; j <= right; j++) {
            events += counts[j];
        }
        if(events < options.minPeakCounts) {
            continue;
        }
        const double denominator = smooth[i - 1] - 2 * smooth[i] + smooth[i + 1];
        const double shift = denominator < 0 ? std::clamp(0.5 * (smooth[i - 1] - smooth[i + 1]) / denominator, -0.5, 0.5) : 0;
        const double half = valley + (smooth[i] - valley) / 2;
        int low = i;
        while(low > left && smooth[low - 1] >= half) {
            low--;
        }
        int high = i;
        while(high < right && smooth[high + 1] >= half) {
            high++;
        }
        const double fwhm = (high - low + 1) * options.binWidth;
        const double sigma = std::sqrt(std::max(0.0, std::pow(fwhm / FwhmPerSigma, 2) - std::pow(options.smoothing * options.binWidth, 2)));
        peaks.push_back({origin + (i + 0.5 + shift) * options.binWidth, std::max(sigma, options.binWidth), events});
    }
    return peaks;
}

// Straight line through the last few points of a ridge extended to x, or from its first few points backwards
double extrapolate(const std::vector<QPointF> &ridge, double x, bool fromEnd = true) {
    constexpr size_t Points = 6;
    const size_t n = std::min(Points, ridge.size());
    if(n == 1) {
        return ridge.front().y();
    }
    const size_t first = fromEnd ? ridge.size() - n : 0;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for(size_t i = first; i < first + n; i++) {
        sx += ridge[i].x();
        sy += ridge[i].y();
        sxx += ridge[i].x() * ridge[i].x();
        sxy += ridge[i].x() * ridge[i].y();
    }
    const double count = static_cast<double>(n);
    const double denominator = count * sxx - sx * sx;
    const double slope = denominator > 0 ? (count * sxy - sx * sy) / denominator : 0;
    return (sy - slope * sx) / count + slope * x;
}

struct Track {
    std::vector<QPointF> ridge;
    std::vector<double> widths;
    std::vector<double> counts;
    int lastSlice = 0;
    double predicted(double tof) const { return extrapolate(ridge, tof); }
};

double interpolate(const std::vector<QPointF> &points, double x) {
    if(points.empty()) {
        return NaN;
    }
    if(x <= points.front().x()) {
        return points.front().y();
    }
    if(x >= points.back().x()) {
        return points.back().y();
    }
    const auto upper = std::lower_bound(points.begin(), points.end(), x,
                                        [](const QPointF &point, double value) { return point.x() < value; });
    const auto lower = upper - 1;
    const double f = (x - lower->x()) / (upper->x() - lower->x());
    return lower->y() + f * (upper->y() - lower->y());
}
}

namespace {
constexpr double ReferenceTime = 100; // ns, of the time term of the mass calibration
constexpr double LightestMass = 0.5; // u, the range of masses looked for
constexpr double HeaviestMass = 400;

// ln(t / 100 ns), NaN when the time of flight is not positive
double logTime(double tofChannel, double tofSlope, double tofOffset) {
    const double t = (tofSlope * tofChannel + tofOffset) / Units::Nanosecond;
    return t > 0 ? std::log(t / ReferenceTime) : NaN;
}

double polynomial(const MassCalibration &calibration, double logMass, double logT) {
    double sum = 0;
    for(const MassCalibrationTerm &term : calibration.terms) {
        sum += term.coefficient * std::pow(logMass, term.massPower) * std::pow(logT, term.timePower);
    }
    return sum;
}
}

double logProtoMass(double tofChannel, double energyChannel, double tofSlope, double tofOffset) {
    const double t = (tofSlope * tofChannel + tofOffset) / Units::Nanosecond;
    if(t <= 0 || energyChannel <= 0) {
        return NaN;
    }
    return std::log(energyChannel * t * t);
}

double MassBand::protoMassAt(double tof) const {
    return interpolate(ridge, tof);
}

double MassBand::widthAt(double tof) const {
    std::vector<QPointF> points(ridge.size());
    for(size_t i = 0; i < ridge.size(); i++) {
        points[i] = {ridge[i].x(), widths[i]};
    }
    return interpolate(points, tof);
}

std::vector<MassBand> findMassBands(std::span<const ToFEEvent> events, double tofSlope, double tofOffset,
                                    const BandSearchOptions &options) {
    std::vector<Point> points;
    points.reserve(events.size());
    for(const ToFEEvent &event : events) {
        const double q = logProtoMass(event.tof, event.energy, tofSlope, tofOffset);
        if(std::isfinite(q)) {
            points.push_back({event.tof, q});
        }
    }
    if(points.size() < 100 || options.tofSliceWidth <= 0 || options.binWidth <= 0) {
        return {};
    }
    std::vector<double> values(points.size());
    std::transform(points.begin(), points.end(), values.begin(), [](const Point &p) { return p.protoMass; });
    const double qMin = quantile(values, OutlierFraction);
    const double qMax = quantile(values, 1 - OutlierFraction);
    std::transform(points.begin(), points.end(), values.begin(), [](const Point &p) { return p.tof; });
    const double tofMin = std::floor(quantile(values, OutlierFraction));
    const double tofMax = quantile(values, 1 - OutlierFraction);
    const auto bins = static_cast<size_t>(std::ceil((qMax - qMin) / options.binWidth)) + 1;
    const auto slices = static_cast<size_t>(std::ceil((tofMax - tofMin) / options.tofSliceWidth)) + 1;
    if(bins > 100000 || slices > 100000) {
        return {};
    }
    std::vector<std::vector<double>> histogram(slices, std::vector<double>(bins, 0));
    for(const Point &point : points) {
        const double s = std::floor((point.tof - tofMin) / options.tofSliceWidth);
        const double b = std::floor((point.protoMass - qMin) / options.binWidth);
        if(s >= 0 && s < static_cast<double>(slices) && b >= 0 && b < static_cast<double>(bins)) {
            histogram[static_cast<size_t>(s)][static_cast<size_t>(b)]++;
        }
    }

    // Ridges followed from slice to slice, each peak continues the nearest ridge that expects it
    std::vector<Track> active;
    std::vector<Track> finished;
    for(size_t s = 0; s < slices; s++) {
        const double tof = tofMin + (static_cast<double>(s) + 0.5) * options.tofSliceWidth;
        std::vector<double> windowCounts(bins, 0);
        const size_t first = s >= static_cast<size_t>(options.window) ? s - static_cast<size_t>(options.window) : 0;
        const size_t last = std::min(slices - 1, s + static_cast<size_t>(options.window));
        for(size_t w = first; w <= last; w++) {
            for(size_t b = 0; b < bins; b++) {
                windowCounts[b] += histogram[w][b];
            }
        }
        std::vector<Peak> peaks = findPeaks(windowCounts, qMin, options);
        // The counts of a peak are those of the slice itself
        for(Peak &peak : peaks) {
            peak.counts *= 1.0 / static_cast<double>(last - first + 1);
        }
        std::vector<bool> used(peaks.size(), false);
        // Pairs by distance, closest first
        struct Pair {
            double distance;
            size_t track;
            size_t peak;
        };
        std::vector<Pair> pairs;
        for(size_t t = 0; t < active.size(); t++) {
            const double expected = active[t].predicted(tof);
            for(size_t p = 0; p < peaks.size(); p++) {
                const double distance = std::abs(peaks[p].position - expected);
                if(distance <= options.tolerance) {
                    pairs.push_back({distance, t, p});
                }
            }
        }
        std::sort(pairs.begin(), pairs.end(), [](const Pair &a, const Pair &b) { return a.distance < b.distance; });
        std::vector<bool> continued(active.size(), false);
        for(const Pair &pair : pairs) {
            if(continued[pair.track] || used[pair.peak]) {
                continue;
            }
            continued[pair.track] = true;
            used[pair.peak] = true;
            Track &track = active[pair.track];
            track.ridge.push_back({tof, peaks[pair.peak].position});
            track.widths.push_back(peaks[pair.peak].width);
            track.counts.push_back(peaks[pair.peak].counts);
            track.lastSlice = static_cast<int>(s);
        }
        for(size_t p = 0; p < peaks.size(); p++) {
            if(!used[p]) {
                active.push_back({{{tof, peaks[p].position}}, {peaks[p].width}, {peaks[p].counts}, static_cast<int>(s)});
            }
        }
        // Ridges missing for too long end
        for(auto it = active.begin(); it != active.end();) {
            if(static_cast<int>(s) - it->lastSlice > options.maxGap) {
                finished.push_back(std::move(*it));
                it = active.erase(it);
            } else {
                ++it;
            }
        }
    }
    finished.insert(finished.end(), std::make_move_iterator(active.begin()), std::make_move_iterator(active.end()));

    // Fragments that continue each other are joined: the closest start after the end of a ridge, where its line
    // leads, within the tolerance growing with the gap
    std::sort(finished.begin(), finished.end(), [](const Track &a, const Track &b) { return a.ridge.front().x() < b.ridge.front().x(); });
    for(bool joined = true; joined;) {
        joined = false;
        for(size_t a = 0; a < finished.size() && !joined; a++) {
            const double end = finished[a].ridge.back().x();
            size_t best = finished.size();
            double bestDistance = 0;
            for(size_t b = 0; b < finished.size(); b++) {
                const double start = finished[b].ridge.front().x();
                if(b == a || start <= end || start - end > options.mergeGap) {
                    continue;
                }
                const double gapSlices = (start - end) / options.tofSliceWidth;
                const double tolerance = options.tolerance * (1 + gapSlices / 4);
                const double distance = std::abs(finished[a].predicted(start) - extrapolate(finished[b].ridge, start, false));
                if(distance <= tolerance && (best == finished.size() || distance < bestDistance)) {
                    best = b;
                    bestDistance = distance;
                }
            }
            if(best < finished.size()) {
                Track &target = finished[a];
                Track &source = finished[best];
                target.ridge.insert(target.ridge.end(), source.ridge.begin(), source.ridge.end());
                target.widths.insert(target.widths.end(), source.widths.begin(), source.widths.end());
                target.counts.insert(target.counts.end(), source.counts.begin(), source.counts.end());
                finished.erase(finished.begin() + static_cast<std::ptrdiff_t>(best));
                joined = true;
            }
        }
    }

    std::vector<MassBand> bands;
    for(Track &track : finished) {
        if(static_cast<int>(track.ridge.size()) < options.minSlices) {
            continue;
        }
        MassBand band;
        band.ridge = std::move(track.ridge);
        band.widths = std::move(track.widths);
        band.counts = std::move(track.counts);
        for(const double count : band.counts) {
            band.events += static_cast<size_t>(count);
        }
        bands.push_back(std::move(band));
    }

    // Leading edges from the ToF spectra of the events near each ridge
    for(MassBand &band : bands) {
        std::vector<size_t> counts;
        for(const Point &point : points) {
            // Near the start of the band, other bands may pass where its ridge is extended before it
            if(point.tof < std::max(0.0, band.startTof() - EdgeSearchSlices * options.tofSliceWidth) || point.tof > band.endTof()) {
                continue;
            }
            if(std::abs(point.protoMass - band.protoMassAt(point.tof)) > EdgeWidths * band.widthAt(point.tof)) {
                continue;
            }
            const auto channel = static_cast<size_t>(std::lround(point.tof));
            if(channel >= counts.size()) {
                counts.resize(channel + 1, 0);
            }
            counts[channel]++;
        }
        const EdgeFit edge = findAndFitEdge(counts);
        band.edgeTof = edge.ok ? edge.mu : NaN;
        band.edgeSigma = edge.ok ? edge.sigma : 0;
    }
    std::sort(bands.begin(), bands.end(), [](const MassBand &a, const MassBand &b) {
        return a.ridge.front().y() < b.ridge.front().y();
    });
    return bands;
}

MassCalibrationFit fitMassCalibration(const std::vector<MassCalibrationPoint> &points, double tofSlope, double tofOffset,
                                      int maxDegree) {
    MassCalibrationFit fit;
    std::vector<double> masses;
    for(const MassCalibrationPoint &point : points) {
        if(std::none_of(masses.begin(), masses.end(), [&point](double m) { return std::abs(m - point.mass) < 1e-3 * point.mass; })) {
            masses.push_back(point.mass);
        }
    }
    if(masses.size() < 2) {
        fit.errorMessage = QStringLiteral("Bands of at least two masses are needed.");
        return fit;
    }
    // Terms up to the degree, the power of the mass below the number of masses
    const int degree = std::max(1, maxDegree);
    const int maxMassPower = static_cast<int>(masses.size()) - 1;
    QList<MassCalibrationTerm> terms;
    for(int d = 0; d <= degree; d++) {
        for(int j = 0; j <= d; j++) {
            if(d - j <= maxMassPower) {
                terms.append({d - j, j, 0});
            }
        }
    }
    struct Row {
        double logMass;
        double logT;
        double residual; // Proto-mass - ln(m / u)
        double weight;
    };
    std::vector<Row> rows;
    for(const MassCalibrationPoint &point : points) {
        const double logT = logTime(point.tof, tofSlope, tofOffset);
        if(std::isfinite(logT) && std::isfinite(point.protoMass) && point.mass > 0 && point.weight > 0) {
            const double logMass = std::log(point.mass / Units::AtomicMassUnit);
            rows.push_back({logMass, logT, point.protoMass - logMass, point.weight});
        }
    }
    const auto n = rows.size();
    const auto p = static_cast<size_t>(terms.size());
    if(n <= p) {
        fit.errorMessage = QString("At least %1 points on the bands are needed.").arg(p + 1);
        return fit;
    }
    gsl_matrix *x = gsl_matrix_alloc(n, p);
    gsl_vector *y = gsl_vector_alloc(n);
    gsl_vector *w = gsl_vector_alloc(n);
    gsl_vector *c = gsl_vector_alloc(p);
    gsl_matrix *covariance = gsl_matrix_alloc(p, p);
    for(size_t i = 0; i < n; i++) {
        for(size_t k = 0; k < p; k++) {
            const MassCalibrationTerm &term = terms[static_cast<qsizetype>(k)];
            gsl_matrix_set(x, i, k, std::pow(rows[i].logMass, term.massPower) * std::pow(rows[i].logT, term.timePower));
        }
        gsl_vector_set(y, i, rows[i].residual);
        gsl_vector_set(w, i, rows[i].weight);
    }
    gsl_error_handler_t *handler = gsl_set_error_handler_off();
    gsl_multifit_linear_workspace *workspace = gsl_multifit_linear_alloc(n, p);
    double chiSquared = 0;
    const int status = gsl_multifit_wlinear(x, w, y, c, covariance, &chiSquared, workspace);
    for(size_t k = 0; k < p; k++) {
        terms[static_cast<qsizetype>(k)].coefficient = gsl_vector_get(c, k);
    }
    gsl_multifit_linear_free(workspace);
    gsl_matrix_free(covariance);
    gsl_vector_free(c);
    gsl_vector_free(w);
    gsl_vector_free(y);
    gsl_matrix_free(x);
    gsl_set_error_handler(handler);
    if(status != GSL_SUCCESS) {
        fit.errorMessage = QString("The fit failed: %1.").arg(gsl_strerror(status));
        return fit;
    }
    fit.calibration.terms = terms;
    fit.calibration.minMass = *std::min_element(masses.begin(), masses.end());
    fit.calibration.maxMass = *std::max_element(masses.begin(), masses.end());
    double sum = 0;
    double weights = 0;
    for(const MassCalibrationPoint &point : points) {
        const double logT = logTime(point.tof, tofSlope, tofOffset);
        const double t = (tofSlope * point.tof + tofOffset) / Units::Nanosecond;
        const double mass = calibratedMass(fit.calibration, point.tof, std::exp(point.protoMass) / (t * t), tofSlope, tofOffset);
        if(std::isfinite(logT) && std::isfinite(mass)) {
            sum += point.weight * std::pow(mass / point.mass - 1, 2);
            weights += point.weight;
        }
    }
    fit.rmsResidual = weights > 0 ? std::sqrt(sum / weights) : NaN;
    fit.ok = true;
    return fit;
}

double calibratedProtoMass(const MassCalibration &calibration, double tofChannel, double mass, double tofSlope,
                           double tofOffset) {
    const double logT = logTime(tofChannel, tofSlope, tofOffset);
    if(!std::isfinite(logT) || mass <= 0) {
        return NaN;
    }
    const double logMass = std::log(mass / Units::AtomicMassUnit);
    return logMass + polynomial(calibration, logMass, logT);
}

double calibratedEnergyChannel(const MassCalibration &calibration, double tofChannel, double mass, double tofSlope,
                               double tofOffset) {
    const double protoMass = calibratedProtoMass(calibration, tofChannel, mass, tofSlope, tofOffset);
    const double t = (tofSlope * tofChannel + tofOffset) / Units::Nanosecond;
    return std::isfinite(protoMass) ? std::exp(protoMass) / (t * t) : NaN;
}

double calibratedMass(const MassCalibration &calibration, double tofChannel, double energyChannel, double tofSlope,
                      double tofOffset) {
    const double protoMass = logProtoMass(tofChannel, energyChannel, tofSlope, tofOffset);
    const double logT = logTime(tofChannel, tofSlope, tofOffset);
    if(!calibration.isValid() || !std::isfinite(protoMass) || !std::isfinite(logT)) {
        return NaN;
    }
    // The proto-mass grows with the mass, the root is bracketed and halved
    auto g = [&](double logMass) { return logMass + polynomial(calibration, logMass, logT) - protoMass; };
    double low = std::log(LightestMass);
    double high = std::log(HeaviestMass);
    if(g(low) > 0 || g(high) < 0) {
        return NaN;
    }
    for(int i = 0; i < 50; i++) {
        const double middle = (low + high) / 2;
        (g(middle) < 0 ? low : high) = middle;
    }
    return std::exp((low + high) / 2) * Units::AtomicMassUnit;
}

namespace {
constexpr double EdgeTolerance = 0.012; // Relative, a leading edge matches the expected one within this or 4 sigma
constexpr double EdgeSigmas = 4;
constexpr double MassTolerance = 0.04; // Relative, a band and an isotope match within this
constexpr double OutlierMass = 0.05; // Relative, bands identified by their edge farther from their calibrated mass are dropped
constexpr double ContinuationMass = 0.12; // Relative, a band starting late within this of another's mass continues it

struct Candidate {
    BandIdentity identity;
    double mass; // kg, detected
    double edge; // Channel
    double prior; // Natural abundance of the isotope
};

std::vector<Candidate> surfaceCandidates(const ExperimentalSettings &settings, const DetectorSettings &detector,
                                         const PhysicsSettings &physics) {
    std::vector<Candidate> candidates;
    for(int z = 1; z <= Elements::maxZ(); z++) {
        for(const Elements::Isotope &isotope : Elements::isotopes(z)) {
            if(isotope.abundance <= 0) {
                continue;
            }
            for(const CutType type : {CutType::Recoil, CutType::Scattered}) {
                Cut cut;
                cut.z = z;
                cut.a = isotope.a;
                cut.type = type;
                const ExpectedTof expected = expectedSurfaceTof(&settings, detector, physics, cut);
                if(!expected.isValid()) {
                    continue;
                }
                const BandIdentity identity{z, isotope.a, type};
                const double edge = (expected.tof - detector.tofOffset) / detector.tofSlope;
                candidates.push_back({identity, detectedMass(identity, &settings), edge, isotope.abundance});
            }
        }
    }
    return candidates;
}
}

double detectedMass(const BandIdentity &identity, const ExperimentalSettings *settings) {
    if(!identity.isKnown()) {
        return 0;
    }
    if(identity.type == CutType::Scattered) {
        return settings && settings->beamZ > 0 ? Elements::mass(settings->beamZ, settings->beamA) : 0;
    }
    return Elements::mass(identity.z, identity.a);
}

double bandMass(const MassBand &band, const MassCalibration &calibration, double tofSlope, double tofOffset) {
    std::vector<std::pair<double, double>> masses; // Mass and events
    double total = 0;
    for(size_t i = 0; i < band.ridge.size(); i++) {
        const double t = (tofSlope * band.ridge[i].x() + tofOffset) / Units::Nanosecond;
        const double mass = t > 0 ? calibratedMass(calibration, band.ridge[i].x(), std::exp(band.ridge[i].y()) / (t * t), tofSlope, tofOffset)
                                  : NaN;
        if(std::isfinite(mass)) {
            masses.push_back({mass, band.counts[i]});
            total += band.counts[i];
        }
    }
    if(masses.empty()) {
        return NaN;
    }
    std::sort(masses.begin(), masses.end());
    double sum = 0;
    for(const auto &[mass, events] : masses) {
        sum += events;
        if(sum >= total / 2) {
            return mass;
        }
    }
    return masses.back().first;
}

MassCalibrationFit calibrateFromBands(const std::vector<MassBand> &bands, const std::vector<BandIdentity> &identities,
                                      const ExperimentalSettings &settings, const DetectorSettings &detector) {
    std::vector<MassCalibrationPoint> points;
    for(size_t i = 0; i < bands.size() && i < identities.size(); i++) {
        const double mass = detectedMass(identities[i], &settings);
        if(mass <= 0) {
            continue;
        }
        const MassBand &band = bands[i];
        for(size_t k = 0; k < band.ridge.size(); k++) {
            // The ridge is extended a little before the leading edge by the window of slices
            if(std::isfinite(band.edgeTof) && band.ridge[k].x() < band.edgeTof - 2 * band.edgeSigma) {
                continue;
            }
            points.push_back({band.ridge[k].x(), band.ridge[k].y(), mass, band.counts[k]});
        }
    }
    return fitMassCalibration(points, detector.tofSlope, detector.tofOffset);
}

IdentityProposal proposeIdentities(const std::vector<MassBand> &bands, const ExperimentalSettings &settings,
                                   const DetectorSettings &detector, const PhysicsSettings &physics) {
    IdentityProposal proposal;
    proposal.bands.resize(bands.size());
    if(detector.tofSlope <= 0 || detector.tofLength <= 0 || settings.beamZ <= 0 || settings.beamEnergy <= 0) {
        proposal.calibration.errorMessage = QStringLiteral("The experimental settings and the ToF calibration are needed.");
        return proposal;
    }
    const std::vector<Candidate> candidates = surfaceCandidates(settings, detector, physics);

    // Candidates of each band by its leading edge, scored by the match and the abundance
    struct Option {
        size_t candidate;
        double score;
    };
    std::vector<std::vector<Option>> options(bands.size());
    for(size_t b = 0; b < bands.size(); b++) {
        const MassBand &band = bands[b];
        if(!std::isfinite(band.edgeTof)) {
            continue;
        }
        const double tolerance = std::max(EdgeTolerance * band.edgeTof, EdgeSigmas * band.edgeSigma);
        for(size_t c = 0; c < candidates.size(); c++) {
            const double difference = (candidates[c].edge - band.edgeTof) / tolerance;
            if(std::abs(difference) <= 1) {
                options[b].push_back({c, std::exp(-2 * difference * difference) * (0.1 + candidates[c].prior)});
            }
        }
    }
    // The most likely combination with masses growing with the proto-mass at the edges: the best total score over the
    // bands in order of proto-mass, each with one of its candidates or none
    std::vector<size_t> order(bands.size());
    for(size_t i = 0; i < order.size(); i++) {
        order[i] = i;
    }
    auto edgeProtoMass = [&bands](size_t b) {
        return bands[b].protoMassAt(std::isfinite(bands[b].edgeTof) ? bands[b].edgeTof : bands[b].startTof());
    };
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return edgeProtoMass(a) < edgeProtoMass(b); });
    struct State {
        double score;
        double mass; // Of the last band identified
        std::vector<int> chosen; // Option of each band in order, -1 for none
    };
    // States kept by the mass of the last identified band, the best for each
    std::vector<State> states{{0, 0, {}}};
    for(const size_t b : order) {
        std::vector<State> next;
        for(const State &state : states) {
            State skip = state;
            skip.chosen.push_back(-1);
            next.push_back(skip);
            for(size_t o = 0; o < options[b].size(); o++) {
                const Candidate &candidate = candidates[options[b][o].candidate];
                if(candidate.mass <= state.mass * (1 + 1e-6)) {
                    continue;
                }
                State take = state;
                take.score += options[b][o].score;
                take.mass = candidate.mass;
                take.chosen.push_back(static_cast<int>(o));
                next.push_back(take);
            }
        }
        std::sort(next.begin(), next.end(), [](const State &a, const State &c) {
            return a.mass != c.mass ? a.mass < c.mass : a.score > c.score;
        });
        // A state is kept when no state with a lower or equal mass has a higher score
        states.clear();
        double best = -1;
        for(const State &state : next) {
            if(state.score > best) {
                states.push_back(state);
                best = state.score;
            }
        }
    }
    const State &best = *std::max_element(states.begin(), states.end(), [](const State &a, const State &c) { return a.score < c.score; });
    std::vector<BandIdentity> identities(bands.size());
    for(size_t i = 0; i < order.size(); i++) {
        const size_t b = order[i];
        if(best.chosen[i] >= 0) {
            const Candidate &candidate = candidates[options[b][static_cast<size_t>(best.chosen[i])].candidate];
            identities[b] = candidate.identity;
            proposal.bands[b].basis = IdentityBasis::Edge;
            proposal.bands[b].expectedEdge = candidate.edge;
        }
    }

    // Calibration from the bands identified by their edges, without those far from their calibrated mass
    MassCalibrationFit fit = calibrateFromBands(bands, identities, settings, detector);
    while(fit.ok) {
        size_t worst = bands.size();
        double worstDifference = OutlierMass;
        for(size_t b = 0; b < bands.size(); b++) {
            const double expected = detectedMass(identities[b], &settings);
            if(expected <= 0) {
                continue;
            }
            const double difference = std::abs(bandMass(bands[b], fit.calibration, detector.tofSlope, detector.tofOffset) / expected - 1);
            if(!(difference <= worstDifference)) {
                worst = b;
                worstDifference = std::isfinite(difference) ? difference : 1e300;
            }
        }
        if(worst == bands.size()) {
            break;
        }
        identities[worst] = {};
        proposal.bands[worst].basis = IdentityBasis::None;
        const MassCalibrationFit refit = calibrateFromBands(bands, identities, settings, detector);
        if(!refit.ok) {
            break;
        }
        fit = refit;
    }

    if(!fit.ok && detector.massCalibration.isValid()) {
        fit.calibration = detector.massCalibration;
        fit.rmsResidual = NaN;
        fit.ok = true;
        fit.errorMessage.clear();
    }

    // Bands without an identity that start in the later half of an identified band, near its mass, continue it
    if(fit.ok) {
        for(size_t b = 0; b < bands.size(); b++) {
            if(identities[b].isKnown()) {
                continue;
            }
            const double mass = bandMass(bands[b], fit.calibration, detector.tofSlope, detector.tofOffset);
            double bestDifference = ContinuationMass;
            for(size_t other = 0; other < bands.size(); other++) {
                const double otherMass = detectedMass(identities[other], &settings);
                const MassBand &parent = bands[other];
                if(otherMass <= 0 || bands[b].startTof() < (parent.startTof() + parent.endTof()) / 2) {
                    continue;
                }
                const double difference = std::abs(mass / otherMass - 1);
                if(difference < bestDifference) {
                    bestDifference = difference;
                    proposal.bands[b].continues = static_cast<int>(other);
                }
            }
        }
    }

    // The other bands get the isotope of the nearest mass, within the masses the calibration was fitted to
    if(fit.ok) {
        for(size_t b = 0; b < bands.size(); b++) {
            if(identities[b].isKnown() || proposal.bands[b].continues >= 0) {
                continue;
            }
            const double mass = bandMass(bands[b], fit.calibration, detector.tofSlope, detector.tofOffset);
            if(!(mass >= fit.calibration.minMass * (1 - MassTolerance) && mass <= fit.calibration.maxMass * (1 + MassTolerance))) {
                continue;
            }
            double bestScore = 0;
            for(int z = 1; z <= Elements::maxZ(); z++) {
                for(const Elements::Isotope &isotope : Elements::isotopes(z)) {
                    const double difference = std::abs(isotope.mass / mass - 1) / MassTolerance;
                    const BandIdentity identity{z, isotope.a, CutType::Recoil};
                    const bool taken = std::find(identities.begin(), identities.end(), identity) != identities.end();
                    const double score = std::exp(-2 * difference * difference) * isotope.abundance;
                    if(isotope.abundance > 0 && difference <= 1 && !taken && score > bestScore) {
                        bestScore = score;
                        identities[b] = identity;
                    }
                }
            }
            if(identities[b].isKnown()) {
                proposal.bands[b].basis = IdentityBasis::Mass;
            }
        }
    }
    for(size_t b = 0; b < bands.size(); b++) {
        proposal.bands[b].identity = identities[b];
        if(proposal.bands[b].basis != IdentityBasis::Edge) {
            proposal.bands[b].expectedEdge = NaN;
        }
    }
    proposal.calibration = fit;
    return proposal;
}

namespace {
constexpr int EmIterations = 30;
constexpr double MinWidth = 0.002; // Of the proto-mass
constexpr double MaxWidth = 0.3;
constexpr double CentreShift = 1; // Widths a centre may move from its ridge in a slice
constexpr double StartSigmas = 3; // A selection starts this many edge widths before the leading edge
constexpr double MinStartMargin = 15; // Channels
constexpr int BoundarySmoothing = 2; // Slices on each side averaged in the boundaries
constexpr int FailedSlices = 2; // Consecutive slices over the contamination limit end a selection
constexpr double PolygonTolerance = 0.002; // Relative to the E channel, vertices nearer to the line are left out
constexpr double MaxWidthGrowth = 2.5; // Of the width of a band from its ridge
constexpr double Significance = 3; // Standard deviations the events of a band must be above the background in its selection

double normalCdf(double x, double centre, double sigma) {
    return 0.5 * std::erfc(-(x - centre) / (sigma * M_SQRT2));
}

double logDensity(double x, double weight, double centre, double sigma) {
    const double z = (x - centre) / sigma;
    return std::log(std::max(weight, 1e-300)) - std::log(sigma) - 0.5 * z * z;
}

// Centre of a band against ToF: its ridge smoothed, beyond it the calibration of its mass or the ridge extended straight
class BandCentre {
public:
    BandCentre(const SelectionBand &band, const MassCalibration &calibration, double tofSlope, double tofOffset)
        : m_band(band), m_calibration(calibration), m_slope(tofSlope), m_offset(tofOffset) {
        const std::vector<QPointF> &ridge = band.band->ridge;
        for(size_t i = 0; i < ridge.size(); i++) {
            std::vector<double> window;
            for(size_t j = i >= 2 ? i - 2 : 0; j <= std::min(ridge.size() - 1, i + 2); j++) {
                window.push_back(ridge[j].y());
            }
            std::nth_element(window.begin(), window.begin() + static_cast<std::ptrdiff_t>(window.size() / 2), window.end());
            m_ridge.push_back({ridge[i].x(), window[window.size() / 2]});
        }
    }
    double at(double tof) const {
        if(m_ridge.empty()) {
            return NaN;
        }
        if(tof <= m_ridge.back().x()) {
            return interpolate(m_ridge, tof);
        }
        const double end = m_ridge.back().x();
        if(m_band.mass > 0 && m_calibration.isValid()) {
            const double shift = m_ridge.back().y() - calibratedProtoMass(m_calibration, end, m_band.mass, m_slope, m_offset);
            const double value = calibratedProtoMass(m_calibration, tof, m_band.mass, m_slope, m_offset) + shift;
            if(std::isfinite(value)) {
                return value;
            }
        }
        return extrapolate(m_ridge, tof);
    }

private:
    const SelectionBand &m_band;
    const MassCalibration &m_calibration;
    double m_slope;
    double m_offset;
    std::vector<QPointF> m_ridge;
};

struct Component {
    double centre;
    double sigma;
    double weight; // Events
    double maxSigma; // A band fades out with depth, its width is kept near that of its ridge instead of taking the background
};

// Fits the weights, widths and centres (near their starting values) of the components and the background density to
// the proto-masses of the events in the range
void fitMixture(const std::vector<double> &values, std::vector<Component> &components, double &background, double low,
                double high) {
    const size_t k = components.size();
    const double range = high - low;
    std::vector<double> centres(k);
    for(size_t c = 0; c < k; c++) {
        centres[c] = components[c].centre;
    }
    const double n = static_cast<double>(values.size());
    background = std::max(background, 0.01 * n / range);
    std::vector<double> responsibilities(k + 1);
    for(int iteration = 0; iteration < EmIterations; iteration++) {
        std::vector<double> sumR(k + 1, 0), sumX(k, 0), sumXX(k, 0);
        for(const double x : values) {
            double total = background;
            for(size_t c = 0; c < k; c++) {
                const Component &component = components[c];
                const double z = (x - component.centre) / component.sigma;
                responsibilities[c] = z * z < 100 ? component.weight / (component.sigma * std::sqrt(2 * M_PI)) * std::exp(-0.5 * z * z) : 0;
                total += responsibilities[c];
            }
            responsibilities[k] = background;
            for(size_t c = 0; c <= k; c++) {
                const double r = responsibilities[c] / total;
                sumR[c] += r;
                if(c < k) {
                    sumX[c] += r * x;
                    sumXX[c] += r * x * x;
                }
            }
        }
        for(size_t c = 0; c < k; c++) {
            Component &component = components[c];
            component.weight = sumR[c];
            if(sumR[c] > 5) {
                const double mean = sumX[c] / sumR[c];
                component.centre = std::clamp(mean, centres[c] - CentreShift * component.sigma, centres[c] + CentreShift * component.sigma);
                const double variance = sumXX[c] / sumR[c] - 2 * mean * component.centre + component.centre * component.centre;
                component.sigma = std::clamp(std::sqrt(std::max(0.0, variance)), MinWidth, component.maxSigma);
            }
        }
        background = sumR[k] / range;
    }
}

// Where the densities of two components are equal between their centres; at the centre of the first when the second is
// denser everywhere between them, at the second when the first is
double equalDensity(const Component &a, const Component &b) {
    auto h = [&](double x) { return logDensity(x, a.weight, a.centre, a.sigma) - logDensity(x, b.weight, b.centre, b.sigma); };
    double low = a.centre;
    double high = b.centre;
    const double atA = h(low);
    const double atB = h(high);
    if(atA <= 0) {
        return a.centre;
    }
    if(atB >= 0) {
        return b.centre;
    }
    for(int i = 0; i < 50; i++) {
        const double middle = (low + high) / 2;
        (h(middle) > 0 ? low : high) = middle;
    }
    return (low + high) / 2;
}

// Vertices of a polyline in ToF-E farther than the tolerance from the line between their neighbours, vertically
void simplify(const QList<QPointF> &points, qsizetype first, qsizetype last, QList<bool> &keep) {
    if(last <= first + 1) {
        return;
    }
    double worst = 0;
    qsizetype worstIndex = -1;
    const QPointF &a = points[first];
    const QPointF &b = points[last];
    for(qsizetype i = first + 1; i < last; i++) {
        const double f = (points[i].x() - a.x()) / (b.x() - a.x());
        const double line = a.y() + f * (b.y() - a.y());
        const double distance = std::abs(points[i].y() - line) / (PolygonTolerance * std::max(1.0, points[i].y()));
        if(distance > worst) {
            worst = distance;
            worstIndex = i;
        }
    }
    if(worst > 1) {
        keep[worstIndex] = true;
        simplify(points, first, worstIndex, keep);
        simplify(points, worstIndex, last, keep);
    }
}

QList<QPointF> simplified(const QList<QPointF> &points) {
    if(points.size() < 3) {
        return points;
    }
    QList<bool> keep(points.size(), false);
    keep.front() = true;
    keep.back() = true;
    simplify(points, 0, points.size() - 1, keep);
    QList<QPointF> result;
    for(qsizetype i = 0; i < points.size(); i++) {
        if(keep[i]) {
            result.append(points[i]);
        }
    }
    return result;
}

double energyChannel(double tof, double protoMass, double tofSlope, double tofOffset) {
    const double t = (tofSlope * tof + tofOffset) / Units::Nanosecond;
    return std::exp(protoMass) / (t * t);
}
}

std::vector<BandSelection> selectBands(std::span<const ToFEEvent> events, const std::vector<SelectionBand> &bands,
                                       const MassCalibration &calibration, double tofSlope, double tofOffset,
                                       const SelectionOptions &options) {
    std::vector<BandSelection> selections(bands.size());
    if(bands.empty() || tofSlope <= 0) {
        return selections;
    }
    std::vector<BandCentre> centres;
    for(const SelectionBand &band : bands) {
        centres.emplace_back(band, calibration, tofSlope, tofOffset);
    }
    std::vector<Point> points;
    for(const ToFEEvent &event : events) {
        const double q = logProtoMass(event.tof, event.energy, tofSlope, tofOffset);
        if(std::isfinite(q)) {
            points.push_back({event.tof, q});
        }
    }
    if(points.empty()) {
        return selections;
    }
    std::sort(points.begin(), points.end(), [](const Point &a, const Point &b) { return a.tof < b.tof; });

    // Where the selections start: before the leading edges of the selected bands
    std::vector<double> starts(bands.size(), NaN);
    std::vector<double> leadingEdges(bands.size(), NaN);
    double firstStart = std::numeric_limits<double>::max();
    for(size_t b = 0; b < bands.size(); b++) {
        const MassBand &band = *bands[b].band;
        leadingEdges[b] = std::isfinite(band.edgeTof) ? band.edgeTof : band.startTof();
        starts[b] = leadingEdges[b] - std::max(MinStartMargin, StartSigmas * band.edgeSigma);
        if(bands[b].selected) {
            firstStart = std::min(firstStart, starts[b]);
        }
    }
    if(firstStart == std::numeric_limits<double>::max()) {
        return selections;
    }

    // Slices from the first start on, at least the width and the events of the options
    struct Slice {
        double low;
        double high;
        std::vector<Component> components;
        double background = 0;
    };
    std::vector<Slice> slices;
    auto it = std::lower_bound(points.begin(), points.end(), firstStart, [](const Point &p, double tof) { return p.tof < tof; });
    const double lastTof = points.back().tof;
    for(double low = firstStart; low < lastTof;) {
        double high = low + options.sliceWidth;
        auto end = std::lower_bound(it, points.end(), high, [](const Point &p, double tof) { return p.tof < tof; });
        while(static_cast<size_t>(end - it) < options.minSliceEvents && end != points.end()) {
            high += options.sliceWidth;
            end = std::lower_bound(end, points.end(), high, [](const Point &p, double tof) { return p.tof < tof; });
        }
        Slice slice{low, std::min(high, lastTof + 1), {}, 0};
        const double middle = (slice.low + slice.high) / 2;
        // The bands whose centres are in the range of the events
        double qLow = std::numeric_limits<double>::max();
        double qHigh = std::numeric_limits<double>::lowest();
        for(size_t b = 0; b < bands.size(); b++) {
            const double centre = centres[b].at(middle);
            const double width = std::clamp(bands[b].band->widthAt(middle), MinWidth, MaxWidth);
            slice.components.push_back({centre, width, 1, std::min(MaxWidth, MaxWidthGrowth * width)});
            qLow = std::min(qLow, centre - 5 * width);
            qHigh = std::max(qHigh, centre + 5 * width);
        }
        std::vector<double> values;
        for(auto p = it; p != end; ++p) {
            if(p->protoMass >= qLow && p->protoMass <= qHigh) {
                values.push_back(p->protoMass);
            }
        }
        for(Component &component : slice.components) {
            component.weight = std::max(1.0, static_cast<double>(values.size()) / static_cast<double>(bands.size()));
            if(!slices.empty()) {
                // From the slice before, the bands change slowly
                const Component &before = slices.back().components[static_cast<size_t>(&component - slice.components.data())];
                component.sigma = before.sigma;
                component.weight = std::max(1.0, before.weight);
            }
        }
        if(!values.empty()) {
            fitMixture(values, slice.components, slice.background, qLow, qHigh);
        }
        slices.push_back(std::move(slice));
        it = end;
        low = high;
    }

    // Boundaries and contamination of each selected band in each slice
    for(size_t b = 0; b < bands.size(); b++) {
        if(!bands[b].selected) {
            continue;
        }
        BandSelection &selection = selections[b];
        struct Edges {
            double tof;
            double lower;
            double upper;
            double own;
            double other;
        };
        std::vector<Edges> edges;
        int failed = 0;
        selection.endReason = QStringLiteral("the events end");
        for(const Slice &slice : slices) {
            // Before the leading edge the band has no events yet, the selection starts with the boundaries after it
            if(slice.high <= leadingEdges[b]) {
                continue;
            }
            const Component &own = slice.components[b];
            double lower = own.centre - options.maxSigmas * own.sigma;
            double upper = own.centre + options.maxSigmas * own.sigma;
            for(size_t other = 0; other < slice.components.size(); other++) {
                const Component &neighbour = slice.components[other];
                if(other == b || neighbour.weight < 1) {
                    continue;
                }
                if(neighbour.centre < own.centre) {
                    lower = std::max(lower, equalDensity(neighbour, own));
                } else {
                    upper = std::min(upper, equalDensity(own, neighbour));
                }
            }
            const double ownEvents = own.weight * (normalCdf(upper, own.centre, own.sigma) - normalCdf(lower, own.centre, own.sigma));
            const double backgroundEvents = slice.background * std::max(0.0, upper - lower);
            double otherEvents = 0;
            for(size_t other = 0; other < slice.components.size(); other++) {
                if(other != b) {
                    const Component &neighbour = slice.components[other];
                    otherEvents += neighbour.weight * (normalCdf(upper, neighbour.centre, neighbour.sigma)
                                                       - normalCdf(lower, neighbour.centre, neighbour.sigma));
                }
            }
            // Overlap with the other bands ends the selection, and so does the band fading into the background
            const bool overlap = upper <= lower || otherEvents > options.maxContamination * (ownEvents + otherEvents);
            const bool faded = ownEvents < Significance * std::sqrt(ownEvents + otherEvents + backgroundEvents);
            if(overlap || faded) {
                if(++failed >= FailedSlices) {
                    edges.resize(edges.size() - static_cast<size_t>(failed - 1));
                    selection.endReason = overlap ? QString("other bands would exceed %1 % of its events").arg(100 * options.maxContamination)
                                                  : QStringLiteral("the band fades into the background");
                    break;
                }
            } else {
                failed = 0;
            }
            edges.push_back({(slice.low + slice.high) / 2, lower, std::max(lower, upper), ownEvents, otherEvents + backgroundEvents});
        }
        if(edges.size() < 2) {
            selection.endReason = QStringLiteral("too few events or too much overlap at the edge");
            continue;
        }
        // Boundaries averaged over neighbouring slices, the first slices before the edge follow those after it
        std::vector<QPointF> lower(edges.size()), upper(edges.size());
        for(size_t i = 0; i < edges.size(); i++) {
            double sumLower = 0, sumUpper = 0;
            int count = 0;
            for(size_t j = i >= BoundarySmoothing ? i - BoundarySmoothing : 0; j <= std::min(edges.size() - 1, i + BoundarySmoothing); j++) {
                sumLower += edges[j].lower;
                sumUpper += edges[j].upper;
                count++;
            }
            lower[i] = {edges[i].tof, sumLower / count};
            upper[i] = {edges[i].tof, sumUpper / count};
            selection.events += edges[i].own;
            selection.contamination += edges[i].other;
        }
        selection.contamination /= std::max(1e-300, selection.events + selection.contamination);
        selection.startTof = starts[b];
        selection.endTof = edges.back().tof;
        lower.front().setX(selection.startTof);
        upper.front().setX(selection.startTof);
        selection.lower = lower;
        selection.upper = upper;
        QList<QPointF> lowerLine, upperLine;
        for(size_t i = 0; i < lower.size(); i++) {
            lowerLine.append({lower[i].x(), energyChannel(lower[i].x(), lower[i].y(), tofSlope, tofOffset)});
            upperLine.append({upper[i].x(), energyChannel(upper[i].x(), upper[i].y(), tofSlope, tofOffset)});
        }
        selection.polygon = simplified(upperLine);
        const QList<QPointF> bottom = simplified(lowerLine);
        for(auto p = bottom.rbegin(); p != bottom.rend(); ++p) {
            selection.polygon.append(*p);
        }
        selection.ok = true;
    }
    return selections;
}
