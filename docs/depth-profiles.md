# Depth profiles

A depth profile turns the selections of a measurement into concentrations against depth, with single scattering
(slab analysis). Add one by right-clicking a measurement → **Add Depth Profile**. The measurement needs experimental and
detector settings with a [ToF calibration](tof-calibration.md).

## Calculation

- The detected energy of each event is its energy from the time of flight with the loss in the timing detector foil
  added back. Events where the detection efficiency is below 10⁻⁶, which includes energies outside the efficiency
  curve, are left out.
- With **Kinematic correction** and an [angle calibration](angle-calibration.md) of the detector, each event has its
  own detection angle. Its detected energy is multiplied by K(θ) / K(θ + Δθ), K being
  the kinematic factor, θ the scattering/recoil angle of the detector and Δθ the event's offset from it, as if it was
  detected at θ. The timing of each event is spread with the timing smoothing of the calibration first. The efficiency is that of the energy before the correction. Events at an angle no particles reach
  are not corrected.
- The detected energy of a selection's particles decreases with the depth they come from: the beam slows down on the
  way in, the particle gets the kinematic fraction of the beam energy and slows down on the way out. Both depend on the
  composition, which is tabulated in stopping bins and calculated with Bragg's rule.
- Each event gets the depth where this gives its energy, and a weight of 1 / (efficiency × cross section), with
  Rutherford cross sections (optionally with the Andersen screening correction) at the beam energy at that depth. An
  isotope that represents its element is also weighted by 1 / abundance, and the selection's additional weight
  multiplies its events. See [Selections](selections.md).
