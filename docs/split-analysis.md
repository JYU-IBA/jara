# Split analysis

The beam can change the sample during a measurement, e.g. release hydrogen. A split analysis shows this by dividing
the measurement into parts and counting the selections in each part. Add one by right-clicking a measurement →
**Add Split Analysis**.

## Dividing the measurement

The events of a measurement file are in the order they were measured. The measurement is divided by the counts of one
selection, the reference: with N reference counts and n parts, each part has N / n reference counts. A stable signal,
e.g. the substrate, makes the parts correspond to equal fluences. The events before the first reference count belong
to the first part, the ones after the last to the last.

Which selection divides the measurement is chosen in the measurement tab: draw a selection of the type **Reference**
(it can be without an element), or right-click any selection → **Divides the Measurement**. The first reference
selection divides the measurement automatically. Without one, every event counts as a reference count.

## The analysis

- **Parts**: the number of parts (2–1000).
- **Selections**: the selections shown, references included.
- The plot shows the counts of each part with √N error bars against the reference counts in %, and a line fitted to
  them. **Relative to Mean** divides the counts of each selection by their mean, so that different selections can be
  compared. The counts axis does not start from zero, so that small changes are visible.
- The table lists the counts, the mean per part, the change over the measurement (the slope of the line over 0–100 %
  relative to the mean, ± its standard deviation) and the fitted line at the start relative to the mean, e.g. for
  correcting losses. The counts are taken to be Poisson distributed around their mean.
- **Export...** writes the counts of each part, with the changes in the header. See
  [Exporting](getting-started.md#exporting).

## Using a part of the measurement

[Energy spectra](energy-spectra.md) and [depth profiles](depth-profiles.md) have a **Part of the measurement** range,
from one percentage to another of the reference counts, e.g. 0–30 % to use only the events before the losses build up.
The range is shown with the reference counts and events in it.

In a depth profile, a selection can also use a narrower part of the profile's part (**Per Selection...**), e.g. the
profile 0–70 % and hydrogen 0–30 %. Its events are weighted by the ratio of the reference counts (here 70/30), see
[Depth profiles](depth-profiles.md).
