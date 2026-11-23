/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#ifndef JARA_REPORTPDF_H
#define JARA_REPORTPDF_H

#include <QString>

class Project;
class QWidget;
struct Report;

// Writes the PDF of a report: the title, the settings of the measurements, a table of the average concentrations with
// one row per depth profile and one column per element, the plots of the depth profiles and an appendix with the
// plots of the split analyses. The depth profiles and split analyses are calculated from their data files as their
// tabs calculate them, which shows progress and can be cancelled.
//
// The averages are those of each profile: over its normalization interval when the averaging follows it, otherwise
// renormalized among its included selections. Recoils, scattered ions and isotopes of an element are summed into the
// element's column, with their uncertainties added in quadrature.
//
// Rows are labelled with the sample, the measurement when the report's mode asks for it (automatically when a sample
// has several rows) and the profile when a measurement has several. On success the report remembers the fingerprints
// of its profiles, see isReportUpToDate(). False when cancelled (no error message) or when the file could not be
// written.
bool writeReportPdf(Project &project, Report &report, const QString &fileName, QWidget *parent, QString &errorMessage);

#endif // JARA_REPORTPDF_H
