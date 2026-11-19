# Selections

Double-click a measurement to open its ToF-E histogram. The toolbar switches between modes:

- **Zoom Box** and **Move**, as in all [plots](getting-started.md#plots)
- **Select**: click to add the vertices of a polygon. Enter or clicking the first vertex completes it, Backspace
  removes the last vertex, Esc cancels.
- **Edit**: drag vertices or whole selections, double-click an edge to add a vertex, right-click a vertex to remove it.

## Properties

A completed polygon opens the properties dialog; double-click a selection in the list (or choose **Properties...**
from its context menu) to open it again.

- **Element** and optionally the **isotope**. Without an isotope, the average mass of the natural element is used.
- **Color**, by default the colour of the element from the preferences
- **Type**:
  - **Recoil (ERD)**: recoiled target atoms
  - **Scattered (RBS)**: incident ions scattered from the target atom. They are labelled with the target atom and an
    asterisk (`Si*`), their energy is calculated with the mass of the incident ion.
  - **Reference**: not used in energy spectra or depth profiles, only to divide the measurement into parts, see
    [Split analysis](split-analysis.md). The element is optional: leave it empty for a region of the histogram
    (labelled `Ref 3` and so on), or give one (`Si (ref)`).
- **Represents the natural element**: for an isotope that occurs naturally. In depth profiles its events are
  weighted by 1 / natural abundance and the profile is labelled with the element. The abundance comes from JIBAL
  (shown in the dialog) but can be changed; the dialog shows the resulting correction.
- **Additional weight** (0–100, default 1) multiplies the weights of the selection's events in depth profiles.

## The selection list

The list next to the histogram shows the selections with their event counts. Its context menu can change the type or
colour of several selections at once, reset all colours, remove selections, and copy and paste selections between
measurements (also with Ctrl/Cmd+C and Ctrl/Cmd+V). **Divides the Measurement** makes a selection the one that divides
the measurement in split analyses and in the ranges of spectra and profiles; it is shown in bold.
