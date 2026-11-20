#include "massselection.h"
#include "elements.h"
#include "energyspectrum.h"
#include "stopping.h"
#include "tofcalibration.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>

#include <gsl/gsl_multifit.h>

namespace {
constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
constexpr double OutlierFraction = 0.001; // Of the events on each side, left out of the ranges
constexpr double Prominence = 4; // Standard deviations of the smoothed counts a peak rises above its valleys
constexpr double FwhmPerSigma = 2.3548;
constexpr double EdgeWidths = 2; // Events within this many widths of the ridge are used for the leading edge
constexpr double DeviationSpan = 600; // Channels of a ridge after its edge compared with the response
constexpr double EdgeSearchSlices = 8; // The leading edge is looked for from this many slices before the ridge starts
constexpr double LayerRise = 0.25; // Relative rise of the events along a band where a layer below begins
constexpr double LayerSignificance = 5; // Standard deviations of the rise
constexpr int LayerRiseSlices = 6; // The rise happens within this many slices
constexpr int MaxLayers = 3; // Of a band

struct Point {
    double tof;
    double protoMass;
};

// E channel of the proto-mass at the ToF channel
double energyChannel(double tof, double protoMass, double tofSlope, double tofOffset) {
    const double t = (tofSlope * tof + tofOffset) / Units::Nanosecond;
    return std::exp(protoMass) / (t * t);
}

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
// Where a layer below begins along the band: after a dip, the events rise by a significant fraction within a few
// slices. The middle of the rise, and its width as a standard deviation. NaN when there is no such rise.
double findLayer(const MassBand &band, double sliceWidth, double &sigma) {
    if(!std::isfinite(band.edgeTof) || band.ridge.size() < 2) {
        return NaN;
    }
    const double start = band.edgeTof + 2 * std::max(band.edgeSigma, sliceWidth);
    const double end = band.endTof();
    std::vector<double> slices;
    for(double tof = start; tof + sliceWidth <= end; tof += sliceWidth) {
        double sum = 0;
        for(auto channel = static_cast<size_t>(std::max(0.0, tof)); channel < static_cast<size_t>(tof + sliceWidth) && channel < band.profile.size(); channel++) {
            sum += static_cast<double>(band.profile[channel]);
        }
        slices.push_back(sum);
    }
    const auto n = static_cast<int>(slices.size());
    std::vector<double> smooth(slices.size());
    for(int i = 0; i < n; i++) {
        const int first = std::max(0, i - 1);
        const int last = std::min(n - 1, i + 1);
        double sum = 0;
        for(int j = first; j <= last; j++) {
            sum += slices[j];
        }
        smooth[i] = sum / (last - first + 1);
    }
    for(int i = 1; i + 1 < n; i++) {
        if(!(smooth[i] <= smooth[i - 1] && smooth[i] <= smooth[i + 1])) {
            continue;
        }
        int top = i;
        for(int j = i + 1; j <= std::min(n - 1, i + LayerRiseSlices); j++) {
            if(smooth[j] > smooth[top]) {
                top = j;
            }
        }
        const double low = smooth[i];
        const double high = smooth[top];
        if(high - low < std::max(LayerRise * high, LayerSignificance * std::sqrt(high))) {
            continue;
        }
        // Crossings of a quarter, half and three quarters of the rise
        auto crossing = [&](double fraction) {
            const double level = low + fraction * (high - low);
            for(int j = i; j < top; j++) {
                if(smooth[j] <= level && smooth[j + 1] > level) {
                    return start + (j + 0.5 + (level - smooth[j]) / (smooth[j + 1] - smooth[j])) * sliceWidth;
                }
            }
            return start + (top + 0.5) * sliceWidth;
        };
        sigma = std::max(sliceWidth / 2, (crossing(0.75) - crossing(0.25)) / 1.349);
        return crossing(0.5);
    }
    return NaN;
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
        // Without a fit, e.g. for few events, the first guess is close enough for identifying the band
        const EdgeFit guess = findEdge(counts);
        const EdgeFit edge = guess.ok ? fitEdge(counts, guess) : guess;
        band.edgeTof = edge.ok ? edge.mu : guess.ok ? guess.mu : NaN;
        band.edgeSigma = edge.ok ? edge.sigma : guess.ok ? guess.sigma : 0;
        band.edgeFitted = edge.ok;
        band.profile = std::move(counts);
    }

