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
#include <gsl/gsl_multifit.h>
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

// With the shift of the fit taken out, nullptr for none
TimingHistogram makeHistogram(const std::vector<Selected> &events, double binWidth, const AngleFit *shift, double origin,
                              size_t bins) {
    TimingHistogram histogram;
    histogram.origin = origin;
    histogram.binWidth = binWidth;
    histogram.counts.assign(bins, 0);
    for(const Selected &event : events) {
        const double x = event.timing - (shift ? shift->shiftAt(event.tof) : 0);
        const double bin = std::floor((x - histogram.binStart(0)) / binWidth);
        if(bin >= 0 && bin < static_cast<double>(bins)) {
            histogram.counts[static_cast<size_t>(bin)]++;
        }
    }
    return histogram;
}

// Initial values from the fit of all events, the range moved by shift, or without a shift (NaN) to the slice's events
// in it
WindowFit sliceInitial(const std::vector<Selected> &events, size_t begin, size_t end, const WindowFit &all, double shift) {
    WindowFit initial = all;
    if(!std::isfinite(shift)) {
        double sum = 0;
        size_t count = 0;
        for(size_t i = begin; i < end; i++) {
            if(events[i].timing >= all.fitMin && events[i].timing < all.fitMax) {
                sum += events[i].timing;
                count++;
            }
        }
        shift = count > 0 ? sum / static_cast<double>(count) - all.centre : 0;
    }
    initial.centre += shift;
    initial.fitMin += shift;
    initial.fitMax += shift;
    initial.area = 0;
    for(size_t i = begin; i < end; i++) {
        if(events[i].timing >= initial.fitMin && events[i].timing < initial.fitMax) {
            initial.area++;
        }
    }
    initial.background = 0;
    return initial;
}

struct SliceRange {
    size_t begin;
    size_t end;
};

// Centres of the slices with the width and resolution of the window, which is that of all events with the shift of
// the fit taken out. Without a shift (shift nullptr), each slice starts from the average timing of its events.
void fitSlices(const std::vector<Selected> &selected, const std::vector<SliceRange> &ranges, const WindowFit &window,
               const AngleFit *shift, double binWidth, double origin, size_t bins, std::vector<SliceCentre> &slices) {
    slices.clear();
    for(const SliceRange &range : ranges) {
        SliceCentre slice;
        slice.events = range.end - range.begin;
        for(size_t i = range.begin; i < range.end; i++) {
            slice.tof += selected[i].tof;
        }
        slice.tof /= std::max<double>(1, static_cast<double>(slice.events));
        if(slice.events >= MinSliceEvents) {
            const std::vector<Selected> part(selected.begin() + static_cast<std::ptrdiff_t>(range.begin),
                                             selected.begin() + static_cast<std::ptrdiff_t>(range.end));
            const TimingHistogram sliceHistogram = makeHistogram(part, binWidth, nullptr, origin, bins);
            const double expected = shift ? shift->shiftAt(slice.tof) : std::numeric_limits<double>::quiet_NaN();
            const WindowFit sliceFit = fitWindow(sliceHistogram,
                                                 sliceInitial(selected, range.begin, range.end, window, expected), true);
            if(sliceFit.ok && sliceFit.centreError > 0) {
                slice.ok = true;
                slice.centre = sliceFit.centre;
                slice.centreError = sliceFit.centreError;
            }
        }
        slices.push_back(slice);
    }
}

