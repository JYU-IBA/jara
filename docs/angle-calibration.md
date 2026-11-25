# Angle calibration

The angle calibration converts the timing coordinate of the events into their detection angles, for a kinematic
correction of each event in [depth profiles](depth-profiles.md#calculation). It belongs to the detector settings. Open
the tool with **Angle Calibration...** in the detector settings.

## How it works

The third column of the event files, the time difference of the coincidence in ticks (see
[Coincidence files](coincidences.md)), follows the position where the particle hits the detector. The detector window
is a rectangle, so the timing distribution is a rectangle convolved with a Gaussian of the timing resolution. Its
centre is the scattering/recoil angle of the detector, its edges are the edges of the window, at
± atan((width / 2) / distance) from it. In between, the angle follows the position on the window.

The distribution is fitted with background + area × (rectangle ⊗ Gaussian), integrated over each bin and with the
counts weighted as Poisson distributed. The fit gives the centre, the half width and the resolution σ.

The timing also depends on the time of flight: the distribution moves with ToF, and slow particles skew it. Events
with a ToF outside a minimum and a maximum are left out. The centre is fitted in slices of ToF, with the width and σ of all
events, and a weighted polynomial through the slice centres, constant, linear, quadratic or cubic in ToF, gives its shift
with ToF. The width and σ are then fitted again to the distribution with this shift taken out. In a second pass, the
slices are fitted again with this width and σ, which the shift no longer widens, starting from the centres of the
first polynomial, and the polynomial and the width and σ are fitted again from them.

Each event gets the angle offset atan(u × (width / 2) / distance), where u = (timing − centre(ToF)) / half width and
centre(ToF) = c₀ + c₁ (ToF − ToF₀) + c₂ (ToF − ToF₀)² + c₃ (ToF − ToF₀)³, with ToF₀ the average ToF of the events of the fit.
Events beyond the edges are there only because of the timing resolution, so u is clamped to ±1, and the correction to
the half angle of the window. The centre is known only between the minimum and maximum ToF of the fit, so events with
a ToF outside that range get no correction: they keep the scattering/recoil angle of the detector.

The fit cannot tell which end of the window is the larger angle. **Larger timing is a smaller angle** sets the
direction. Changing the coincidence trigger between E and ToF negates the timing, so the calibration must be done
again.

## Using the tool

1. Check the measurements whose events are used. They are pooled. Measurements without a timing column are noted.
2. The upper plot shows the timing against ToF. Drag the dashed lines, or set **Minimum ToF** and **Maximum ToF**, to
   leave out events with a shorter or longer ToF. By default the range is from zero to the longest ToF of the events.
   Events outside the range also get no kinematic correction. The fitted window, centre and edges, is drawn over the
   range.
3. The lower plot shows the timing distribution between the minimum and maximum ToF with the fit. With several ToF slices, it is
   the distribution at the average ToF of the events, with the shift of the centre taken out. Drag the dashed lines
   to change the fit range, **Automatic Range** finds it again. **Bin width** sets the bins of the distribution and
   the fit. With single ticks of unequal widths, wider bins even them out, but the window needs several bins.
   **Timing smoothing** spreads each timing value uniformly over ± this many ticks, in both plots and in the fit,
   which evens out structure of the ticks. It widens the fitted σ (a spread of ± s adds s / √3 in quadrature), not
   the centre or the width of the window. It is stored with the calibration, and the kinematic correction spreads
   the timing of each event in the same way, so that the angles of the events do not come in steps of the ticks.
   **Centre vs ToF** sets the order of the polynomial of the centre: constant, linear, quadratic or cubic. It needs at least
   one more slice than the order. **ToF slices** sets the number of slices, and **Slicing** sets whether they have
   equal counts or equal widths in ToF up to the maximum. Equal widths reach further into the slow tail, where the
   curvature shows. Slices with fewer than 100 events are left out. The table lists the centre of each slice and its
   residual from the fitted centre. The residuals should scatter around zero without a trend. The results give
   χ²/ndf of the slice centres when there are more slices than parameters. With a constant centre the slices are still
   fitted, as a check of whether the centre moves with ToF.
4. To find the direction, choose a selection in **Expected edge of**. Its expected leading edge over the window, from
   the kinematics at each angle, is drawn in the upper plot as a dark red dashed line for the chosen direction. The
   leading edge of the data should follow it; if it slopes the other way, change the direction. This needs the
   experimental settings of the measurement, the ToF calibration and the detector distance and window width.
5. **Apply to Detector** stores the calibration, with the range of ToF, the direction, the order and the slicing, in
   the detector settings.

Below the plots, the angle at the cursor is shown with its offset from the scattering/recoil angle, from the fit and
the chosen direction, clamped to the window as in the correction. In the distribution it is at the average ToF.
Needs the detector distance and window width.

Depth profiles of the detector then use the kinematic correction, which each profile can turn off with
**Kinematic correction**. They are marked out of date and recalculated. As a check, the surface edge of a profile
should be sharper with the correction than without it.
