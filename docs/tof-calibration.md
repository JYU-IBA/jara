# ToF calibration

The ToF calibration converts channels to times of flight, t = slope × channel + offset. It belongs to the detector
settings. Open the tool with **ToF Calibration...** in the detector settings.

## How it works

The short-ToF (high energy) edge of a selection's ToF spectrum is made by particles from the sample surface. Their
energy follows from the kinematics of the incident ion and the target atom: the recoil energy for recoils, the energy
of the scattered ion for scattered ions. They lose energy in the timing detector foil, and their time of flight follows
from the energy after the foil and the ToF length. A straight line through the edge channels and the expected times of
flight of several selections gives the calibration.

The measurements need experimental settings (incident ion, energy) and the detector's angle, ToF length and foil.

## Using the tool

1. The list shows the selections of the measurements assigned to the detector, with their expected time of flight.
   Reference selections are not listed.
2. Click a selection to see its ToF spectrum. The edge is fitted automatically with
   background + amplitude × Φ((channel − μ) / σ), Φ being the cumulative normal distribution, in a window around a
   first guess. Drag the dashed lines to fit again in another window.
3. If the fit is not good, **Set Edge** lets you click the edge by hand.
4. **Accept Edge** stores the edge channel in the selection as a calibration point.
5. With at least two points at different channels, the line is fitted and shown with its points.
   **Apply to Detector** sets the slope and offset of the detector settings.

Energy spectra and depth profiles that used the old calibration are marked out of date and recalculated.