// Weighted polynomial in ToF - referenceTof through the centres of the slices of the fit, into its coefficients and
// order. False, with the centre message, when it cannot be fitted.
bool fitPolynomial(AngleFit &fit, int order) {
    std::vector<const SliceCentre *> okSlices;
    for(const SliceCentre &slice : fit.slices) {
        if(slice.ok) {
            okSlices.push_back(&slice);
        }
    }
    const auto parameters = static_cast<size_t>(order) + 1;
    const bool distinct = okSlices.size() >= 2 && okSlices.front()->tof != okSlices.back()->tof;
    if(okSlices.size() < parameters || !distinct) {
        fit.centreMessage = QString("The centres of at least %1 slices are needed for the polynomial of order %2.")
                                    .arg(parameters)
                                    .arg(order);
        return false;
    }
    const size_t n = okSlices.size();
    gsl_matrix *x = gsl_matrix_alloc(n, parameters);
    gsl_vector *y = gsl_vector_alloc(n);
    gsl_vector *w = gsl_vector_alloc(n);
    gsl_vector *c = gsl_vector_alloc(parameters);
    gsl_matrix *covariance = gsl_matrix_alloc(parameters, parameters);
    for(size_t i = 0; i < n; i++) {
        const double d = okSlices[i]->tof - fit.referenceTof;
        double power = 1;
        for(size_t k = 0; k < parameters; k++) {
            gsl_matrix_set(x, i, k, power);
            power *= d;
        }
        gsl_vector_set(y, i, okSlices[i]->centre);
        gsl_vector_set(w, i, 1 / (okSlices[i]->centreError * okSlices[i]->centreError));
    }
    gsl_multifit_linear_workspace *workspace = gsl_multifit_linear_alloc(n, parameters);
    gsl_error_handler_t *handler = gsl_set_error_handler_off();
    double chiSquared = 0;
    const int status = gsl_multifit_wlinear(x, w, y, c, covariance, &chiSquared, workspace);
    gsl_set_error_handler(handler);
    const double scale = n > parameters ? std::max(1.0, chiSquared / static_cast<double>(n - parameters)) : 1;
    bool finite = status == GSL_SUCCESS;
    for(size_t k = 0; k < parameters; k++) {
        finite = finite && std::isfinite(gsl_vector_get(c, k));
    }
    if(finite) {
        fit.order = order;
        fit.coefficients = {};
        fit.errors = {};
        for(size_t k = 1; k < parameters; k++) {
            fit.coefficients[k] = gsl_vector_get(c, k);
            fit.errors[k] = std::sqrt(gsl_matrix_get(covariance, k, k) * scale);
        }
        fit.sliceChiSquared = n > parameters ? chiSquared / static_cast<double>(n - parameters)
                                             : std::numeric_limits<double>::quiet_NaN();
        fit.centreMessage.clear();
    } else {
        fit.centreMessage = QStringLiteral("The polynomial through the centres of the slices could not be fitted.");
    }
    gsl_multifit_linear_free(workspace);
    gsl_matrix_free(covariance);
    gsl_vector_free(c);
    gsl_vector_free(w);
    gsl_vector_free(y);
    gsl_matrix_free(x);
    return finite;
}
}

double WindowFit::value(double x, double binWidth) const {
    return windowCounts(x - binWidth / 2, x + binWidth / 2, centre, halfWidth, sigma, area, background);
}