    // Layers: after the leading edge, a sharp rise of the events along the band begins a layer below. The layers split
    // off are looked at in turn.
    const size_t maxBands = bands.size() * MaxLayers;
    for(size_t b = 0; b < bands.size() && bands.size() < maxBands; b++) {
        double sigma = 0;
        const double split = findLayer(bands[b], options.tofSliceWidth, sigma);
        if(!std::isfinite(split)) {
            continue;
        }
        MassBand &band = bands[b];
        MassBand below;
        below.layerAbove = static_cast<int>(b);
        const auto first = static_cast<size_t>(std::lower_bound(band.ridge.begin(), band.ridge.end(), split,
                                                                [](const QPointF &p, double tof) { return p.x() < tof; })
                                               - band.ridge.begin());
        below.ridge.assign(band.ridge.begin() + static_cast<std::ptrdiff_t>(first), band.ridge.end());
        below.widths.assign(band.widths.begin() + static_cast<std::ptrdiff_t>(first), band.widths.end());
        below.counts.assign(band.counts.begin() + static_cast<std::ptrdiff_t>(first), band.counts.end());
        band.ridge.resize(first);
        band.widths.resize(first);
        band.counts.resize(first);
        below.edgeTof = split;
        below.edgeSigma = sigma;
        below.layerEnd = band.layerEnd;
        band.layerEnd = split - 2 * sigma;
        below.profile = band.profile;
        for(MassBand *part : {&band, &below}) {
            part->events = 0;
            for(const double count : part->counts) {
                part->events += static_cast<size_t>(count);
            }
        }
        bands.push_back(std::move(below));
    }
    // Sorted by proto-mass, the layers keep pointing to the bands above them
    std::vector<size_t> order(bands.size());
    for(size_t i = 0; i < order.size(); i++) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&bands](size_t a, size_t b) {
        const double qa = bands[a].ridge.front().y();
        const double qb = bands[b].ridge.front().y();
        return qa != qb ? qa < qb : bands[a].startTof() < bands[b].startTof();
    });
    std::vector<int> position(bands.size());
    for(size_t i = 0; i < order.size(); i++) {
        position[order[i]] = static_cast<int>(i);
    }
    std::vector<MassBand> sorted;
    for(const size_t i : order) {
        sorted.push_back(std::move(bands[i]));
        if(sorted.back().layerAbove >= 0) {
            sorted.back().layerAbove = position[static_cast<size_t>(sorted.back().layerAbove)];
        }
        sorted.back().profile.clear();
    }
    return sorted;
}

