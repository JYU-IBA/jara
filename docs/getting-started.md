# Getting started

## Projects

A project (`*.json`) holds samples, their measurements and the settings of the experiment. It only refers to the data
files, which are never copied or moved. Paths inside the project's folder are stored relative to the project file,
so the folder can be moved as a whole.

The project panel on the left shows the project information first, then the settings, the samples and the reports.
Selecting an item shows its properties below the tree; double-clicking opens it in a tab. Tabs can be dragged out into
windows of their own.

Double-click the project information to open it in a tab: the project's **name**, shown in the tree and in reports
(the file name when empty), its file, when it was created and last saved, and **notes**.

- **File → New / Open / Save**. Recent projects are under File → Open Recent.
- **File → Export Archive** writes the project and copies of its data files into a `.tar.gz` or `.zip`, so that it
  can be opened elsewhere.
- When a project is opened, the files it refers to are checked against the SHA-256 checksums stored when they were
  added. Missing and changed files are reported.

## Samples and measurements

- **Sample → Add Sample**, then **Add Measurements...** to add ToF-E event files (`*.asc`) to it. Files can also be
  dropped on a sample in the tree.
- **Add Files...** attaches any other files to a sample, e.g. notes or images. Samples also have a description and
  metadata entries.
- A measurement file has one event per line, ToF and energy in channels in the first two columns; further columns
  are ignored. The order of the lines is the order the events were measured in, which the
  [split analysis](split-analysis.md) relies on.
- Measurements can be dragged to another sample.

Each measurement is assigned experimental, detector and physics settings in its properties below the tree, see
[Settings](settings.md). The properties also show the beam, the angles and the exit angle that follow from them.

## Working with a measurement

Double-click a measurement to open its ToF-E histogram and draw [selections](selections.md). Right-click a measurement
in the tree to add analyses to it:

- **Add Energy Spectrum**, see [Energy spectra](energy-spectra.md)
- **Add Depth Profile**, see [Depth profiles](depth-profiles.md)
- **Add Split Analysis**, see [Split analysis](split-analysis.md)

They appear under the measurement and are saved in the project. Depth profiles can be dragged to another measurement,
which copies them. [Reports](reports.md) collect depth profiles of several measurements into a PDF.

## Plots

All plots work the same way:

- **Zoom Box**: drag to zoom, double-click to show everything. **Move**: drag to move the view.
- The mouse wheel or a pinch zooms around the cursor, two-finger scrolling moves the view.
- The legend can be dragged to any corner.
- Right-click for the linear/logarithmic scale and the options of the plot.

## Exporting

Energy spectra, depth profiles and split analyses have an **Export...** button. The file type is chosen in the save
dialog:

- **Text (`.txt`)**: a few comment lines starting with `#` describe the data, then a `#` line with the column names and
  tab-separated values.
- **CSV (`.csv`)**: the column names (quoted) on the first row and the values, nothing else.

Energies and depths are given as the left edge, the centre and the right edge of each bin.
