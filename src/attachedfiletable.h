/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#ifndef JARA_ATTACHEDFILETABLE_H
#define JARA_ATTACHEDFILETABLE_H

#include <QList>
#include <QStringList>
#include <QTableWidget>

struct AttachedFile;

// The attached files of a sample or of the project with their descriptions, which can be edited. Files are added and
// removed in the project tree.
class AttachedFileTable : public QTableWidget {
    Q_OBJECT

public:
    explicit AttachedFileTable(QList<AttachedFile> *files, QWidget *parent = nullptr); // Not owned
    void refresh(); // Rebuilt only when the files change, not while their descriptions are edited

signals:
    void modified(); // A description was edited

private:
    void itemEdited(QTableWidgetItem *item);

    QList<AttachedFile> *m_files;
    QStringList m_shownPaths;
};

#endif // JARA_ATTACHEDFILETABLE_H
