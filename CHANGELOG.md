# Changelog

All notable changes to JaRA are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and JaRA uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). Add changes under [Unreleased] as they are made;
`release_scripts/bump_version.sh` moves them under the new version.

## [Unreleased]

### Added

- Project and settings files record the version of JaRA that saved them, in `savedWithVersion`.
- Projects count their saves, in `saveCount`, shown in the project information.
- Projects record the user name of who saved them, from the environment (`USER`, or `USERNAME` on Windows), in
  `savedBy`, shown in the project information.
- A preference for the JIBAL configuration file: the one installed with JaRA (the default), JIBAL's user
  configuration or a file chosen by the user. It takes effect when JaRA is restarted. The About dialog and
  `--check-installation` show the configuration file in use. Projects record the JIBAL version and the configuration
  file they were saved with and where it comes from, in `savedWithJibal`.

### Changed

- The version of JaRA is a semantic version, with the commits since the release and uncommitted changes as build
  metadata, e.g. `0.2.1+3.gabcdef0.dirty` instead of `v0.2.1-3-gabcdef0-dirty`.

## [0.2.1] - 2026-11-25

### Added

- Angle calibration: the centre of the window against ToF can be constant, linear, quadratic or cubic, fitted to the
  centres of the ToF slices. The slices can have equal counts or equal widths in ToF.
- Angle calibration: a minimum ToF besides the maximum, both set with draggable lines that do not cross. By default
  the range is from zero to the longest ToF of the data.
- Angle calibration: the residuals of the slice centres from the fitted centre, and χ²/ndf of the slices.

### Changed

- Angle calibration: the slices are fitted in two passes. The second uses the width and resolution of the window with
  the shift of the centre taken out, so that the shift does not widen them.
- Angle calibration: the window is drawn only over the ToF range of the fit.
- Events with a ToF outside the range of the angle calibration get no kinematic correction.
- The depth of each event is calculated on its own, in parallel, from its detected energy back through the stopping
  bins. With the kinematic correction, each event has its own kinematic factor, exit angle and cross section.
- Rutherford cross sections with the Andersen screening correction are calculated in JaRA, as JIBAL calculates them.
- Depth profiles that are out of date after a settings edit are recalculated when their tab is shown, not at once.
- The angle calibration in project files: the centre is a list of polynomial coefficients about a reference ToF, with
  the ToF range of the fit. Calibrations made with earlier versions must be made again.

## [0.2.0] - 2026-11-25

### Added

- Packages on the GitHub releases page: a Linux AppImage, a Windows zip and MSI installer and a macOS disk image.
- Build instructions for Ubuntu with its packaged libraries.

### Changed

- JaRA is licensed under the GPL v2 or later.
- Stopping is taken from the points of the stopping files without re-tabulation, interpolated linearly in energy as
  JIBAL does.
- Depths in the depth profile log are written as e15 at./cm².

## [0.1.4] - 2026-11-23

### Added

- Angle calibration tool, which fits the timing coordinate of the events, with timing smoothing, and a kinematic
  correction of each event in depth profiles, an option of each profile.
- Conversion of list-mode coincidence files (`*.evnt`) into ToF-E event files, with a plot of the time differences for
  choosing the timing window.
- Files attached to the project as a whole, and images attached to samples shown in a tab of their own.
- Areal densities of renormalized averages, also in exports.
- The application icon.
- A macOS disk image made with `release_scripts/deploy_mac.sh`.

### Changed

- Settings and preferences are grouped in group boxes, and form layouts look the same on all platforms.
- Elements and isotopes are kept in order of Z, then mass number.
- Bins and averages without events are shown as an exact 0 at.%; averages not clearly above zero as upper limits.
- New projects are saved right away, and the project directory is the default location of file dialogs.

### Removed

- The automatic mass selection, which did not work well enough.

## [0.1.3] - 2026-11-19

### Added

- Reports: a PDF of depth profiles with their average concentrations by element, the settings, the plots and split
  analyses in an appendix.
- Project information, with the project's name, notes and creation and save times.
- Duplicating settings, energy spectra, depth profiles and split analyses from their context menus.
- Selections of a depth profile can use a narrower part of the measurement.

### Changed

- Project tree items open with a click in the current tab, or in a new tab with Ctrl/Cmd-click or a middle click.
- Samples, measurements and the project information are edited in tabs of their own, and items are renamed in place.
- The zoom of depth profile plots is remembered in the project.

## [0.1.2] - 2026-11-19

### Added

- Reference selections, split analyses, and ranges of the measurement for energy spectra and depth profiles.
- A dialog for all properties of a selection, with an additional weight and natural abundances for isotopes.
- Checksums of the referenced files in projects, checked on opening, and exporting a project as an archive.
- Selection labels in depth profile plots, and legends that can be dragged to any corner.
- CSV export.
- A README and documentation.

### Changed

- Isotopes of an element are summed in depth profiles unless shown separately.
- Depth profiles are averaged over the normalization interval by default.

### Fixed

- A crash when a check box in a list or table was changed.

## [0.1.1] - 2026-11-19

First tagged version.

[Unreleased]: https://github.com/JYU-IBA/jara/compare/v0.2.1...HEAD
[0.2.1]: https://github.com/JYU-IBA/jara/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/JYU-IBA/jara/compare/v0.1.4...v0.2.0
[0.1.4]: https://github.com/JYU-IBA/jara/compare/v0.1.3...v0.1.4
[0.1.3]: https://github.com/JYU-IBA/jara/compare/v0.1.2...v0.1.3
[0.1.2]: https://github.com/JYU-IBA/jara/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/JYU-IBA/jara/releases/tag/v0.1.1
