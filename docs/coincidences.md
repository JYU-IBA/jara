# Coincidence files

File → **Import Event Data...** finds ToF-E coincidences in list-mode files (`*.evnt`), writes them as
measurement files (`*.asc`) and adds them to samples.

## List-mode files

A list-mode file has one signal per line: the ADC number, the channel and the timestamp in clock ticks. Only lines
with exactly three integers are read; the header with the metadata of the measurement, and any other line, is
ignored. Files are read in chunks and processed as a stream, so their length is not limited by memory.

## Coincidence parameters

The coincidence parameters depend on the detector system and are part of the
[detector settings](settings.md#detector-settings), where **Coincidences** shows them and **Edit...** opens them. Times
are in clock ticks.

- **ToF ADC** and **E ADC**: the ADC numbers of the ToF and energy signals. Signals of other ADCs are ignored.
- **Trigger**: E or ToF. Time differences are measured from the signals of this ADC.
- **Window low** and **high**: a signal of the other ADC is in coincidence with a trigger signal when its timestamp
  minus the trigger's is inside the window (both ends included). All signals in the window are considered, the search
  is not limited to the next few lines.
- **Reorder span**: signals are put in time order in a buffer this long. A signal that comes in the file later than
  this after newer signals is dropped and counted as out of order. Files from the data acquisition so far are in time
  order.
- **Pairing**: when a trigger has several signals in its window, or a signal is in the window of several triggers:
  - **One-to-one, closest to the window centre** (default): each signal is used at most once. The pairs closest to
    the centre of the window are taken first.
  - **Discard ambiguous**: only triggers with a single signal in their window, which is in the window of no other
    trigger.
  - **All combinations**: every pair in the window, so a signal can appear in several events.

## The dialog

The files are listed with the `.asc` file written for each, next to the `.evnt` file with the same name, and the
sample it is added to: an existing sample, or a new name, which creates a sample. Files go to the current sample by
default, otherwise to a new sample named after the file.

The coincidence parameters start from the chosen detector settings and can be changed in the dialog (the ADCs only in
the detector settings). They are stored in the detector settings when the files are converted.

The plots show the time differences of the selected file over the window and some more on both sides: against the
ToF channel, and below that as a distribution with one bin per tick. The distribution's vertical axis follows the
bins in view; its scale (logarithmic by default) is changed from its context menu and remembered. The window is
marked in both plots, and its edges can be dragged with the mouse, which changes the window low and high. The plots
are calculated again only when the window no longer fits in them, so that the zoom is kept; **Refresh Plots**
calculates them again for the current window and margin. Below the plots are the counts of signals, of pairs in the window and of
ambiguous signals, and the number of coincidences with the pairing. Long files are previewed from their beginning,
which the statistics then say; converting always reads the whole file.

**Convert** asks before overwriting existing `.asc` files. The written files have the ToF and E channels and the time
difference (ticks) in three columns, in the order of the trigger timestamps. They are added as measurements with the
chosen detector settings; a file already measured in its sample is not added again, its checksum is updated.

File → Preferences → Coincidences has the default coincidence parameters of new detector settings, and sets how much
is shown around the window (100 ticks by default), how many events are read for the preview and the scale of the
distribution.
