# Depth profiles

A depth profile turns the selections of a measurement into concentrations against depth, with single scattering
(slab analysis). Add one by right-clicking a measurement → **Add Depth Profile**. The measurement needs experimental and
detector settings with a [ToF calibration](tof-calibration.md).

## Calculation

- The detected energy of each event is its energy from the time of flight with the loss in the timing detector foil
  added back. Events where the detection efficiency is below 10⁻⁶, which includes energies outside the efficiency
  curve, are left out.
- The detected energy of a selection's particles decreases with the depth they come from: the beam slows down on the
  way in, the particle gets the kinematic fraction of the beam energy and slows down on the way out. Both depend on the
  composition, which is tabulated in stopping bins and calculated with Bragg's rule.
- Each event gets the depth where this gives its energy, and a weight of 1 / (efficiency × cross section), with
  Rutherford cross sections (optionally with the Andersen screening correction) at the beam energy at that depth. An
  isotope that represents its element is also weighted by 1 / abundance, and the selection's additional weight
  multiplies its events. See [Selections](selections.md).
- The weights of each element in a stopping bin, relative to all of them, give the composition of the next iteration.
  Each bin also gets a number of pseudo-events with the composition of the bin before it (**Smoothing**), which keeps
  bins with few events from swinging and lets the iterations converge. The first iteration assumes the same composition
  at all depths.
- The iterations continue until no concentration changes more than the tolerance of the preferences, or the maximum
  number of iterations is reached. **Iterate More** continues, **Recalculate** starts again.
- The profile bins the events again with the profile bin width. The concentrations are normalized so that the total
  is 100 % on average over the normalization interval. Uncertainties are statistical and include the normalization.

Depths are areal densities, shown in 10¹⁵ at/cm², µg/cm² or nm (with the density given in the panel).

## Parameters

- **Selections**: the summary shows how many are included; **Choose...** picks them. Reference selections are not
  used.
- **Part of the measurement**: use only the events from one percentage to another of the counts of the selection that
  divides the measurement, see [Split analysis](split-analysis.md).
- **Stopping bin**, **Max. depth** (or **Auto**, which follows the depth the events reach), **Smoothing**: changing
  them starts the iterations again.
- **Profile bin**: the bins of the profile shown.
- **100 % over**: the normalization interval, found automatically (**Auto**) as the longest stretch where the
  smoothed total stays near its typical level, or set by hand.

Isotopes of an element are summed into one curve unless **Isotopes** is checked in the toolbar. A warning is shown
when an element is counted more than once, e.g. an isotope representing its element together with another isotope.

## Averages

The table lists the average concentration of each curve with its uncertainty and the events.

- **Over**: the averaging interval. With **Normalization** checked (the default) it is the normalization interval,
  where the total is already 100 %. Otherwise the averages are also **renormalized** to 100 % among the checked
  selections of the Renorm. column, e.g. to leave out the substrate.
- The checkbox in the Selection column shows or hides the curve in the plot.

## Plot

- The dashed orange lines are the averaging interval and can be dragged; with Normalization checked they move the
  normalization interval. **Normalization** in the toolbar also shows the normalization interval, **Total** the sum of
  the concentrations.
- The context menu has **Concentrations in the Legend**, **Shade Averaging Interval** and **Labels**: the names of
  the selections in the plot at their averages, in the middle of the averaging interval. Labels are spread apart so
  they do not overlap; a dragged label stays at its depth and concentration. The submenu chooses which selections get
  a label and resets dragged labels.
- **Export...** writes the bin edges and centres, the depths in µg/cm² and nm, and for each curve the concentration,
  its uncertainty and the events, with the averages in the header. Concentrations are written to 0.0001 at.%. See
  [Exporting](getting-started.md#exporting).

Changes of the selections or settings made elsewhere mark the profile out of date until it is recalculated.
