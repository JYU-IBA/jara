/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#ifndef JARA_TABLEEXPORT_H
#define JARA_TABLEEXPORT_H

#include <QList>
#include <QString>
#include <QStringList>

class QWidget;

// A table exported as a text file, with the comments as lines starting with "# ", the column names on a comment line
// and tab-separated values, or as a CSV file with only the column names on the first row and the values. Numbers are
// formatted by the caller without the locale, so that other programs can read them.
struct ExportTable {
    QStringList comments;
    QStringList columns;
    QList<QStringList> rows;
};

// Asks for the file, the format follows the extension or the chosen file type. Returns the file written, empty when
// cancelled or when it could not be written, which is reported.
QString exportTable(QWidget *parent, const QString &title, const QString &baseName, const ExportTable &table);

#endif // JARA_TABLEEXPORT_H
