/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#ifndef JARA_SAMPLEVIEW_H
#define JARA_SAMPLEVIEW_H

#include <QStringList>
#include <QWidget>

class AttachedFileTable;
class QLineEdit;
class QPlainTextEdit;
class QTableWidget;
class QTableWidgetItem;
struct Sample;

// Tab in the main work area with a sample's name, description, metadata and the descriptions of its attached files.
// Files and measurements are added and removed in the project tree.
class SampleView : public QWidget {
    Q_OBJECT

public:
    explicit SampleView(Sample *sample, QWidget *parent = nullptr);
    Sample *sample() const { return m_sample; }

public slots:
    void refresh(); // Call after the project has changed, e.g. the sample was renamed or files were added

signals:
    void modified();
    void renamed(Sample *sample); // While the name is typed
    void renameFinished(); // When the name has been entered, samples are sorted by it

private slots:
    void addMetadata();
    void removeMetadata();
    void metadataItemChanged(QTableWidgetItem *item);

private:
    void updateMetadata();

    Sample *m_sample;
    QLineEdit *m_nameEdit;
    QPlainTextEdit *m_descriptionEdit;
    QTableWidget *m_metadataTable;
    AttachedFileTable *m_fileTable;
};

#endif // JARA_SAMPLEVIEW_H
