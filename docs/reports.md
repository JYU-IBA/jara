# Reports

A report collects depth profiles of the project into a PDF: a table of their average concentrations, the settings of
their measurements, their plots and optionally split analyses in an appendix. Add one with Sample → Add Report or from
the context menu of the project tree. Reports are listed after the samples.

## Contents

The list on the left shows every depth profile and split analysis of the project, grouped by sample and measurement.
Checked depth profiles are in the table and the plots; checked split analyses are in the appendix. A new report
includes all depth profiles and no split analyses.

## Options

- **Title** of the PDF.
- **Measurement names** in the rows: **Automatic** shows them when a sample has several rows, **Always** or **Never**.
  When a measurement has several included depth profiles, the profile name is added as well.
- **Settings**: the items shown. Incident ion, beam energy, incident angle and scattering angle are on by default;
  ToF length, timing foil, ToF calibration, efficiencies, physics settings and depth profile parameters can be added.
  Values shared by all rows are listed once, the others are given in a table by row.
- **Plot ranges**:
  - **As in the depth profile tabs**: the zoom of each depth profile, which the tabs remember in the project, or
    everything when it has not been zoomed.
  - **The same for all plots**: one depth and concentration range, optionally on a log scale.
  - **Each its own**: a range for each included profile, starting from the zoom of its tab.

## The PDF

**Generate PDF...** calculates every included depth profile and split analysis from its data file, as their tabs do,
and writes the PDF:

1. The title, the project's name and file, and when the report was generated.
2. The settings.
3. The average concentrations in at.%, one row per depth profile and one column per element in order of atomic number.
   Recoils, scattered ions and isotopes of an element are summed, and their uncertainties added in quadrature. The
   averages are those of each profile: over the normalization interval when its averaging follows it, otherwise
   renormalized among its included selections (marked with *). Uncertainties are statistical. Wide tables are split
   into blocks of six elements.
4. The depth profile plots, two per page, in 10¹⁵ at/cm².
5. The appendix: the split analysis plots.

## Out of date

The report remembers what it showed when its PDF was generated. When an included depth profile, its measurement's
selections, settings or data file, the report's options or the project's name change, the report is marked out of date (• in its tab) until
the PDF is generated again. Zooming a depth profile only counts when the plot ranges are taken from the tabs.
