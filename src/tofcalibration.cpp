/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#include "tofcalibration.h"
#include "elements.h"
#include "energyspectrum.h"
#include "stopping.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <gsl/gsl_blas.h>
#include <gsl/gsl_cdf.h>
#include <gsl/gsl_fit.h>
#include <gsl/gsl_multifit_nlinear.h>
#include <gsl/gsl_randist.h>

namespace {
constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
// The moving average used for finding the edge has about this many counts at the level the edge rises to, so that
// noise does not look like an edge when there are few counts per channel
constexpr double SmoothedCounts = 25;
constexpr int MinSmoothingHalfWidth = 2; // Channels on each side
constexpr int MaxSmoothingHalfWidth = 50;
constexpr int MinWindowHalfWidth = 10; // Channels
constexpr double WindowRiseWidths = 3; // Half width of the fit window in 10-90 % rise widths
constexpr double RiseWidthsPerSigma = 2.5631; // 10-90 % rise width of the Gaussian CDF in standard deviations
constexpr int MinFitChannels = 6;
constexpr double MinSigma = 0.3; // Channels, sharper edges cannot be told apart in integer channels
constexpr int FitPasses = 4; // Of weights from the previous pass, see fitEdge()
constexpr double MinWeightCounts = 0.1; // Weights are at most 1 / sqrt(this)

// Half width of the moving average from the average counts in the channels between the first and the last event
int smoothingHalfWidth(const std::vector<size_t> &counts) {
    const auto first = std::find_if(counts.begin(), counts.end(), [](size_t count) { return count > 0; });
    const auto last = std::find_if(counts.rbegin(), counts.rend(), [](size_t count) { return count > 0; }).base();
    if(first >= last) {
        return MinSmoothingHalfWidth;
    }
    double total = 0;
    for(auto it = first; it != last; ++it) {
        total += static_cast<double>(*it);
    }
    const double average = total / static_cast<double>(last - first);
    return std::clamp(static_cast<int>(std::ceil(SmoothedCounts / average / 2)), MinSmoothingHalfWidth, MaxSmoothingHalfWidth);
}

std::vector<double> smoothed(const std::vector<size_t> &counts, int halfWidth) {
    const auto n = static_cast<int>(counts.size());
    std::vector<double> result(counts.size());
    for(int i = 0; i < n; i++) {
        const int first = std::max(0, i - halfWidth);
        const int last = std::min(n - 1, i + halfWidth);
        double sum = 0;
        for(int j = first; j <= last; j++) {
            sum += static_cast<double>(counts[j]);
        }
        result[i] = sum / (last - first + 1);
    }
    return result;
}

// First index from start on where values reach the level, -1 when they never do
int firstAtLeast(const std::vector<double> &values, double level, int start = 0) {
    for(int i = std::max(0, start); i < static_cast<int>(values.size()); i++) {
        if(values[i] >= level) {
            return i;
        }
    }
    return -1;
}

// Fit parameters: mu, ln(sigma - MinSigma) to keep sigma above the minimum, amplitude and background
enum Parameter { Mu, LogSigma, Amplitude, Background, ParameterCount };

struct FitData {
    const std::vector<size_t> *counts;
    int windowMin;
    std::vector<double> weights; // 1 / standard deviation of the counts in each channel of the window
};

double sigmaOf(const gsl_vector *p) {
    return MinSigma + std::exp(gsl_vector_get(p, LogSigma));
}

int residuals(const gsl_vector *p, void *data, gsl_vector *f) {
    const auto *fit = static_cast<const FitData *>(data);
    const double mu = gsl_vector_get(p, Mu);
    const double sigma = sigmaOf(p);
    const double amplitude = gsl_vector_get(p, Amplitude);
    const double background = gsl_vector_get(p, Background);
    for(size_t i = 0; i < f->size; i++) {
        const auto count = static_cast<double>((*fit->counts)[fit->windowMin + i]);
        const double x = fit->windowMin + static_cast<double>(i);
        const double model = background + amplitude * gsl_cdf_ugaussian_P((x - mu) / sigma);
        gsl_vector_set(f, i, (model - count) * fit->weights[i]);
    }
    return GSL_SUCCESS;
}

int jacobian(const gsl_vector *p, void *data, gsl_matrix *j) {
    const auto *fit = static_cast<const FitData *>(data);
    const double mu = gsl_vector_get(p, Mu);
    const double sigma = sigmaOf(p);
    const double amplitude = gsl_vector_get(p, Amplitude);
    for(size_t i = 0; i < j->size1; i++) {
        const double w = fit->weights[i];
        const double z = (fit->windowMin + static_cast<double>(i) - mu) / sigma;
        const double density = gsl_ran_ugaussian_pdf(z);
        gsl_matrix_set(j, i, Mu, -amplitude * density / sigma * w);
        gsl_matrix_set(j, i, LogSigma, -amplitude * density * z / sigma * (sigma - MinSigma) * w);
        gsl_matrix_set(j, i, Amplitude, gsl_cdf_ugaussian_P(z) * w);
        gsl_matrix_set(j, i, Background, w);
    }
    return GSL_SUCCESS;
}
}

