# Automatic mass selection: what was tried (removed)

An attempt at automatic selections of the ToF-E histogram: find the mass bands, identify them, and select each as deep
into the sample as it goes without significant overlap with other bands. It worked on some data but not sufficiently
well in general, and was removed. The code is in git history:

- `6e511f9` — first version: band finding, identification by leading edges, empirical mass calibration, selections,
  window, automatic flag on cuts.
- `f3946c4` — the empirical mass calibration replaced by a physical model of the energy detector, layers, artifacts,
  faster stopping.
- Both reverted together with this note.

`git show f3946c4:src/massselection.cpp` and so on give the last state of the files: `massselection.*` (core),
`energydetector.*` (detector model), `massselectionwindow.*` (UI). The pieces below are in the order the analysis ran.

## 1. Proto-mass and bands

- The proto-mass q = ln(E_ch × t²), t from the ToF calibration in ns, turns each mass into a nearly level band against
  ToF, slightly falling with ToF. Bands are much easier to follow in (ToF, q) than in ToF-E.
- Band finding: histogram of q in ToF slices of 40 ch, each slice's peaks found in the sum of ±2 neighbouring slices
  (faint bands need it), Gaussian smoothing, peaks accepted by prominence over valleys in units of the smoothed noise.
  Peaks linked slice to slice into ridges by nearest prediction (line through the last ~6 ridge points), ridges
  allowed to miss 3 slices, fragments joined when one continues another's line within a growing tolerance. Took
  ~0.3 s for 0.5–1.4 M events.
- Leading edge of each band: the ToF spectrum of events within ±2 widths of the ridge, fitted with `findAndFitEdge`
  from the ToF calibration; the first guess when the fit fails (faint bands).
- Worked well: on erda2_724 (Potku copy) and tofe_2780–2783 all main bands were found. Problems: deep low-energy
  regions produce many junk fragments; heavy bands are wide and split into parallel ridges; a band can be two
  elements of nearly equal mass at different depths (Al film on Si substrate, 27 vs 28 u), handled by splitting the
  band where its events rise sharply again ("layers").

## 2. Identification

- Expected leading edge of every naturally occurring isotope, recoiled or the beam scattered from it, from the
  kinematics, the timing foil and the ToF calibration (`expectedSurfaceTof`). With the ToF calibration right, light
  elements match within a few channels (H, Li, C, N, O, F, Si, Al all did).
- Anchors: a dynamic programme over the bands in order of proto-mass, each with one of its edge candidates or none,
  masses increasing, scored by edge match × (0.1 + abundance). Only sharp fitted edges.
- Other bands: by the calibration (below), their edge (nothing comes before its surface edge, surface particles more
  likely), abundance. Bands that continue an identified one deeper were joined to it; bands below the response of H
  were taken as artifacts (random coincidences, H with wrong E).
- Weak points: buried elements have no surface edge; heavy elements' edges are close together and ambiguous; scattered
  beam bands match many targets (it proposed "scattered from Mn" etc.); without a list of likely elements, heavy bands
  got unlikely identities. A user-given list of expected elements, or the user identifying a few bands first, would
  help more than any scoring.

## 3. Calibration of the E axis

Two approaches were tried.

1. **Empirical**: q − ln(m/u) as a polynomial in ln(m/u) and ln(t / 100 ns), up to the third degree, fitted to the
   ridges of identified bands. Very good inside the calibrated masses (0.6 % rms on erda2_724, masses within ±3 % along
   whole bands). Fails outside them: it has no Z, so it cannot extrapolate; with nothing above Al, Cl came out as
   32.7 u and was taken for S. Linear continuation beyond the range helped only a little.
2. **Physical model of a gas ionization chamber** (as suggested): the energy from ToF minus the loss in the stop timing
   foil (8 µg/cm² C) and the entrance window (100 nm Si₃N₄, energies after layers by the difference of ranges), the
   rest into the gas (isobutane), minus the part lost in nuclear stopping (∫ Sn/(Sn+Se) dE, with JIBAL stopping); the
   W-value depends on the ion (increases for heavy ions, multiple ionization):
   `E_ch = offset + gain × (1 + k (Z − 6)) × E_ionizing`, with the effective window thickness as a fitted factor
   (golden section; it came out 1.8–2.7 × nominal: window plus dead layer). On tofe_2780–2783: 0.6–1.2 % rms, Cl
   correctly identified, the Si layer under Al identified. Remaining trend: light ions a few % off at low energies,
   Cl ~4–5 % off (the linear Z term fitted to Z ≤ 13).
   - The model assumes the E channel is the whole energy in the gas. It does not fit ΔE-only data, nor ΔE + E summed
     with unequal gains (the Aalto erda2 data: ~6 % rms, Li 12 % off). A segmented-anode model was discussed but not
     done.
   - Stopping per element with mass scaling (velocity) for isotopes was accurate enough and much cheaper.

## 4. Selections

- In ToF slices (≥ 40 ch, widened to ≥ 300 events), the q distribution fitted by EM as Gaussians at the band centres
  (ridge, beyond it the calibrated response shifted to the ridge end, centres free within 1 σ, widths limited to 2.5 ×
  the ridge width) plus a flat background. Components inactive before their leading edge and, for a layer above, after
  the layer below begins.
- Boundary between neighbours where the densities are equal, at most 3 σ from the centre; a selection ends after two
  slices where other bands exceed 1 % of its events, or its events are no longer 3 σ above background.
- Polygons in (ToF, q), smoothed over ±2 slices, converted to ToF-E (E = e^q / t²) and simplified (Douglas–Peucker,
  0.2 % of E).
- Compared with hand cuts: 92–100 % of the hand cut's events inside the automatic selection for H, Li, C, N, O, F,
  Al, Si, Cl, with the automatic ones usually reaching deeper.

## 5. Practical lessons

- JIBAL re-reads a whole GSTO file whenever a stopping table needs a new pair; assigning all pairs first and loading
  once (`jibal_gsto_load_all`) took identification from 7.5 s to 0.1 s. Worth doing anywhere many particles are needed.
- The Potku project's `erda2_724.asc` and `~/Documents/ERDA/Aalto_20260930/erda2_724.asc` differ (ΔE + E summed vs
  ΔE only); the Potku cuts match only the Potku copy. Neither suits a full-energy detector model.
- Reasonable test data: `testdata/test.json`, measurements tofe_2780–2783 (AlN/Al on Si with H, C, O, Cl; 63Cu
  13.6 MeV, settings in the project) with hand cuts to compare against.
- What did not work sufficiently: robust identification without user input (buried and heavy elements, scattered
  beam), detector types other than a full-energy gas chamber, and junk fragments at long ToF. A simpler tool — the
  user identifies the bands (or lists elements) and the program only fits the detector response and draws the
  selections — may be the better next try.