- Stopping is only known within the energy range of the stopping files, e.g. 1 keV/u–10 MeV/u for SRIM-2013 (see
  [Stopping](settings.md#stopping)). The profile is not calculated when the beam energy, or the kinematic factor ×
  the beam energy of a detected particle, is above the range. Events whose particle has an energy outside the range
  on its way out, at the sample surface or in the timing detector foil, are left out. Events from depths where the
  beam has slowed down below the range get no depth and a weight of zero. With an automatic maximum depth, the
  stopping bins are not extended beyond where the beam reaches the lowest energy of its stopping data.
- The weights of each element in a stopping bin, relative to all of them, give the composition of the next iteration.
  Each bin also gets a number of pseudo-events with the composition of the bin before it (**Smoothing**), which keeps
  bins with few events from swinging and lets the iterations converge. The first iteration assumes the same composition
  at all depths.
- The iterations continue until no concentration changes more than the tolerance of the preferences, or the maximum
  number of iterations is reached. **Iterate More** continues, **Recalculate** starts again.
- **Log...** shows the calculation as text, updated while it iterates:
  - the events in the measurement, in the profile's part of it and in the selections, and how many were included or
    left out and why;
  - the elements of the composition, the stopping bins and the energy range of the beam's stopping data;
  - for each iteration, the largest change of a concentration, the number of stopping bins, the beam energy at the
    end of them, and the events with a depth, beyond the maximum depth and from where the beam is below its stopping
    data;
  - for each selection, its events and how many were used or left out (no energy or efficiency below 10⁻⁶, energy
    outside the stopping data), the detected energies and depths of the events, and the weights: the user weight, the
    abundance weight, the range weight, the average 1 / efficiency, the average cross section and event weight, and
    the selection's share of the weights of all events;
  - the parameters: the beam, the angles, the ToF calibration, the foil, the efficiency curves, the kinematic
    correction and angle calibration, the part of the measurement, the selections, the cross sections, the stopping
    settings and the stopping file used for each particle in each element with the energy range of its data, the
    stopping bins and the numerical parameters of the calculation;
  - JIBAL: its version, configuration file, data files and all of its GSTO files.

  The text can be selected and copied.
- The profile bins the events again with the profile bin width. The concentrations are normalized so that the total
  is 100 % on average over the normalization interval. Uncertainties are statistical and include the normalization.
  The weights of a selection using its own part of the measurement (see below) come from the reference counts in
  that part, whose binomial uncertainty is included as well. A bin or an average without events is exactly 0, shown
  as "0 at.%" without an uncertainty.
- With **Round Concentrations** (see Averages and Plot), averages less than twice their uncertainty are shown as 84 % upper
  limits, e.g. "< 0.07 at.%". The average and its uncertainty are taken as n = (average / uncertainty)² events of
  equal weight, whose Poisson upper limit is n + 1 + √(n + ¾) events (Gehrels' approximation).

Depths are areal densities, shown in 10¹⁵ at./cm², µg/cm² or nm (with the density given in the panel).

## Parameters

- **Selections**: the summary shows how many are included; **Choose...** picks them. Reference selections are not
  used.
- **Part of the measurement**: use only the events from one percentage to another of the counts of the selection that
  divides the measurement, see [Split analysis](split-analysis.md).
- **Per Selection...** opens a dialog where a selection can use a narrower part of that part, e.g. hydrogen only from
  the beginning before it is lost. The part is given as percentages of the profile's part. The selection's events are
  weighted by the reference counts in the profile's part / in its own part (shown in the dialog), so that it stands for
  the same fluence as the others. The button shows how many selections have their own part.
- **Stopping bin**, **Max. depth** (or **Auto**, which follows the depth the events reach), **Smoothing**: changing
  them starts the iterations again.
- **Profile bin**: the bins of the profile shown.
- **Kinematic correction**: corrects each event from its own angle, see above. On by default, shown only when the
  detector has an angle calibration with the detector distance and window width. Changing it starts the iterations
  again.
- **100 % over**: the normalization interval, found automatically (**Auto**) as the longest stretch where the
  smoothed total stays near its typical level, or set by hand.

Isotopes of an element are summed into one curve unless **Isotopes** is checked in the toolbar. A warning is shown
when an element is counted more than once, e.g. an isotope representing its element together with another isotope.

## Averages

The table lists the average concentration of each curve with its uncertainty, the average integrated over the
averaging interval as an areal density in 10¹⁵ at./cm² (e.g. the amount of an element in a layer) and the events.

- **Over**: the averaging interval. With **Normalization** checked (the default) it is the normalization interval,
  where the total is already 100 %. Otherwise the averages are also **renormalized** to 100 % among the checked
  selections of the Renorm. column, e.g. to leave out the substrate.
- The checkbox in the Selection column shows or hides the curve in the plot.
- **Round Concentrations** in the context menu of the table rounds the averages to their uncertainties (one
  significant digit, two when they are 15 or less) and shows those not clearly above zero as upper limits, see above,
  in the table, the header of the export and reports. Otherwise they are shown as they are, to 0.01 at.%. New profiles
  take it from the preferences, off by default. The concentrations of the bins in the export are never rounded.

## Plot

- The zoom of the plot is remembered in the project and restored when the tab is opened again. Reports can use it for
  their plots, see [Reports](reports.md).
- The dashed orange lines are the averaging interval and can be dragged; with Normalization checked they move the
  normalization interval. **Normalization** in the toolbar also shows the normalization interval, **Total** the sum of
  the concentrations.
- The context menu has **Concentrations in the Legend**, **Round Concentrations** (in the legend, as in the table of
  averages, on by default for new profiles), **Shade Averaging Interval** and **Labels**: the names of the selections
  in the plot at their averages, in the middle of the averaging interval. Labels are spread apart so they do not
  overlap; a dragged label stays at its depth and concentration. The submenu chooses which selections get a label and
  resets dragged labels.
- **Export...** writes the bin edges and centres, the depths in µg/cm² and nm, and for each curve the concentration,
  its uncertainty and the events, with the averages, also as areal densities, in the header.
  Concentrations are written to 0.0001 at.%. See [Exporting](getting-started.md#exporting).

Changes of the selections or settings made elsewhere mark the profile out of date until it is recalculated.