std::vector<size_t> tofHistogram(const std::vector<ToFEEvent> &events, const QList<QPointF> &polygon) {
    std::vector<size_t> counts;
    forEachEventIn(events, polygon, [&counts](const ToFEEvent &event) {
        if(event.tof < 0) {
            return;
        }
        const auto channel = static_cast<size_t>(std::lround(event.tof));
        if(channel >= counts.size()) {
            counts.resize(channel + 1, 0);
        }
        counts[channel]++;
    });
    return counts;
}

double surfaceEnergy(const ExperimentalSettings *settings, const DetectorSettings &detector, const Cut &cut,
                     QString &errorMessage) {
    if(!settings) {
        errorMessage = QStringLiteral("The measurement has no experimental settings.");
        return NaN;
    }
    if(settings->beamZ <= 0 || settings->beamEnergy <= 0) {
        errorMessage = QStringLiteral("The incident ion and the beam energy are needed.");
        return NaN;
    }
    const double m1 = Elements::mass(settings->beamZ, settings->beamA);
    const double m2 = Elements::mass(cut.z, cut.a);
    if(m1 <= 0 || m2 <= 0) {
        errorMessage = QString("The mass of %1 is not known.")
                               .arg(m1 <= 0 ? Elements::label(settings->beamZ, settings->beamA) : Elements::label(cut.z, cut.a));
        return NaN;
    }
    const double angle = detector.scatteringAngle;
    if(cut.type == CutType::Recoil) {
        if(angle >= 90 * Units::Degree) {
            errorMessage = QStringLiteral("Recoils cannot reach a recoil angle of 90° or more.");
            return NaN;
        }
        const double cosine = std::cos(angle);
        return settings->beamEnergy * 4 * m1 * m2 * cosine * cosine / ((m1 + m2) * (m1 + m2));
    }
    const double sine = std::sin(angle);
    const double root = m2 * m2 - m1 * m1 * sine * sine;
    if(root < 0) {
        errorMessage = QString("%1 cannot scatter from %2 to an angle this large.")
                               .arg(Elements::label(settings->beamZ, settings->beamA), Elements::label(cut.z, cut.a));
        return NaN;
    }
    const double k = (std::sqrt(root) + m1 * std::cos(angle)) / (m1 + m2);
    return settings->beamEnergy * k * k;
}

ExpectedTof expectedSurfaceTof(const ExperimentalSettings *settings, const DetectorSettings &detector,
                               const PhysicsSettings &physics, const Cut &cut) {
    ExpectedTof expected;
    if(detector.tofLength <= 0) {
        expected.errorMessage = QStringLiteral("The ToF length of the detector is not set.");
        return expected;
    }
    expected.energy = surfaceEnergy(settings, detector, cut, expected.errorMessage);
    if(std::isnan(expected.energy)) {
        return expected;
    }
    if(expected.energy <= 0) {
        expected.errorMessage = QStringLiteral("The particles have no energy at this angle.");
        return expected;
    }
    const double mass = particleMass(cut.type, cut.z, cut.a, settings->beamZ, settings->beamA);
    const int particleZ = cut.type == CutType::Scattered ? settings->beamZ : cut.z;
    const FoilEnergyLoss foil(particleZ, mass, detector.foilMaterial, detector.foilThickness, physics);
    if(!foil.isValid()) {
        expected.errorMessage = foil.errorMessage();
        return expected;
    }
    const double energyAfterFoil = foil.energyAfter(expected.energy);
    if(energyAfterFoil <= 0) {
        expected.errorMessage = QStringLiteral("The particles stop in the timing detector foil.");
        return expected;
    }
    expected.foilLoss = expected.energy - energyAfterFoil;
    expected.tof = energyToTof(energyAfterFoil, mass, detector.tofLength);
    return expected;
}

