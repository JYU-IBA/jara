# JaRA

JaRA is a desktop program for analysing time-of-flight elastic recoil detection analysis (ToF-ERDA) data. It reads
list-mode ToF-E event files, lets you select the events of each element by drawing polygons on the ToF-E histogram,
and turns the selections into energy spectra and depth profiles. Stopping, masses and isotopes come from
[JIBAL](https://github.com/JYU-IBA/jibal).

## Features

- **Projects** of samples and measurements, with the experimental, detector and physics settings shared between
  measurements. Referenced files are checked for changes when a project is opened, and a project can be exported as an
  archive together with its data files. See [Getting started](docs/getting-started.md) and [Settings](docs/settings.md).
- **Coincidence files**: list-mode files (`*.evnt`) converted to ToF-E events, with the timing window chosen from a
  plot of the time differences. See [Coincidence files](docs/coincidences.md).
- **ToF-E histograms with polygon selections** of recoils (ERD), scattered ions (RBS) and reference selections. An
  isotope can stand for its natural element, and the events of a selection can be given an extra weight. See
  [Selections](docs/selections.md).
- **ToF calibration** from the leading edges of the selections' ToF spectra. See [ToF calibration](docs/tof-calibration.md).
- **Angle calibration** of the timing coordinate of the events, for a kinematic correction of each event in depth
  profiles. See [Angle calibration](docs/angle-calibration.md).
- **Energy spectra** of selections, with the energy lost in the timing detector foil added back. See
  [Energy spectra](docs/energy-spectra.md).
- **Depth profiles** from single scattering (slab analysis), iterated until the composition converges, with
  normalization, averages, uncertainties and labels in the plot. See [Depth profiles](docs/depth-profiles.md).
- **Split analysis**: the measurement divided into parts with equal counts of a reference selection, to see whether
  counts are lost during the measurement, e.g. hydrogen released by the beam. Energy spectra and depth profiles can
  use only a part of the measurement. See [Split analysis](docs/split-analysis.md).
- **Reports**: a PDF of depth profiles with a table of average concentrations by element, the settings, the plots and
  split analyses in an appendix, marked out of date when the profiles change. See [Reports](docs/reports.md).
- **Export** of spectra, profiles and split analyses as text files with a short header or as CSV.

## Download

Packages for macOS (disk image), Windows (MSI installer and zip) and Linux (AppImage) are on the
[releases page](https://github.com/JYU-IBA/jara/releases). They contain everything JaRA needs. What changed in each
version is in the [changelog](CHANGELOG.md).

**macOS:** the app is signed ad hoc, not notarized by Apple, so macOS blocks it the first time it is opened. Open it
once, then click **Open Anyway** in System Settings → Privacy & Security, or run
`xattr -dr com.apple.quarantine /Applications/JaRA.app`. See [Installing](docs/installing.md) for the details and for
Windows and Linux.

## Quick start

1. Download JaRA, see [Installing](docs/installing.md), or build it, see [Building](docs/building.md).
2. Create a project (File → New), add a sample and its measurement files (`*.asc`).
3. Add experimental and detector settings and assign them to the measurements.
4. Open the ToF-E histogram of a measurement and draw a selection around the events of each element.
5. Calibrate the ToF with a few selections, then add energy spectra or depth profiles to the measurement.

[Getting started](docs/getting-started.md) goes through these steps in more detail.

## Documentation

- [Installing](docs/installing.md): the packages of the releases, and opening JaRA on macOS
- [Building](docs/building.md): requirements and compiling
- [Getting started](docs/getting-started.md): projects, samples, measurements, files and exporting
- [Settings](docs/settings.md): experimental, detector and physics settings, efficiencies, the timing detector foil,
  preferences
- [Coincidence files](docs/coincidences.md): converting list-mode files to measurements
- [Selections](docs/selections.md): the ToF-E histogram and selections
- [ToF calibration](docs/tof-calibration.md)
- [Angle calibration](docs/angle-calibration.md)
- [Energy spectra](docs/energy-spectra.md)
- [Depth profiles](docs/depth-profiles.md)
- [Split analysis](docs/split-analysis.md)
- [Reports](docs/reports.md)

## License

Copyright (C) 2026 Jaakko Julin

JaRA is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version. See
[LICENSE.txt](LICENSE.txt). JaRA uses JIBAL, which is distributed under the same license.

## How JaRA is made

The code of JaRA has been written primarily, but not entirely, by Claude, an AI model by Anthropic, using Claude Opus
5.5 in Claude Code, under the direction of the author, who designs the program, decides what is done and how, and
reviews and tests the changes.

## Versions

Releases are tagged `vX.Y.Z` and the same number is kept in `version.txt` for builds without git.
`release_scripts/bump_version.sh` changes the version, moves the [Unreleased] changes of the [changelog](CHANGELOG.md)
under it, commits and tags the commit. Packages for Linux (AppImage), Windows (zip, MSI) and macOS (disk image) are on
the [releases page](https://github.com/JYU-IBA/jara/releases), with the changes of the version from the changelog as
the release notes. See [Packages and releases](docs/building.md#packages-and-releases-on-github).
