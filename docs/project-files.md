# Project files

A project is a JSON file (`*.json`). It holds the samples, their measurements, the settings and the reports, and
refers to the data files, see [Getting started](getting-started.md#projects). Only the current format version is
read.

## Saves

Each save records who saved the project and with what:

- `saved`: the time of the save, local time with the offset from UTC. `created` is the time the project was created.
- `saveCount`: the number of times the project has been saved, one more on every save.
- `savedBy`: the user name of the last save, from the environment (`USER`, or `USERNAME` on Windows). Left out when
  neither is set.
- `savedWithVersion`: the version of JaRA, e.g. `0.2.1+3.gabcdef0` for a development build, see
  [Building](building.md#version-information).
- `savedWithJibal`: the version of JIBAL (`version`), the [JIBAL configuration](settings.md#jibal-configuration) file
  in use (`configFile`) and where it comes from (`configSource`): `distribution` (installed with JaRA), `user` (JIBAL's
  user configuration), `file` (another file) or `none`. This is the file actually used, whatever the preference was,
  and tells which masses, abundances and stopping data the project was last saved with.

The project information shows the time of the last save, who saved it and the number of saves, e.g.
"2026-11-25 22:40 by jaakjuli, saved 3 times".

Settings files exported from the settings also record `savedWithVersion`.