AngleCalibration AngleFit::calibration(bool reversed) const {
    AngleCalibration calibration;
    calibration.valid = ok;
    calibration.referenceTof = referenceTof;
    calibration.tofMin = tofMin;
    calibration.tofMax = tofMax;
    calibration.centre = coefficients;
    calibration.centre[0] = window.centre;
    calibration.centreOrder = order;
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
        if(std::isfinite(event.timing) && event.tof >= options.minTof && event.tof <= options.maxTof) {
            selected.push_back({event.tof, event.timing});
        }
    }
    fit.events = selected.size();
    fit.tofMin = options.minTof;
    fit.tofMax = options.maxTof;
    if(selected.empty()) {
        fit.errorMessage = QStringLiteral("There are no events with a timing value between the minimum and maximum ToF.");
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
    TimingHistogram histogram = makeHistogram(selected, binWidth, nullptr, origin, bins);
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

    // Centres of the slices with the width of all events. Fitted also for a constant centre, the residuals show
    // whether it is enough.
    const int slices = std::max(1, options.slices);
    const int order = std::clamp(options.order, 0, MaxCentreOrder);
    fit.tofMin = options.minTof;
    fit.tofMax = options.maxTof;
    std::vector<SliceRange> ranges;
    if(slices > 1) {
        // Equal widths over the ToF of the events, not over empty parts of the range
        const double firstTof = selected.front().tof;
        const double tofSpan = selected.back().tof - firstTof;
        auto boundary = [&](int s) -> size_t {
            if(s >= slices) {
                return selected.size();
            }
            if(!options.equalTofSlices) {
                return selected.size() * static_cast<size_t>(s) / static_cast<size_t>(slices);
            }
            const double tof = firstTof + tofSpan * s / slices;
            return static_cast<size_t>(std::lower_bound(selected.begin(), selected.end(), tof,
                                                        [](const Selected &event, double value) { return event.tof < value; })
                                       - selected.begin());
        };
        for(int s = 0; s < slices; s++) {
            ranges.push_back({boundary(s), boundary(s + 1)});
        }
    }
    if(all.ok) {
        fitSlices(selected, ranges, all, nullptr, binWidth, origin, bins, fit.slices);
    }

    // The width and resolution with the shift of the centre taken out
    auto refitWindow = [&]() {
        histogram = makeHistogram(selected, binWidth, &fit, origin, bins);
        initial = all.ok ? all : initial;
        if(manualRange) {
            initial.fitMin = options.fitMin;
            initial.fitMax = options.fitMax;
        }
        all = fitWindow(histogram, initial, false);
    };
    if(all.ok && order > 0 && fitPolynomial(fit, order)) {
        refitWindow();
        // Second pass: the slices with the width and resolution of the window without the shift, which the shift
        // widened in the first pass, and starting from the centres of the first polynomial. Kept only when it works.
        if(all.ok) {
            AngleFit second = fit;
            fitSlices(selected, ranges, all, &fit, binWidth, origin, bins, second.slices);
            if(fitPolynomial(second, order)) {
                fit.slices = std::move(second.slices);
                fit.coefficients = second.coefficients;
                fit.errors = second.errors;
                fit.sliceChiSquared = second.sliceChiSquared;
                refitWindow();
            }
        }
    }
    fit.histogram = std::move(histogram);
    fit.window = all;
    fit.ok = all.ok;
    // Residuals of the slices from the final centre
    if(all.ok) {
        for(SliceCentre &slice : fit.slices) {
            slice.residual = slice.ok ? slice.centre - fit.centreAt(slice.tof) : 0;
        }
    }
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

QString centreText(const AngleCalibration &calibration, int centreDigits, int coefficientDigits) {
    QString text = QString::number(calibration.centre[0], 'g', centreDigits);
    const QString d = calibration.referenceTof == 0
                              ? QStringLiteral("ToF")
                              : QString("(ToF − %1)").arg(QString::number(calibration.referenceTof, 'g', 6));
    auto term = [&](double coefficient, const QString &variable) {
        if(coefficient != 0) {
            text += QString(" %1 %2 × %3")
                            .arg(coefficient < 0 ? "−" : "+", QString::number(std::abs(coefficient), 'g', coefficientDigits),
                                 variable);
        }
    };
    const QString powers[] = {QString(), QString(), QStringLiteral("²"), QStringLiteral("³")};
    for(size_t k = 1; k < calibration.centre.size(); k++) {
        term(calibration.centre[k], d + powers[std::min<size_t>(k, 3)]);
    }
    return text + QStringLiteral(" ticks");
}

double windowHalfAngle(const DetectorSettings &detector) {
    if(detector.detectorDistance <= 0 || detector.windowWidth <= 0) {
        return 0;
    }
    return std::atan(detector.windowWidth / 2 / detector.detectorDistance);
}

double angleOffset(const AngleCalibration &calibration, double distance, double windowWidth, const ToFEEvent &event) {
    if(!calibration.valid || calibration.halfWidth <= 0 || distance <= 0 || windowWidth <= 0 || !std::isfinite(event.timing)
       || event.tof < calibration.tofMin || event.tof > calibration.tofMax) {
        return 0;
    }
    double position = std::clamp((event.timing - calibration.centreAt(event.tof)) / calibration.halfWidth, -1.0, 1.0);
    if(calibration.reversed) {
        position = -position;
    }
    return std::atan(position * windowWidth / 2 / distance);
}