double EdgeFit::value(double channel) const {
    return background + amplitude * gsl_cdf_ugaussian_P((channel - mu) / sigma);
}

EdgeFit findEdge(const std::vector<size_t> &counts) {
    EdgeFit edge;
    const std::vector<double> smooth = smoothed(counts, smoothingHalfWidth(counts));
    const double max = smooth.empty() ? 0 : *std::max_element(smooth.begin(), smooth.end());
    if(max <= 0) {
        edge.errorMessage = QStringLiteral("The selection has no events.");
        return edge;
    }
    // The level the counts rise to at the edge: the highest counts shortly after they first become significant. A
    // larger peak further on, e.g. from a buried layer, does not count.
    const int start = firstAtLeast(smooth, 0.2 * max);
    const int last = static_cast<int>(smooth.size()) - 1;
    const int span = std::max(MinWindowHalfWidth, (last - start) / 20);
    const double level = *std::max_element(smooth.begin() + start, smooth.begin() + std::min(last, start + span) + 1);
    // The rise around the middle of the edge. Background before the edge may reach 10 % here and there.
    const int middle = firstAtLeast(smooth, 0.5 * level);
    int low = middle;
    while(low > 0 && smooth[low - 1] >= 0.1 * level) {
        low--;
    }
    const int high = firstAtLeast(smooth, 0.9 * level, middle);
    const double riseWidth = std::max(1, high - low);

    const int halfWidth = std::max(MinWindowHalfWidth, static_cast<int>(std::ceil(WindowRiseWidths * riseWidth)));
    edge.windowMin = std::max(0, middle - halfWidth);
    edge.windowMax = std::min(last, middle + halfWidth);
    edge.mu = middle;
    edge.sigma = std::max(MinSigma + 0.2, riseWidth / RiseWidthsPerSigma);
    edge.background = smooth[edge.windowMin];
    edge.amplitude = std::max(1.0, level - edge.background);
    edge.ok = true;
    return edge;
}

