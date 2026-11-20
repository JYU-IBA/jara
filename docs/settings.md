# Settings

Settings are shared: several measurements can be assigned the same experimental, detector and physics settings, and
changing them affects all of those measurements. Add them from the **Experiment** menu or by right-clicking the tree,
and click them to edit them in a tab. The tab also lists the measurements they are assigned to.

Settings can be duplicated (context menu), copied and pasted between projects, exported as JSON files and imported
from JSON files or from other projects (Experiment → Import...).

Angles are measured from the sample surface. The geometry is IBM: the exit angle is the scattering angle minus the
incident angle.

## Experimental settings

- **Beam energy** and **incident ion** (a specific isotope)
- **Incident angle** from the sample surface

## Detector settings

- **Scattering/recoil angle**
- **ToF length**: the flight path between the timing detectors
- **Timing detector foil**: material and thickness (as mass or atoms per area) of the foil of the first timing
  detector. Particles lose energy in it before their time of flight is measured; spectra and profiles add this loss
  back. The default is a 3 µg/cm² carbon foil.
- **Efficiencies**: detection efficiency curves of isotopes, imported from text files with the energy (MeV) and the
  efficiency in two columns. The isotope is taken from the file name (e.g. `1H.eff` or `H-1.eff`) or asked for. The
  efficiency is that of the particle's energy before the foil, interpolated linearly and zero outside the curve.
  Isotopes without a curve are detected fully.
- **ToF calibration**: t = slope × channel + offset, set by hand or with the
  [ToF calibration tool](tof-calibration.md) (**ToF Calibration...**).
- **Detector distance** and **Window width**: from the sample to the window of the energy detector, and the width of
  the window in the scattering plane. With the angle calibration they give each event its angle.
- **Angle calibration**: the timing coordinate of the events against the detection angle, made with the
  [angle calibration tool](angle-calibration.md) (**Angle Calibration...**). With it, the distance and the width,
  depth profiles can correct each event kinematically, see [Depth profiles](depth-profiles.md#calculation).
- **Coincidence parameters (list-mode files)**: the ToF and E ADCs, the trigger, the timing window, the reorder span
  and the pairing used when [importing event data](coincidences.md#coincidence-parameters). The defaults are on the
  Coincidences page of the preferences.

## Physics settings

Used by depth profiles and the foil correction. Measurements without physics settings use the defaults.

- **Screening** of the Rutherford cross sections: none or Andersen
- **Electronic and nuclear stopping factors** that multiply the stopping of all elements
- **Stopping file**: the GSTO file of electronic stopping, or JIBAL's choice
- **Element pairs**: corrections for an incident element in a target element, or in any target, with their own
  factors and stopping file

## Preferences

File → Preferences holds the program-wide defaults:

- Default experimental and detector settings of new projects and new settings
- ToF-E histogram binning and colour scheme
- Depth profile iterations and convergence tolerance, the parameters of new depth profiles, the default legend
  corner, and whether new depth profiles show concentrations in the legend and round them in the legend and in the
  table of averages
- The default coincidence parameters, the plot margin, the size of the preview and the scale of the distribution of
  time differences when [importing event data](coincidences.md)
- Colours of the elements, used for new selections
