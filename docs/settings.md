# Settings

Settings are shared: several measurements can be assigned the same experimental, detector and physics settings, and
changing them affects all of those measurements. Add them from the **Experiment** menu or by right-clicking the tree,
and click them to edit them in a tab. The tab also lists the measurements they are assigned to.

Settings can be duplicated (context menu), copied and pasted between projects, exported as JSON files and imported
from JSON files or from other projects (Experiment → Import...).

Angles are measured from the sample surface. The geometry is IBM: the exit angle is the scattering angle minus the
incident angle.

## Experimental settings

**Beam**

- **Incident ion** (a specific isotope) and **beam energy**
- **Incident angle** from the sample surface

## Detector settings

- **Scattering/recoil angle**
- **Efficiencies**: detection efficiency curves of isotopes, imported from text files with the energy (MeV) and the
  efficiency in two columns. The isotope is taken from the file name (e.g. `1H.eff` or `H-1.eff`) or asked for. The
  efficiency is that of the particle's energy before the foil, interpolated linearly and zero outside the curve.
  Isotopes without a curve are detected fully.

**Time of flight**

- **Length**: the flight path between the timing detectors
- **Timing detector foil**: material and thickness (as mass or atoms per area) of the foil of the first timing
  detector. Particles lose energy in it before their time of flight is measured; spectra and profiles add this loss
  back. The default is a 3 µg/cm² carbon foil.
- **Calibration slope** and **offset**: t = slope × channel + offset, set by hand or with the
  [ToF calibration tool](tof-calibration.md) (**ToF Calibration...**).

**Detection angle**

- **Detector distance** and **Window width**: from the sample to the window of the energy detector, and the width of
  the window in the scattering plane. With the angle calibration they give each event its angle.
