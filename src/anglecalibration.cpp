/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#include "anglecalibration.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

#include <gsl/gsl_blas.h>
#include <gsl/gsl_cdf.h>
#include <gsl/gsl_fit.h>
#include <gsl/gsl_multifit_nlinear.h>
#include <gsl/gsl_randist.h>

namespace {
constexpr size_t MaxBins = 100000;
constexpr double OutlierFraction = 0.001; // Of the timing values on each side, left out of the histogram range
constexpr double SmoothedTicks = 3; // Half width of the moving average used for finding the window
constexpr double MarginSigmas = 4; // Fit range beyond the edges of the window
constexpr double MarginBins = 3; // Also this many bins
constexpr int FitPasses = 4; // Of weights from the previous pass, see fitWindow()
constexpr double MinWeightCounts = 0.1; // Weights are at most 1 / sqrt(this)
constexpr size_t MinSliceEvents = 100;

// Antiderivative of the cumulative normal distribution
double integratedCdf(double z) {
    return z * gsl_cdf_ugaussian_P(z) + gsl_ran_ugaussian_pdf(z);
}

// Counts from a to b of the window distribution
double windowCounts(double a, double b, double centre, double halfWidth, double sigma, double area, double background) {
    auto g = [&](double x, double edge) { return integratedCdf((x - edge) / sigma); };
    const double low = centre - halfWidth;
    const double high = centre + halfWidth;
    const double integral = sigma * (g(b, low) - g(a, low) - g(b, high) + g(a, high));
    return background * (b - a) + area / (2 * halfWidth) * integral;
}

std::vector<double> smoothed(const std::vector<double> &counts, int halfWidth) {
    const auto n = static_cast<int>(counts.size());
    std::vector<double> result(counts.size());
    for(int i = 0; i < n; i++) {
        const int first = std::max(0, i - halfWidth);
        const int last = std::min(n - 1, i + halfWidth);
        double sum = 0;
        for(int j = first; j <= last; j++) {
            sum += counts[j];
        }
        result[i] = sum / (last - first + 1);
    }
    return result;
}

// Fit parameters: centre, area and background, and unless fixed ln(halfWidth) and ln(sigma)
enum Parameter { Centre, Area, Background, LogHalfWidth, LogSigma, ParameterCount };
constexpr size_t FixedWidthParameters = 3;

struct FitData {
    const TimingHistogram *histogram;
    size_t first; // Bin
    std::vector<double> weights; // 1 / standard deviation of the counts in each bin of the range
    double halfWidth; // When fixed
    double sigma;
};

struct Shape {
    double centre;
    double area;
    double background;
    double halfWidth;
    double sigma;
};

Shape shapeOf(const gsl_vector *p, const FitData &data) {
    const bool free = p->size == ParameterCount;
    return {gsl_vector_get(p, Centre), gsl_vector_get(p, Area), gsl_vector_get(p, Background),
            free ? std::exp(gsl_vector_get(p, LogHalfWidth)) : data.halfWidth,
            free ? std::exp(gsl_vector_get(p, LogSigma)) : data.sigma};
}

double modelCounts(const Shape &shape, const TimingHistogram &histogram, size_t bin) {
    const double a = histogram.binStart(bin);
    return windowCounts(a, a + histogram.binWidth, shape.centre, shape.halfWidth, shape.sigma, shape.area,
                        shape.background);
}

int residuals(const gsl_vector *p, void *params, gsl_vector *f) {
    const auto *data = static_cast<const FitData *>(params);
    const Shape shape = shapeOf(p, *data);
    for(size_t i = 0; i < f->size; i++) {
        const size_t bin = data->first + i;
        const double model = modelCounts(shape, *data->histogram, bin);
        gsl_vector_set(f, i, (model - data->histogram->counts[bin]) * data->weights[i]);
    }
    return GSL_SUCCESS;
}

// Fits the window in the range of the initial values. With a fixed width, only the centre, area and background are
// fitted, the half width and sigma are those of the initial values.
WindowFit fitWindow(const TimingHistogram &histogram, const WindowFit &initial, bool fixedWidth) {
    WindowFit result = initial;
    result.ok = false;
    result.centreError = 0;
    result.halfWidthError = 0;
    const double min = std::min(initial.fitMin, initial.fitMax);
    const double max = std::max(initial.fitMin, initial.fitMax);
    if(histogram.counts.empty()) {
        result.errorMessage = QStringLiteral("There are no events.");
        return result;
    }
    // Bins inside the range
    const double firstBin = std::ceil((min - histogram.binStart(0)) / histogram.binWidth);
    const double endBin = std::floor((max - histogram.binStart(0)) / histogram.binWidth);
    const auto first = static_cast<size_t>(std::max(0.0, firstBin));
    const auto end = static_cast<size_t>(std::clamp(endBin, 0.0, static_cast<double>(histogram.counts.size())));
    const size_t parameterCount = fixedWidth ? FixedWidthParameters : ParameterCount;
    if(end <= first || end - first < parameterCount + 2) {
        result.errorMessage = QString("The fit range must have at least %1 bins.").arg(parameterCount + 2);
        return result;
    }
    const size_t n = end - first;
    result.fitMin = histogram.binStart(first);
    result.fitMax = histogram.binStart(end);

    // Poisson distributed counts: the first pass weights them by the smoothed counts, the later ones by the model of
    // the previous pass. This converges to the maximum likelihood fit.
    FitData data{&histogram, first, std::vector<double>(n), initial.halfWidth, initial.sigma};
    const std::vector<double> smooth = smoothed(histogram.counts, 2);
    for(size_t i = 0; i < n; i++) {
        data.weights[i] = 1 / std::sqrt(std::max(MinWeightCounts, smooth[first + i]));
    }
    gsl_multifit_nlinear_fdf fdf;
    fdf.f = residuals;
    fdf.df = nullptr; // Finite differences
    fdf.fvv = nullptr;
    fdf.n = n;
    fdf.p = parameterCount;
    fdf.params = &data;

    double start[ParameterCount];
    start[Centre] = initial.centre;
    start[Area] = initial.area;
    start[Background] = initial.background;
    start[LogHalfWidth] = std::log(std::max(0.5, initial.halfWidth));
    start[LogSigma] = std::log(std::max(0.1, initial.sigma));
    gsl_vector_view startView = gsl_vector_view_array(start, parameterCount);

    const gsl_multifit_nlinear_parameters parameters = gsl_multifit_nlinear_default_parameters();
    gsl_multifit_nlinear_workspace *workspace = gsl_multifit_nlinear_alloc(gsl_multifit_nlinear_trust, &parameters,
                                                                           fdf.n, fdf.p);
    // GSL aborts on errors by default, the fit reports them instead
    gsl_error_handler_t *handler = gsl_set_error_handler_off();
    int status = GSL_SUCCESS;
    for(int pass = 0; pass < FitPasses && status == GSL_SUCCESS; pass++) {
        if(pass > 0) {
            const gsl_vector *p = gsl_multifit_nlinear_position(workspace);
            const Shape shape = shapeOf(p, data);
            for(size_t i = 0; i < n; i++) {
                data.weights[i] = 1 / std::sqrt(std::max(MinWeightCounts, modelCounts(shape, histogram, first + i)));
            }
            gsl_vector_memcpy(&startView.vector, p);
        }
        status = gsl_multifit_nlinear_init(&startView.vector, &fdf, workspace);
        int info = 0;
        if(status == GSL_SUCCESS) {
            status = gsl_multifit_nlinear_driver(500, 1e-8, 1e-8, 0, nullptr, nullptr, &info, workspace);
        }
    }
    const gsl_vector *p = gsl_multifit_nlinear_position(workspace);
    double chiSquared = 0;
    gsl_blas_ddot(gsl_multifit_nlinear_residual(workspace), gsl_multifit_nlinear_residual(workspace), &chiSquared);
    gsl_matrix *covariance = gsl_matrix_alloc(parameterCount, parameterCount);
    gsl_matrix *j = gsl_multifit_nlinear_jac(workspace);
    const bool hasCovariance = status == GSL_SUCCESS && gsl_multifit_nlinear_covar(j, 0, covariance) == GSL_SUCCESS;
    const Shape shape = shapeOf(p, data);
    result.centre = shape.centre;
    result.area = shape.area;
    result.background = shape.background;
    result.halfWidth = shape.halfWidth;
    result.sigma = shape.sigma;
    if(hasCovariance && n > parameterCount) {
        // Scaled by the reduced chi squared when it is above one. The counts of single ticks are not quite Poisson
        // distributed, the ticks are not all equally wide.
        const double scale = std::max(1.0, chiSquared / static_cast<double>(n - parameterCount));
        result.centreError = std::sqrt(gsl_matrix_get(covariance, Centre, Centre) * scale);
        if(!fixedWidth) {
            // Of ln(halfWidth), times halfWidth
            result.halfWidthError = result.halfWidth * std::sqrt(gsl_matrix_get(covariance, LogHalfWidth, LogHalfWidth) * scale);
        }
    }
    gsl_matrix_free(covariance);
    gsl_multifit_nlinear_free(workspace);
    gsl_set_error_handler(handler);

    if(status != GSL_SUCCESS) {
        result.errorMessage = QString("The fit failed: %1.").arg(gsl_strerror(status));
    } else if(!std::isfinite(result.centre) || !std::isfinite(result.halfWidth) || !std::isfinite(result.sigma)
              || result.area <= 0) {
        result.errorMessage = QStringLiteral("The fit did not find the window.");
    } else if(result.centre < result.fitMin || result.centre > result.fitMax) {
        result.errorMessage = QStringLiteral("The fitted centre is outside the fit range.");
    } else {
        result.ok = true;
    }
    return result;
}

// First guess: the edges are where the smoothed counts cross half of their highest value
WindowFit findWindow(const TimingHistogram &histogram) {
    WindowFit window;
    const int halfWidth = std::max(1, static_cast<int>(std::lround(SmoothedTicks / histogram.binWidth)));
    const std::vector<double> smooth = smoothed(histogram.counts, halfWidth);
    const auto highest = std::max_element(smooth.begin(), smooth.end());
    if(highest == smooth.end() || *highest <= 0) {
        window.errorMessage = QStringLiteral("There are no events.");
        return window;
    }
    const double half = *highest / 2;
    const auto peak = static_cast<size_t>(highest - smooth.begin());
    size_t low = peak;
    while(low > 0 && smooth[low - 1] >= half) {
        low--;
    }
    size_t high = peak;
    while(high + 1 < smooth.size() && smooth[high + 1] >= half) {
        high++;
    }
    const double lowEdge = histogram.binStart(low);
    const double highEdge = histogram.binStart(high + 1);
    window.centre = (lowEdge + highEdge) / 2;
    window.halfWidth = std::max(histogram.binWidth, (highEdge - lowEdge) / 2);
    window.sigma = std::max(0.5, window.halfWidth / 10);
    const double margin = MarginSigmas * window.sigma + MarginBins * histogram.binWidth;
    window.fitMin = window.centre - window.halfWidth - margin;
    window.fitMax = window.centre + window.halfWidth + margin;
    window.background = 0;
    window.area = 0;
    for(size_t bin = 0; bin < histogram.counts.size(); bin++) {
        const double x = histogram.binStart(bin);
        if(x >= window.fitMin && x < window.fitMax) {
            window.area += histogram.counts[bin];
        }
    }
    window.ok = true;
    return window;
}

struct Selected {
    double tof;
    double timing;
};

TimingHistogram makeHistogram(const std::vector<Selected> &events, double binWidth, double slope, double referenceTof,
                              double origin, size_t bins) {
    TimingHistogram histogram;
    histogram.origin = origin;
    histogram.binWidth = binWidth;
    histogram.counts.assign(bins, 0);
    for(const Selected &event : events) {
        const double x = event.timing - slope * (event.tof - referenceTof);
        const double bin = std::floor((x - histogram.binStart(0)) / binWidth);
        if(bin >= 0 && bin < static_cast<double>(bins)) {
            histogram.counts[static_cast<size_t>(bin)]++;
        }
    }
    return histogram;
}

// Initial values from the fit of all events, the range moved to the slice's events in it
WindowFit sliceInitial(const std::vector<Selected> &events, size_t begin, size_t end, const WindowFit &all) {
    WindowFit initial = all;
    double sum = 0;
    size_t count = 0;
    for(size_t i = begin; i < end; i++) {
        if(events[i].timing >= all.fitMin && events[i].timing < all.fitMax) {
            sum += events[i].timing;
            count++;
        }
    }
    const double centre = count > 0 ? sum / static_cast<double>(count) : all.centre;
    const double shift = centre - all.centre;
    initial.centre = centre;
    initial.fitMin += shift;
    initial.fitMax += shift;
    initial.area = static_cast<double>(count);
    initial.background = 0;
    return initial;
}
}

