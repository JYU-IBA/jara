# Automatic selections

**Automatic Selections...** in the toolbar of a measurement's ToF-E histogram finds the mass bands of the histogram,
identifies them and makes a selection of each, as deep into the sample (long ToF) as it goes without significant
overlap with other bands. It needs the detector settings with the ToF calibration, and for identifying the bands the
experimental settings.

## How it works

The mass of a particle is m = 2 E t² / L², but the E detector is not linear in energy and its response depends on the
mass (pulse height defect). The proto-mass ln(E channel × t²), with t from the ToF calibration, still separates the
masses: each mass is a band that runs nearly level against ToF.

- **Bands**: in slices of ToF, the peaks of the proto-mass distribution are found (each with a few neighbouring
  slices, so that faint bands have enough events) and followed from slice to slice as ridges. Pieces of a ridge lost
  for a while are joined. The leading (short ToF) edge of each band is fitted as in the
  [ToF calibration](tof-calibration.md).
- **Identities**: the leading edges are compared with where the particles from the surface are expected, for all
  naturally occurring isotopes, recoiled or the incident ion scattered from them. The most likely combination, with
  masses growing with the proto-mass and favouring abundant isotopes, gives the bands identified **by edge**.
- **Mass calibration**: the proto-mass minus ln(m / u) is fitted as a polynomial of ln(m / u) and ln(t), up to the
  third degree, to the ridges of the identified bands. Bands far from their calibrated mass lose their identity. The
  other bands get the naturally occurring isotope nearest to their calibrated mass (**by mass**), within the masses
  the calibration was fitted to, unless they continue another band deeper in the sample. Without enough bands
  identified by their edges, the mass calibration of the detector is used.
- **Selections**: in slices of ToF, the proto-mass distribution is fitted as a sum of Gaussians, one for each band,
  centred on its ridge, and a flat background. Beyond its ridge, a band follows its mass in the calibration. The
  boundary between two neighbouring bands is where their densities are equal, at most three widths from each centre.
  A selection starts a little before the leading edge of its band and ends where events of other bands would exceed
  the limit of its events, or where its own events are no longer significant above the background.

Isotopes are selected separately when another isotope of the element has a band of its own (e.g. ⁶Li and ⁷Li),
otherwise the selection is of the element. Scattered ions are selected as scattered from their target element.

## The window

The table lists the bands: their identity and how it was found, the measured and expected leading edge, the mass from
the calibration, the ToF range of the selection, the expected events of the band in it, the expected fraction of
other bands and the background in it, and why it ends. The histogram shows the ridges of the bands (dotted) and the
selections of the checked bands; those of the unchecked ones are dashed. The cursor info gives the calibrated mass.

- **Double-click** a band (or **Identify...** in its context menu) to give its identity, **Clear Identity** to remove
  it. The calibration and the selections are calculated again.
- **Overlap with other bands at most** sets the limit where selections end, 1 % by default.
- **Propose Again** identifies the bands again; your identities are discarded.
- **Apply to Detector** stores the mass calibration in the detector settings, for measurements whose bands cannot be
  identified by their edges.
- **Make Selections** makes selections of the checked bands. They are marked automatic: making them again replaces
  them, and those of the same particle keep their place in energy spectra and depth profiles. Earlier automatic
  selections that are not made again are removed when you agree. Selections drawn by hand are left alone, and
  editing the polygon of an automatic selection makes it a hand-drawn one.

Heavy ions have a large pulse height defect, so their bands come close to each other and to the scattered incident
ions, and the mass calibration does not reach beyond the heaviest identified band. Check the identities of heavy
bands, and raise the overlap limit when their selections end at once.
