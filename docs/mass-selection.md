# Automatic selections

**Automatic Selections...** in the toolbar of a measurement's ToF-E histogram finds the mass bands of the histogram,
identifies them and makes a selection of each, as deep into the sample (long ToF) as it goes without significant
overlap with other bands. It needs the detector settings with the ToF calibration, and for identifying the bands the
experimental settings.

## How it works

The mass of a particle is m = 2 E t² / L², but the E detector does not measure the energy alone: its response depends
on the element (pulse height defect). The proto-mass ln(E channel × t²), with t from the ToF calibration, still
separates the masses: each mass is a band that runs nearly level against ToF.

- **Bands**: in slices of ToF, the peaks of the proto-mass distribution are found (each with a few neighbouring
  slices, so that faint bands have enough events) and followed from slice to slice as ridges. Pieces of a ridge lost
  for a while are joined. The leading (short ToF) edge of each band is fitted as in the
  [ToF calibration](tof-calibration.md), or guessed for faint bands.
- **Layers**: when the events along a band rise sharply again deeper in the sample, the band is split there: e.g. a
  film of Al on a Si substrate, whose masses are too near to be told apart. The layer above ends where the one below
  begins. A layer is not taken to be the element above it, unless you identify it so; the two then make one selection.
- **Identities from edges**: the leading edges are compared with where the particles from the surface are expected,
  for all naturally occurring isotopes, recoiled or the incident ion scattered from them. The most likely combination,
  with masses growing with the proto-mass and favouring abundant isotopes, gives the bands identified **by edge**.
- **Response of the energy detector**: the bands so identified, and those you identify, calibrate a model of the
  energy detector, a gas ionization chamber. A particle with the energy from its time of flight loses energy in the
  foil of the stop timing detector and in the entrance window (energies after them by the difference of ranges), and
  gives the rest to the gas. Only the part lost in electronic stopping ionizes; the part lost in nuclear stopping in
  the gas, the larger part of the pulse height defect, is calculated with the stopping of the physics settings. The
  W-value also depends on the ion:

      E channel = offset + gain × (1 + slope × (Z − 6)) × ionizing energy

  The offset, gain and slope are fitted, and so is the effective thickness of the window as a factor: the window and
  the dead layer behind it are thicker in effect than the window alone. Anchors whose ridges follow the response
  clearly worse than the others are dropped. Without enough identified bands, the calibration of the detector is used.
- **Identities from the response**: the other bands get the most likely isotope by how well their ridges follow its
  response near their leading edges, by their edges (no particles come before those from the surface, and particles
  from the surface are more likely) and by the abundance. Beyond the elements of the calibration, the response is less
  certain. A band that continues another one deeper in the sample is joined to it, and a band below the response of H
  is no particle (e.g. random coincidences).
- **Selections**: in slices of ToF, the proto-mass distribution is fitted as a sum of Gaussians, one for each band,
  centred on its ridge, and a flat background. Beyond its ridge, a band follows the response to its particles. A band
  has no events before its leading edge. The boundary between two neighbouring bands is where their densities are
  equal, at most three widths from each centre. A selection starts a little before the leading edge of its band and
  ends where events of other bands would exceed the limit of its events, where its own events are no longer
  significant above the background, or where the layer below begins.

Isotopes are selected separately when another isotope of the element has a band of its own (e.g. ⁶Li and ⁷Li),
otherwise the selection is of the element. Scattered ions are selected as scattered from their target element.

## The window

The table lists the bands: their identity and how it was found, the measured and expected leading edge, the deviation
of the ridge from the response to the identity, the ToF range of the selection, the expected events of the band in it,
the expected fraction of other bands and the background in it, and why it ends. The histogram shows the ridges of the
bands (dotted) and the selections of the checked bands; those of the unchecked ones are dashed.

- **Double-click** a band (or **Identify...** in its context menu) to give its identity, **Clear Identity** to remove
  it. The calibration and the selections are calculated again.
- **Overlap with other bands at most** sets the limit where selections end, 1 % by default.
- **Propose Again** identifies the other bands again, your identities are kept.
- **Apply to Detector** stores the calibration of the energy detector in the detector settings, for measurements with
  too few bands identified by their edges.
- **Make Selections** makes selections of the checked bands. They are marked automatic: making them again replaces
  them, and those of the same particle keep their place in energy spectra and depth profiles. Earlier automatic
  selections that are not made again are removed when you agree. Selections drawn by hand are left alone, and
  editing the polygon of an automatic selection makes it a hand-drawn one.

Heavy ions come close to each other and to the scattered incident ions, and the response is less certain beyond the
identified elements. Check the identities of heavy bands, and raise the overlap limit when their selections end at
once. The model is of a gas ionization chamber that measures the whole energy of the particles.