- **Calibration**: the timing coordinate of the events against the detection angle, made with the
  [angle calibration tool](angle-calibration.md) (**Angle Calibration...**). With it, the distance and the width,
  depth profiles can correct each event kinematically, see [Depth profiles](depth-profiles.md#calculation).

**List-mode files**

- **Coincidences**: a summary of the coincidence parameters used when
  [importing event data](coincidences.md#coincidence-parameters); **Edit...** opens all of them: the ToF and E ADCs,
  the trigger, the timing window, the reorder span and the pairing. The defaults are on the Coincidences page of the
  preferences.

## Physics settings

Used by depth profiles and the foil correction. Measurements without physics settings use the defaults.

- **Cross sections**: the **screening** of the Rutherford cross sections, none or Andersen
- **Stopping of all elements**: **electronic and nuclear factors** that multiply the stopping, the **nuclear stopping
  mass**, the **interpolation** of stopping, the **slowing down** method and the **stopping file**, the GSTO file of
  electronic stopping or JIBAL's choice
- **Stopping of element pairs**: corrections for an incident element in a target element, or in any target, with their
  own factors and stopping file

### Stopping

The stopping of an ion in an element is the sum of electronic and nuclear stopping:

- **Electronic stopping** comes from a GSTO file of JIBAL. The **Stopping file** of all elements is either one of the
  electronic stopping files JIBAL has, or **Automatic (JIBAL)**. Automatic means the file assigned to the pair in
  JIBAL's `assignments.txt` if there is one, otherwise the first file in JIBAL's `files.txt` that has the pair.
- **Nuclear stopping** is the universal (ZBL) nuclear stopping, calculated from the atomic numbers and masses with the
  same formula as JIBAL's. The **Nuclear stopping mass** is the mass of the target atoms: **Average mass** of the
  element (the default), or **Isotopes**, where each natural isotope of the element has its own mass and is weighted
  by its abundance. The mass of the incident ion is always its own.

A stopping file samples the stopping at a set of energies (its points, e.g. 201 points from 1 keV/u to 10 MeV/u
evenly on a logarithmic scale in SRIM-2013). JaRA calculates the stopping at the points of the file, the electronic
stopping as it is in the file and the nuclear stopping with the formula, and interpolates their sum between the points
as the **Interpolation** says:

- **Linear** (the default): linearly in energy, as JIBAL does. The electronic stopping is then exactly JIBAL's.
- **Cubic**: a cubic spline through the points, with the stopping as a function of the logarithm of the energy. The
  spline is one cubic polynomial between each two points, passes through the points, and has continuous first and
  second derivatives. Its ends are "not-a-knot": the first two intervals are one cubic, and so are the last two. A file
  with three points gives the parabola through them and one with two points a straight line, both in the logarithm of
  the energy. Near the stopping maximum the spline can rise above the highest point, where the true maximum falls
  between the points, which linear interpolation cuts off. Between the points it differs from the linear one by a
  fraction of a per cent.

Each element has its own interpolated curve, and the stopping of a mixture is their sum weighted by the atomic
fractions (Bragg's rule). Outside the energy range of the file nothing is interpolated: see the energy range below.

The nuclear stopping is interpolated in the same way. Its formula switches from one expression to another at a reduced
energy of 30, with a jump of about 1 %, which the interpolation smooths over between the points around it; the cubic
spline also oscillates slightly for a few points on each side. Elsewhere the interpolated nuclear stopping is within
0.05 % of the formula.

Each is multiplied by its factor: **Electronic factor** and **Nuclear factor**. A factor of 1.05 makes the stopping
5 % larger.

**Slowing down** sets how the energy of a particle is followed along a path, e.g. through a stopping bin of a depth
profile or the timing detector foil. The path is divided into steps where the particle loses at most 2 % of its
energy. In each step:

- **Runge-Kutta (fourth order)**: the stopping at four energies within the step.
- **Midpoint (second order)** (the default): the stopping at the energy halfway through the step, estimated from the
  stopping at its start. Depth profiles are about twice as fast as with Runge-Kutta, and with the default stopping bins within 0.001 at.% of
  Runge-Kutta.
- **Euler (first order)**: the stopping at the energy at the start of the step, about three times faster than
  Runge-Kutta. The error of a step grows with the square of its energy loss; with the default stopping bins
  concentrations differ from Runge-Kutta by up to a few tenths of an at.%.

**Stopping of element pairs** changes the stopping of single combinations. Pairs are added and removed from the context
menu of the table (**Add Pair**, **Remove**, **Remove All**). Each row has:

- **Incident**: the element of the ion, e.g. the recoil or the beam
- **Target**: an element of the sample or the foil, or **Any** for all target elements that have no row of their own
  with this incident element
- **Electronic** and **Nuclear**: factors, multiplied by the factors of all elements. With 1.1 for all elements and
  0.9 for the pair, the stopping of the pair is multiplied by 0.99.
- **Stopping file**: the electronic stopping file of the pair, or **Same as all** for the file chosen for all elements.
  Only files that have the pair are listed.

A pair of its own (e.g. H in Si) is used before a row with **Any** target (H in any element), which is used before
the settings of all elements. If the chosen file has no stopping for a combination that a depth profile or foil
needs, the calculation stops with an error naming the file and the pair.

Stopping is only known within the energy range of the file, e.g. 1 keV/u–10 MeV/u for SRIM-2013. Depth profiles and
energy spectra do not use stopping outside it:

- A depth profile is not calculated when the beam energy, or the energy a detected particle gets from the beam (the
  kinematic factor × the beam energy, at the angle of any of its events), is above the range. The error names the particle, the element and the file.
- Events whose energy is outside the range in the sample or in the timing detector foil are left out, and events from
  depths where the beam has slowed down below the range get a weight of zero. See
  [Depth profiles](depth-profiles.md#calculation) and [Energy spectra](energy-spectra.md).
- The ToF calibration tool shows an error when the expected energy of a selection is outside the range of the foil's
  stopping data.

### Stopping data in the packages

The packages of a release (the AppImage, the Windows zip and MSI and the macOS disk image) have their own JIBAL data
and do not use a JIBAL installation or configuration on the computer. The data is JIBAL's data archive
([data.tar.gz](https://users.jyu.fi/~jaakjuli/jibal/data/data.tar.gz)) with the masses and abundances of JIBAL. The
electronic stopping files in it are:

| File       | Source    | Incident Z | Target Z | Energy           |
|------------|-----------|------------|----------|------------------|
| `srim2013` | SRIM-2013 | 1–92       | 1–92     | 1 keV/u–10 MeV/u |
| `dpass`    | DPASS     | 1–92       | 1–92     | 1 keV/u–1 GeV/u  |

`srim2013` is first in `files.txt` and `assignments.txt` assigns nothing, so **Automatic (JIBAL)** means SRIM-2013 for
every pair. The archive also has straggling files (Yang, Chu, Bohr), which JaRA does not use.

A program built from the sources uses the JIBAL configuration of the computer, and then the stopping files that are
listed there, unless it is installed with `JARA_BUNDLE_JIBAL_DATA` (see [Building](building.md)).
`jara --check-installation` prints the configuration file and data directory that are used and the number of GSTO
files (stopping and straggling).

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
- The JIBAL configuration file, see below

### JIBAL configuration

JIBAL gives JaRA the masses and abundances of the isotopes and the stopping of the elements. Its configuration file
says where these data files are. The **JIBAL** tab of the preferences chooses the file:

- **Distribution** (the default): the `jibal.conf` installed with JaRA, with the data files of the packages, see
  [Building](building.md#installing-and-the-jibal-data). A build without it uses the configuration JIBAL finds
  itself, e.g. that of the user.
- **JIBAL user configuration**: JIBAL's own configuration of the user, `~/.jibal/jibal.conf`, or
  `%AppData%\jibal\jibal.conf` on Windows, e.g. with other stopping files.
- **File**: any JIBAL configuration file. Relative paths in it are relative to the file.

The tab shows the file in use and where it comes from, which can differ from the choice: with **Distribution** and
no file installed with JaRA, JIBAL finds one itself, e.g. the user configuration. JIBAL is read once when JaRA starts, so a change takes effect when JaRA is restarted.
If JIBAL cannot be initialized with the user configuration or the chosen file, JaRA says so at startup and uses the
distribution configuration, so that the preference can be changed. The About dialog shows the configuration file in
use, and `jara --check-installation` checks the one of the preferences.
