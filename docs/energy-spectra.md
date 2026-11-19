# Energy spectra

An energy spectrum shows the selections of a measurement as counts against energy. Add one by right-clicking a
measurement → **Add Energy Spectrum**.

## Calculation

- The time of flight of each event is t = slope × channel + offset from the detector's
  [ToF calibration](tof-calibration.md), and the energy E = m v² / 2 with v = ToF length / t.
- The mass is that of the selected isotope, or the average mass of the natural element when no isotope is selected.
  For scattered ions it is the mass of the incident ion.
- The energy lost in the timing detector foil is added back, so the spectra show the energies before the foil.
- ToF channels are integers, so each event is spread evenly within its channel before the conversion. Otherwise the
  discrete energies of the channels would show up as spikes in narrow bins. The spreading uses a fixed seed, so the
  same inputs always give the same spectrum.

Reference selections are not included.

## Using it

- **Selections**: check the selections to include.
- **Bin width** in keV.
- **Part of the measurement**: use only the events from one percentage to another of the counts of the selection that
  divides the measurement, see [Split analysis](split-analysis.md).
- **Log Scale**, zoom and move as in all plots.
- **Export...** writes the bin edges, the centres and the counts of each selection, see
  [Exporting](getting-started.md#exporting).

Changes made in the spectrum recalculate it right away. When the selections or the settings change elsewhere, the
spectrum is marked out of date until it is refreshed.
