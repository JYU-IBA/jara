/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#ifndef JARA_ANGLECALIBRATION_H
#define JARA_ANGLECALIBRATION_H

#include <array>
#include <limits>
#include <span>
#include <vector>

#include <QString>

#include "project.h"
#include "tofedata.h"

// Detection angle calibration from the timing coordinate of the events, the third column of event files: the time
// difference of the coincidence in ticks, which follows the position where the particle hits the detector. The
// detector window is a rectangle, so the timing distribution is a rectangle convolved with a Gaussian of the timing
// resolution. Its centre is the scattering/recoil angle of the detector and its edges the edges of the window, at
// ± atan((width / 2) / distance) from it.
//
// The timing also depends on the time of flight, slow particles shift the distribution. Events with a long ToF are left
// out of the fit, and the centre is a polynomial of order 0 to 3 in the ToF channel, fitted to the centres of slices
// of equal counts or of equal ToF widths. The width and the resolution are fitted to the distribution with this shift
// taken out.

// Counts of the timing values in bins. Timing values are integers, bin i covers the values from origin + i * binWidth
// to origin + (i + 1) * binWidth - 1, i.e. x from origin - 0.5 + i * binWidth on.
struct TimingHistogram {
    double origin = 0; // Ticks
    double binWidth = 1; // Ticks, integer valued
    std::vector<double> counts;
    double binStart(size_t bin) const { return origin - 0.5 + static_cast<double>(bin) * binWidth; }
};

// Fit of counts = background * binWidth + area * (rectangle of width 2 * halfWidth convolved with a Gaussian of
// sigma), integrated over each bin. Counts are weighted as Poisson distributed.
struct WindowFit {
    double centre = 0; // Ticks
    double centreError = 0; // Standard error of the centre
    double halfWidth = 0; // Ticks
    double halfWidthError = 0;
    double sigma = 0; // Ticks
    double area = 0; // Counts of the window
    double background = 0; // Counts per tick
    double fitMin = 0; // Ticks, the fit range
    double fitMax = 0;
    bool ok = false;
    QString errorMessage; // When not ok
    double value(double x, double binWidth) const; // The fitted counts of a bin of the width centred at x
};

// The centre of the window in a slice of ToF channels
struct SliceCentre {
    double tof = 0; // Channel, the average of the slice
    double centre = 0; // Ticks
    double centreError = 0;
    double residual = 0; // Ticks, the centre minus the fitted polynomial
    size_t events = 0;
    bool ok = false;
};

struct AngleFitOptions {
    double minTof = 0; // Channel, events with a shorter ToF are left out
    double maxTof = 0; // Channel, events with a longer ToF are left out
    double binWidth = 1; // Ticks, rounded to an integer of at least 1
    int order = 1; // Of the polynomial of the centre against ToF, 0–MaxCentreOrder
    int slices = 5; // Of ToF for the centre polynomial
    bool equalTofSlices = false; // Slices of equal ToF width instead of equal counts
    // Fit range in ticks of the distribution at the reference ToF, NaN for one found from the data
    double fitMin = std::numeric_limits<double>::quiet_NaN();
    double fitMax = std::numeric_limits<double>::quiet_NaN();
};

struct AngleFit {
    size_t events = 0; // With a timing value and ToF up to the maximum
    double referenceTof = 0; // Channel, the average of the events, the histogram is shifted to it
    double tofMin = 0; // Channel, the range of ToF in the fit, the minimum and maximum of the options
    double tofMax = 0;
    TimingHistogram histogram; // Timing - shift(ToF), with the shift zero at the reference ToF
    WindowFit window; // Fitted to the histogram
    std::vector<SliceCentre> slices;
    // Of the shift, coefficients[k] of (ToF - referenceTof)^k in ticks/ch^k. The constant, coefficients[0], is zero,
    // the window gives it. All zero with order 0 or when the polynomial could not be fitted.
    std::array<double, MaxCentreOrder + 1> coefficients{};
    std::array<double, MaxCentreOrder + 1> errors{};
    int order = 0; // Of the fitted polynomial
    double sliceChiSquared = 0; // Reduced, of the slice centres from the polynomial, NaN when there are no degrees of freedom
    QString centreMessage; // When the polynomial through the slices could not be fitted
    bool ok = false;
    QString errorMessage; // When not ok
    double shiftAt(double tof) const {
        const double d = tof - referenceTof;
        double sum = 0;
        for(size_t k = coefficients.size() - 1; k > 0; k--) {
            sum = (sum + coefficients[k]) * d;
        }
        return sum;
    }
    double centreAt(double tof) const { return window.centre + shiftAt(tof); }
    AngleCalibration calibration(bool reversed) const;
};

AngleFit fitAngleCalibration(std::span<const ToFEEvent> events, const AngleFitOptions &options);

// Copies of the events with each timing value spread uniformly over ± smoothing ticks, which evens out structure of
// the ticks, e.g. ticks of unequal widths. The spread uses a fixed seed, so the same events always give the same
// result. Copies as they are without smoothing.
std::vector<ToFEEvent> smoothTiming(std::span<const ToFEEvent> events, double smoothing);

// The centre of the window against ToF as text, e.g. "1.11 − 1.99e-3 × (ToF − 2000) ticks", with the given numbers of
// significant digits
QString centreText(const AngleCalibration &calibration, int centreDigits = 6, int coefficientDigits = 3);

// Half of the angle the detector window covers seen from the sample, atan((width / 2) / distance), rad. Zero when the
// distance or width is not set.
double windowHalfAngle(const DetectorSettings &detector);

// Angle of the event's detection, rad, relative to the scattering/recoil angle of the detector. Within the window:
// clamped to its half angle, as events beyond the edges are there only because of the timing resolution. Zero when the
// event has no timing, its ToF is outside the range of the calibration or the detector has no angle calibration.
double angleOffset(const AngleCalibration &calibration, double distance, double windowWidth, const ToFEEvent &event);

#endif // JARA_ANGLECALIBRATION_H