double WindowFit::value(double x, double binWidth) const {
    return windowCounts(x - binWidth / 2, x + binWidth / 2, centre, halfWidth, sigma, area, background);
}

AngleCalibration AngleFit::calibration(bool reversed) const {
    AngleCalibration calibration;
    calibration.valid = ok;
    calibration.centre = window.centre - centreSlope * referenceTof;
    calibration.centreSlope = centreSlope;
    calibration.halfWidth = window.halfWidth;
    calibration.sigma = window.sigma;
    calibration.reversed = reversed;
    return calibration;
}

AngleFit fitAngleCalibration(std::span<const ToFEEvent> events, const AngleFitOptions &options) {
    AngleFit fit;
    const double binWidth = std::max(1.0, std::round(options.binWidth));
    std::vector<Selected> selected;
    for(const ToFEEvent &event : events) {
        if(std::isfinite(event.timing) && event.tof <= options.maxTof) {
            selected.push_back({event.tof, event.timing});
        }
    }
    fit.events = selected.size();
    if(selected.empty()) {
        fit.errorMessage = QStringLiteral("There are no events with a timing value up to the maximum ToF.");
        return fit;
    }
    std::sort(selected.begin(), selected.end(), [](const Selected &a, const Selected &b) { return a.tof < b.tof; });
    fit.referenceTof = std::accumulate(selected.begin(), selected.end(), 0.0,
                                       [](double sum, const Selected &event) { return sum + event.tof; })
            / static_cast<double>(selected.size());

    // Range of the histogram without a few outliers, with a margin for the shift of the centre line
    std::vector<double> timings(selected.size());
    std::transform(selected.begin(), selected.end(), timings.begin(), [](const Selected &event) { return event.timing; });
    const auto outliers = static_cast<size_t>(OutlierFraction * static_cast<double>(timings.size()));
    std::nth_element(timings.begin(), timings.begin() + outliers, timings.end());
    const double low = timings[outliers];
    std::nth_element(timings.begin(), timings.end() - 1 - outliers, timings.end());
    const double high = timings[timings.size() - 1 - outliers];
    const double margin = std::max(10 * binWidth, 0.25 * (high - low));
    const double origin = std::floor(low - margin);
    const double span = std::ceil(high + margin) - origin + 1;
    if(span / binWidth > static_cast<double>(MaxBins)) {
        fit.errorMessage = QString("The timing values spread over %1 ticks, too many for the bin width.").arg(span);
        return fit;
    }
    const auto bins = static_cast<size_t>(std::ceil(span / binWidth));

    // All events without the shift
    TimingHistogram histogram = makeHistogram(selected, binWidth, 0, fit.referenceTof, origin, bins);
    WindowFit initial = findWindow(histogram);
    if(!initial.ok) {
        fit.histogram = histogram;
        fit.window = initial;
        fit.errorMessage = initial.errorMessage;
        return fit;
    }
    const bool manualRange = std::isfinite(options.fitMin) && std::isfinite(options.fitMax);
    if(manualRange) {
        initial.fitMin = options.fitMin;
        initial.fitMax = options.fitMax;
    }
    WindowFit all = fitWindow(histogram, initial, false);

    // Centres of the slices with the width of all events
    const int slices = std::max(1, options.slices);
    if(all.ok && slices > 1) {
        std::vector<double> x, y, w;
        for(int s = 0; s < slices; s++) {
            const size_t begin = selected.size() * static_cast<size_t>(s) / static_cast<size_t>(slices);
            const size_t end = selected.size() * static_cast<size_t>(s + 1) / static_cast<size_t>(slices);
            SliceCentre slice;
            slice.events = end - begin;
            for(size_t i = begin; i < end; i++) {
                slice.tof += selected[i].tof;
            }
            slice.tof /= std::max<double>(1, static_cast<double>(slice.events));
            if(slice.events >= MinSliceEvents) {
                const std::vector<Selected> part(selected.begin() + static_cast<std::ptrdiff_t>(begin),
                                                 selected.begin() + static_cast<std::ptrdiff_t>(end));
                const TimingHistogram sliceHistogram = makeHistogram(part, binWidth, 0, fit.referenceTof, origin, bins);
                const WindowFit sliceFit = fitWindow(sliceHistogram, sliceInitial(selected, begin, end, all), true);
                if(sliceFit.ok && sliceFit.centreError > 0) {
                    slice.ok = true;
                    slice.centre = sliceFit.centre;
                    slice.centreError = sliceFit.centreError;
                    x.push_back(slice.tof);
                    y.push_back(slice.centre);
                    w.push_back(1 / (slice.centreError * slice.centreError));
                }
            }
            fit.slices.push_back(slice);
        }
        if(x.size() < 2 || x.front() == x.back()) {
            fit.slopeMessage = QStringLiteral("The centres of at least two slices are needed for the slope.");
        } else {
            double c0, c1, cov00, cov01, cov11, chiSquared;
            gsl_fit_wlinear(x.data(), 1, w.data(), 1, y.data(), 1, x.size(), &c0, &c1, &cov00, &cov01, &cov11, &chiSquared);
            const double scale = x.size() > 2 ? std::max(1.0, chiSquared / static_cast<double>(x.size() - 2)) : 1;
            if(std::isfinite(c1)) {
                fit.centreSlope = c1;
                fit.centreSlopeError = std::sqrt(cov11 * scale);
            } else {
                fit.slopeMessage = QStringLiteral("The line through the centres of the slices could not be fitted.");
            }
        }
    }

    // The width and resolution with the shift of the centre taken out
    if(fit.centreSlope != 0) {
        histogram = makeHistogram(selected, binWidth, fit.centreSlope, fit.referenceTof, origin, bins);
        initial = all.ok ? all : initial;
        if(manualRange) {
            initial.fitMin = options.fitMin;
            initial.fitMax = options.fitMax;
        }
        all = fitWindow(histogram, initial, false);
    }
    fit.histogram = std::move(histogram);
    fit.window = all;
    fit.ok = all.ok;
    fit.errorMessage = all.errorMessage;
    if(!all.ok && binWidth > initial.halfWidth / 2) {
        fit.errorMessage += QStringLiteral(" The bins are too wide for the window, try narrower ones.");
    }
    return fit;
}

std::vector<ToFEEvent> smoothTiming(std::span<const ToFEEvent> events, double smoothing) {
    std::vector<ToFEEvent> result(events.begin(), events.end());
    if(smoothing <= 0) {
        return result;
    }
    std::mt19937 random(1);
    std::uniform_real_distribution<double> spread(-smoothing, smoothing);
    for(ToFEEvent &event : result) {
        event.timing += spread(random);
    }
    return result;
}

double windowHalfAngle(const DetectorSettings &detector) {
    if(detector.detectorDistance <= 0 || detector.windowWidth <= 0) {
        return 0;
    }
    return std::atan(detector.windowWidth / 2 / detector.detectorDistance);
}

double angleOffset(const AngleCalibration &calibration, double distance, double windowWidth, const ToFEEvent &event) {
    if(!calibration.valid || calibration.halfWidth <= 0 || distance <= 0 || windowWidth <= 0 || !std::isfinite(event.timing)) {
        return 0;
    }
    const double centre = calibration.centre + calibration.centreSlope * event.tof;
    double position = std::clamp((event.timing - centre) / calibration.halfWidth, -1.0, 1.0);
    if(calibration.reversed) {
        position = -position;
    }
    return std::atan(position * windowWidth / 2 / distance);
}