namespace {
constexpr double EdgeTolerance = 0.012; // Relative, a leading edge matches the expected one within this or 4 sigma
constexpr double EdgeSigmas = 4;
constexpr double MinAnchorSigma = 20; // Channels, an edge this sharp, or sharper than below, can anchor the identities
constexpr double AnchorSharpness = 0.015; // Relative to the edge
constexpr double GuessSigmas = 8; // Of a guessed edge, in place of EdgeSigmas
constexpr double OutlierDeviation = 0.04; // Anchors whose ridges deviate more from the response are dropped, and
constexpr double OutlierFactor = 4; // more than this many times the median deviation of the anchors
constexpr double ContinuationDeviation = 0.05; // A band following the response of another's identity this well continues it
constexpr double MinResponseSigma = 0.015; // Relative, smallest uncertainty of a ridge against the response
constexpr double ResponseSigmas = 3; // A band and an isotope match within this many uncertainties
constexpr double SurfaceLikelihood = 3; // Relative likelihood of a band whose edge is where the surface particles are
constexpr double LighterThanHydrogen = 0.1; // Relative, a band this far below the response of H is no particle
constexpr double ExtrapolationPerZ = 0.01; // Relative uncertainty of the response per unit of Z beyond the calibration
constexpr int MaxExtrapolationZ = 30; // Of the range of candidates tried

struct Candidate {
    BandIdentity identity;
    double mass; // kg, detected
    double edge; // Channel
    double prior; // Natural abundance of the isotope
};

std::vector<Candidate> surfaceCandidates(const ExperimentalSettings &settings, const DetectorSettings &detector,
                                         const PhysicsSettings &physics) {
    // As expectedSurfaceTof() in tofcalibration.h, with the loss in the timing foil of each element of the detected
    // particles tabulated once and scaled to the mass of the isotope: stopping depends on the velocity
    std::map<int, std::unique_ptr<FoilEnergyLoss>> foils;
    auto energyAfterFoil = [&](int z, double mass, double energy) {
        const double elementMass = Elements::mass(z, 0);
        auto &foil = foils[z];
        if(!foil) {
            foil = std::make_unique<FoilEnergyLoss>(z, elementMass, detector.foilMaterial, detector.foilThickness, physics);
        }
        if(!foil->isValid() || elementMass <= 0) {
            return NaN;
        }
        const double scale = mass / elementMass;
        return scale * foil->energyAfter(energy / scale);
    };
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
                QString error;
                const double energy = surfaceEnergy(&settings, detector, cut, error);
                const BandIdentity identity{z, isotope.a, type};
                const double mass = detectedMass(identity, &settings);
                if(!(energy > 0) || mass <= 0) {
                    continue;
                }
                const double after = energyAfterFoil(detectedZ(identity, &settings), mass, energy);
                if(!(after > 0)) {
                    continue;
                }
                const double edge = (energyToTof(after, mass, detector.tofLength) - detector.tofOffset) / detector.tofSlope;
                candidates.push_back({identity, mass, edge, isotope.abundance});
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

int detectedZ(const BandIdentity &identity, const ExperimentalSettings *settings) {
    if(!identity.isKnown()) {
        return 0;
    }
    return identity.type == CutType::Scattered ? (settings ? settings->beamZ : 0) : identity.z;
}

double bandDeviation(const MassBand &band, const BandIdentity &identity, const ExperimentalSettings &settings,
                     EnergyDetectorModel &model, const EnergyDetectorCalibration &calibration) {
    const int z = detectedZ(identity, &settings);
    const double mass = detectedMass(identity, &settings);
    if(z <= 0 || mass <= 0 || !calibration.valid) {
        return NaN;
    }
    const DetectorSettings &detector = model.detector();
    double sum = 0;
    double weights = 0;
    // Near the edge, where the energies are highest and the response best known
    const double start = std::isfinite(band.edgeTof) ? band.edgeTof - 2 * band.edgeSigma : band.startTof();
    for(size_t k = 0; k < band.ridge.size(); k++) {
        if(band.ridge[k].x() < start || band.ridge[k].x() > start + DeviationSpan) {
            continue;
        }
        const double predicted = model.energyChannel(calibration, z, mass, band.ridge[k].x());
        const double observed = energyChannel(band.ridge[k].x(), band.ridge[k].y(), detector.tofSlope, detector.tofOffset);
        if(predicted > 0 && observed > 0) {
            sum += band.counts[k] * std::pow(std::log(observed / predicted), 2);
            weights += band.counts[k];
        }
    }
    return weights > 0 ? std::sqrt(sum / weights) : NaN;
}

EnergyDetectorFit calibrateFromBands(const std::vector<MassBand> &bands, const std::vector<BandIdentity> &identities,
                                     const ExperimentalSettings &settings, EnergyDetectorModel &model) {
    std::vector<EnergyDetectorPoint> points;
    const DetectorSettings &detector = model.detector();
    for(size_t i = 0; i < bands.size() && i < identities.size(); i++) {
        const int z = detectedZ(identities[i], &settings);
        const double mass = detectedMass(identities[i], &settings);
        if(z <= 0 || mass <= 0) {
            continue;
        }
        const MassBand &band = bands[i];
        for(size_t k = 0; k < band.ridge.size(); k++) {
            // The ridge is extended a little before the leading edge by the window of slices
            if(std::isfinite(band.edgeTof) && band.ridge[k].x() < band.edgeTof - 2 * band.edgeSigma) {
                continue;
            }
            points.push_back({z, mass, band.ridge[k].x(),
                              energyChannel(band.ridge[k].x(), band.ridge[k].y(), detector.tofSlope, detector.tofOffset),
                              band.counts[k]});
        }
    }
    return fitEnergyDetector(model, points);
}

IdentityProposal proposeIdentities(const std::vector<MassBand> &bands, const ExperimentalSettings &settings,
                                   const DetectorSettings &detector, EnergyDetectorModel &model,
                                   const PhysicsSettings &physics, const std::vector<BandIdentity> &given) {
    IdentityProposal proposal;
    proposal.bands.resize(bands.size());
    if(detector.tofSlope <= 0 || detector.tofLength <= 0 || settings.beamZ <= 0 || settings.beamEnergy <= 0) {
        proposal.calibration.errorMessage = QStringLiteral("The experimental settings and the ToF calibration are needed.");
        return proposal;
    }
    std::vector<BandIdentity> identities(bands.size());
    for(size_t b = 0; b < bands.size() && b < given.size(); b++) {
        if(given[b].isKnown()) {
            identities[b] = given[b];
            proposal.bands[b].basis = IdentityBasis::User;
        }
    }
    auto taken = [&identities](const BandIdentity &identity) {
        return std::find(identities.begin(), identities.end(), identity) != identities.end();
    };
    // The stopping of all elements in the foils, the window and the gas is needed, it is read at once
    QList<int> z1s;
    for(int z = 1; z <= Elements::maxZ(); z++) {
        z1s.append(z);
    }
    QList<int> targets;
    for(const Material *material : {&detector.foilMaterial, &detector.stopFoilMaterial, &detector.windowMaterial, &detector.gasMaterial}) {
        for(const MaterialElement &element : material->elements) {
            if(!targets.contains(element.z)) {
                targets.append(element.z);
            }
        }
    }
    prepareStopping(z1s, targets, physics);
    const std::vector<Candidate> candidates = surfaceCandidates(settings, detector, physics);
    auto edgeTolerance = [](const MassBand &band) {
        return std::max(EdgeTolerance * band.edgeTof, (band.edgeFitted ? EdgeSigmas : GuessSigmas) * band.edgeSigma);
    };

    // Anchors: bands with a sharp fitted leading edge where particles from the surface of some isotope are expected,
    // scored by the match and the abundance
    struct Option {
        size_t candidate;
        double score;
    };
    std::vector<std::vector<Option>> options(bands.size());
    for(size_t b = 0; b < bands.size(); b++) {
        const MassBand &band = bands[b];
        if(identities[b].isKnown() || !band.edgeFitted || band.layerAbove >= 0
           || band.edgeSigma > std::max(MinAnchorSigma, AnchorSharpness * band.edgeTof)) {
            continue;
        }
        const double tolerance = edgeTolerance(band);
        for(size_t c = 0; c < candidates.size(); c++) {
            const double difference = (candidates[c].edge - band.edgeTof) / tolerance;
            if(std::abs(difference) <= 1 && !taken(candidates[c].identity)) {
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
    for(size_t i = 0; i < order.size(); i++) {
        const size_t b = order[i];
        if(best.chosen[i] >= 0) {
            const Candidate &candidate = candidates[options[b][static_cast<size_t>(best.chosen[i])].candidate];
            identities[b] = candidate.identity;
            proposal.bands[b].basis = IdentityBasis::Edge;
            proposal.bands[b].expectedEdge = candidate.edge;
        }
    }

    // Calibration from the anchors and the given identities, without anchors whose ridges follow the response clearly
    // worse than the others: an edge that matches by chance. How well the response can follow the bands depends on the
    // detector, so the limit is relative to the typical deviation.
    EnergyDetectorFit fit = calibrateFromBands(bands, identities, settings, model);
    while(fit.ok) {
        std::vector<std::pair<double, size_t>> deviations;
        for(size_t b = 0; b < bands.size(); b++) {
            if(proposal.bands[b].basis == IdentityBasis::Edge) {
                const double deviation = bandDeviation(bands[b], identities[b], settings, model, fit.calibration);
                deviations.push_back({std::isfinite(deviation) ? deviation : 1e300, b});
            }
        }
        if(deviations.size() < 3) {
            break;
        }
        std::sort(deviations.begin(), deviations.end());
        const double typical = deviations[deviations.size() / 2].first;
        const auto [worstDeviation, worst] = deviations.back();
        if(worstDeviation <= std::max(OutlierDeviation, OutlierFactor * typical)) {
            break;
        }
        if(worst == bands.size()) {
            break;
        }
        identities[worst] = {};
        proposal.bands[worst].basis = IdentityBasis::None;
        const EnergyDetectorFit refit = calibrateFromBands(bands, identities, settings, model);
        if(!refit.ok) {
            break;
        }
        fit = refit;
    }
    if(!fit.ok && detector.energyCalibration.valid) {
        fit.calibration = detector.energyCalibration;
        fit.rmsResidual = NaN;
        fit.ok = true;
        fit.errorMessage.clear();
    }

    if(fit.ok) {
        // Bands without an identity that start in the later half of an identified band and follow its response
        // continue it. Layers split off a band above them do not.
        for(size_t b = 0; b < bands.size(); b++) {
            if(identities[b].isKnown() || bands[b].layerAbove >= 0) {
                continue;
            }
            double bestDeviation = ContinuationDeviation;
            for(size_t other = 0; other < bands.size(); other++) {
                const MassBand &parent = bands[other];
                if(!identities[other].isKnown() || bands[b].startTof() < (parent.startTof() + parent.endTof()) / 2) {
                    continue;
                }
                const double deviation = bandDeviation(bands[b], identities[other], settings, model, fit.calibration);
                if(deviation < bestDeviation) {
                    bestDeviation = deviation;
                    proposal.bands[b].continues = static_cast<int>(other);
                }
            }
        }

        // The other bands get the most likely isotope by how well their ridges follow the response, their leading
        // edges (no particles come before those from the surface, and particles from the surface are more likely) and
        // the abundance. Layers are not of the element above them. Candidates are tried in order of mass from where the
        // response is below the ridge until it is well above it.
        std::vector<size_t> byMass(candidates.size());
        for(size_t c = 0; c < byMass.size(); c++) {
            byMass[c] = c;
        }
        std::sort(byMass.begin(), byMass.end(), [&](size_t a, size_t c) { return candidates[a].mass < candidates[c].mass; });
        // The response is trusted up to the ToF where identified bands of two particles still have ridges. Bands that
        // start deeper are left to the user, except layers, which are tied to the band above them.
        std::vector<double> ends;
        for(size_t b = 0; b < bands.size(); b++) {
            if(identities[b].isKnown()) {
                ends.push_back(bands[b].endTof());
            }
        }
        std::sort(ends.begin(), ends.end(), std::greater<>());
        const double supported = ends.size() >= 2 ? ends[1] : -1;
        const double hydrogenMass = Elements::mass(1, 1);
        for(size_t b = 0; b < bands.size(); b++) {
            const MassBand &band = bands[b];
            if(identities[b].isKnown() || proposal.bands[b].continues >= 0 || band.ridge.empty()) {
                continue;
            }
            // Lighter than H along most of the ridge, it may join H at its end
            double lighter = 0;
            double total = 0;
            for(size_t k = 0; k < band.ridge.size(); k++) {
                const double hydrogen = model.energyChannel(fit.calibration, 1, hydrogenMass, band.ridge[k].x());
                const double observed = energyChannel(band.ridge[k].x(), band.ridge[k].y(), detector.tofSlope, detector.tofOffset);
                if(hydrogen > 0 && std::log(observed / hydrogen) < -LighterThanHydrogen) {
                    lighter += band.counts[k];
                }
                total += band.counts[k];
            }
            if(lighter > total / 2) {
                proposal.bands[b].artifact = true;
                continue;
            }
            if(band.layerAbove < 0 && band.startTof() > supported) {
                continue;
            }
            // Where the ridge has the most events
            const size_t peak = static_cast<size_t>(std::max_element(band.counts.begin(), band.counts.end()) - band.counts.begin());
            const double tof = band.ridge[peak].x();
            const double observed = energyChannel(tof, band.ridge[peak].y(), detector.tofSlope, detector.tofOffset);
            const int aboveZ = band.layerAbove >= 0 ? identities[static_cast<size_t>(band.layerAbove)].z : 0;
            const double bandSigma = std::max(MinResponseSigma, band.widthAt(tof));
            // Less certain beyond the Z of the calibration, known only when it was fitted here
            auto sigmaOf = [&](const Candidate &candidate) {
                const int z = detectedZ(candidate.identity, &settings);
                const int beyond = fit.maxZ > 0 ? std::max({0, z - fit.maxZ, fit.minZ - z}) : 0;
                return std::hypot(bandSigma, ExtrapolationPerZ * beyond);
            };
            const double widest = std::hypot(bandSigma, ExtrapolationPerZ * MaxExtrapolationZ);
            auto logRatio = [&](size_t c) {
                const Candidate &candidate = candidates[c];
                const double predicted = model.energyChannel(fit.calibration, detectedZ(candidate.identity, &settings),
                                                             candidate.mass, tof);
                return predicted > 0 ? std::log(predicted / observed) : -1e300;
            };
            // The lightest candidate whose response is not far below the ridge, the response grows with the mass
            const double lowest = -ResponseSigmas * widest;
            size_t first = 0;
            size_t last = byMass.size();
            while(first < last) {
                const size_t middle = (first + last) / 2;
                if(logRatio(byMass[middle]) < lowest) {
                    first = middle + 1;
                } else {
                    last = middle;
                }
            }
            double bestScore = 0;
            for(size_t i = first; i < byMass.size(); i++) {
                const Candidate &candidate = candidates[byMass[i]];
                const double ratio = logRatio(byMass[i]);
                if(ratio < lowest) {
                    continue;
                }
                if(ratio > ResponseSigmas * widest) {
                    break; // Heavier ones are farther above it
                }
                const double sigma = sigmaOf(candidate);
                if(candidate.identity.z == aboveZ || taken(candidate.identity)) {
                    continue;
                }
                double edgeFactor = 1;
                if(std::isfinite(band.edgeTof)) {
                    // A guessed edge is not sure enough to rule out particles from the surface coming later
                    const double edgeDifference = (band.edgeTof - candidate.edge) / edgeTolerance(band);
                    if(edgeDifference < -1 && band.edgeFitted) {
                        continue;
                    }
                    if(edgeDifference <= 1 && band.layerAbove < 0) {
                        edgeFactor = SurfaceLikelihood;
                    }
                }
                const double deviation = bandDeviation(band, candidate.identity, settings, model, fit.calibration);
                if(!(deviation <= ResponseSigmas * sigma)) {
                    continue;
                }
                const double score = std::exp(-0.5 * std::pow(deviation / sigma, 2)) * edgeFactor * (0.05 + candidate.prior);
                if(score > bestScore) {
                    bestScore = score;
                    identities[b] = candidate.identity;
                    proposal.bands[b].expectedEdge = edgeFactor > 1 ? candidate.edge : NaN;
                }
            }
            if(identities[b].isKnown()) {
                proposal.bands[b].basis = IdentityBasis::Response;
            }
        }
    }
    for(size_t b = 0; b < bands.size(); b++) {
        proposal.bands[b].identity = identities[b];
        proposal.bands[b].deviation = fit.ok ? bandDeviation(bands[b], identities[b], settings, model, fit.calibration) : NaN;
        if(proposal.bands[b].basis == IdentityBasis::None || proposal.bands[b].basis == IdentityBasis::User) {
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
    BandCentre(const SelectionBand &band, EnergyDetectorModel &model, const EnergyDetectorCalibration &calibration)
        : m_band(band), m_model(model), m_calibration(calibration) {
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
        if(m_band.z > 0 && m_calibration.valid) {
            const double shift = m_ridge.back().y() - response(end);
            const double value = response(tof) + shift;
            if(std::isfinite(value)) {
                return value;
            }
        }
        return extrapolate(m_ridge, tof);
    }

private:
    // Proto-mass of the response of the energy detector
    double response(double tof) const {
        const DetectorSettings &detector = m_model.detector();
        const double channel = m_model.energyChannel(m_calibration, m_band.z, m_band.mass, tof);
        const double t = (detector.tofSlope * tof + detector.tofOffset) / Units::Nanosecond;
        return channel > 0 && t > 0 ? std::log(channel * t * t) : NaN;
    }

    const SelectionBand &m_band;
    EnergyDetectorModel &m_model;
    const EnergyDetectorCalibration &m_calibration;
    std::vector<QPointF> m_ridge;
};

struct Component {
    double centre;
    double sigma;
    double weight; // Events
    double maxSigma; // A band fades out with depth, its width is kept near that of its ridge instead of taking the background
    bool active = true; // A layer has no events before it begins, the layer above none after it
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
                responsibilities[c] = component.active && z * z < 100
                                              ? component.weight / (component.sigma * std::sqrt(2 * M_PI)) * std::exp(-0.5 * z * z)
                                              : 0;
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

}

std::vector<BandSelection> selectBands(std::span<const ToFEEvent> events, const std::vector<SelectionBand> &bands,
                                       EnergyDetectorModel &model, const EnergyDetectorCalibration &calibration,
                                       const SelectionOptions &options) {
    const double tofSlope = model.detector().tofSlope;
    const double tofOffset = model.detector().tofOffset;
    std::vector<BandSelection> selections(bands.size());
    if(bands.empty() || tofSlope <= 0) {
        return selections;
    }
    std::vector<BandCentre> centres;
    for(const SelectionBand &band : bands) {
        centres.emplace_back(band, model, calibration);
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
            const MassBand &band = *bands[b].band;
            // No events of a band before its leading edge, nor of a layer above after the layer below it begins
            const bool active = !(middle > band.layerEnd) && middle >= starts[b];
            slice.components.push_back({centre, width, 1, std::min(MaxWidth, MaxWidthGrowth * width), active});
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
            if(!component.active) {
                component.weight = 0;
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
            if(!own.active && slice.low > bands[b].band->layerEnd) {
                if(!edges.empty()) {
                    edges.push_back({bands[b].band->layerEnd, edges.back().lower, edges.back().upper, 0, 0});
                }
                selection.endReason = QStringLiteral("the layer below begins");
                break;
            }
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