EdgeFit fitEdge(const std::vector<size_t> &counts, const EdgeFit &initial) {
    EdgeFit result = initial;
    result.ok = false;
    result.muError = 0;
    result.windowMin = std::max(0, std::min(initial.windowMin, initial.windowMax));
    result.windowMax = std::min(static_cast<int>(counts.size()) - 1, std::max(initial.windowMin, initial.windowMax));
    const int n = result.windowMax - result.windowMin + 1;
    if(n < MinFitChannels) {
        result.errorMessage = QString("The fit window must have at least %1 channels with data.").arg(MinFitChannels);
        return result;
    }

    // Poisson distributed counts: the first pass weights them by the smoothed counts, the later ones by the model of
    // the previous pass. This converges to the maximum likelihood fit, weights from the counts themselves would pull
    // the fit down where there are few counts.
    FitData data{&counts, result.windowMin, std::vector<double>(static_cast<size_t>(n))};
    const std::vector<double> smooth = smoothed(counts, smoothingHalfWidth(counts));
    for(int i = 0; i < n; i++) {
        data.weights[i] = 1 / std::sqrt(std::max(MinWeightCounts, smooth[result.windowMin + i]));
    }
    gsl_multifit_nlinear_fdf fdf;
    fdf.f = residuals;
    fdf.df = jacobian;
    fdf.fvv = nullptr;
    fdf.n = static_cast<size_t>(n);
    fdf.p = ParameterCount;
    fdf.params = &data;

    double start[ParameterCount];
    start[Mu] = std::clamp(initial.mu, static_cast<double>(result.windowMin), static_cast<double>(result.windowMax));
    start[LogSigma] = std::log(std::max(0.1, initial.sigma - MinSigma));
    start[Amplitude] = initial.amplitude;
    start[Background] = initial.background;
    gsl_vector_view startView = gsl_vector_view_array(start, ParameterCount);

    const gsl_multifit_nlinear_parameters parameters = gsl_multifit_nlinear_default_parameters();
    gsl_multifit_nlinear_workspace *workspace = gsl_multifit_nlinear_alloc(gsl_multifit_nlinear_trust, &parameters,
                                                                           fdf.n, fdf.p);
    // GSL aborts on errors by default, the fit reports them instead
    gsl_error_handler_t *handler = gsl_set_error_handler_off();
    int status = GSL_SUCCESS;
    for(int pass = 0; pass < FitPasses && status == GSL_SUCCESS; pass++) {
        if(pass > 0) {
            const gsl_vector *p = gsl_multifit_nlinear_position(workspace);
            for(int i = 0; i < n; i++) {
                const double model = gsl_vector_get(p, Background) + gsl_vector_get(p, Amplitude)
                        * gsl_cdf_ugaussian_P((result.windowMin + i - gsl_vector_get(p, Mu)) / sigmaOf(p));
                data.weights[i] = 1 / std::sqrt(std::max(MinWeightCounts, model));
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
    gsl_matrix *covariance = gsl_matrix_alloc(ParameterCount, ParameterCount);
    gsl_matrix *j = gsl_multifit_nlinear_jac(workspace);
    const bool hasCovariance = status == GSL_SUCCESS && gsl_multifit_nlinear_covar(j, 0, covariance) == GSL_SUCCESS;
    result.mu = gsl_vector_get(p, Mu);
    result.sigma = sigmaOf(p);
    result.amplitude = gsl_vector_get(p, Amplitude);
    result.background = gsl_vector_get(p, Background);
    if(hasCovariance && n > ParameterCount) {
        // Scaled by the reduced chi squared when it is above one, the counts may not be quite Poisson distributed
        const double scale = std::max(1.0, chiSquared / (n - ParameterCount));
        result.muError = std::sqrt(gsl_matrix_get(covariance, Mu, Mu) * scale);
    }
    gsl_matrix_free(covariance);
    gsl_multifit_nlinear_free(workspace);
    gsl_set_error_handler(handler);

    if(status != GSL_SUCCESS) {
        result.errorMessage = QString("The fit failed: %1.").arg(gsl_strerror(status));
    } else if(!std::isfinite(result.mu) || !std::isfinite(result.sigma) || result.amplitude <= 0) {
        result.errorMessage = QStringLiteral("The fit did not find a rising edge.");
    } else if(result.mu < result.windowMin || result.mu > result.windowMax) {
        result.errorMessage = QStringLiteral("The fitted edge is outside the fit window.");
    } else {
        result.ok = true;
    }
    return result;
}

EdgeFit findAndFitEdge(const std::vector<size_t> &counts) {
    const EdgeFit initial = findEdge(counts);
    return initial.ok ? fitEdge(counts, initial) : initial;
}

LinearFit fitLine(const QList<QPointF> &points) {
    LinearFit fit;
    const auto n = static_cast<size_t>(points.size());
    std::vector<double> x(n), y(n);
    for(size_t i = 0; i < n; i++) {
        x[i] = points[static_cast<qsizetype>(i)].x();
        y[i] = points[static_cast<qsizetype>(i)].y();
    }
    if(n < 2 || std::all_of(x.begin(), x.end(), [&x](double value) { return value == x[0]; })) {
        fit.errorMessage = QStringLiteral("At least two points with different channels are needed.");
        return fit;
    }
    double cov00, cov01, cov11, sumSquares;
    gsl_fit_linear(x.data(), 1, y.data(), 1, n, &fit.offset, &fit.slope, &cov00, &cov01, &cov11, &sumSquares);
    if(!(fit.slope > 0)) {
        fit.errorMessage = QStringLiteral("The fitted slope is not positive: the time of flight should grow with the channel.");
        return fit;
    }
    fit.ok = true;
    return fit;
}
