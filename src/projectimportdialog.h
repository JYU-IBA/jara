/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#ifndef JARA_PROJECTIMPORTDIALOG_H
#define JARA_PROJECTIMPORTDIALOG_H

#include <QDialog>
#include <QSet>

#include "project.h"

class QTreeWidget;
class QTreeWidgetItem;

// Chooses what to import from another project: experimental, detector and physics settings, samples and measurements.
// Everything is selected at first.
class ProjectImportDialog : public QDialog {
    Q_OBJECT

public:
    // With a target sample, only measurements are imported and they are added to the target sample
    ProjectImportDialog(Project &source, const QString &fileName, const Sample *targetSample, QWidget *parent = nullptr);

    bool isSelected(const ExperimentalSettings *settings) const { return m_selected.contains(settings); }
    bool isSelected(const DetectorSettings *detector) const { return m_selected.contains(detector); }
    bool isSelected(const PhysicsSettings *physics) const { return m_selected.contains(physics); }
    bool isSelected(const Sample *sample) const { return m_selected.contains(sample); } // Also when only some measurements are
    bool isSelected(const Measurement *measurement) const { return m_selected.contains(measurement); }
    void accept() override;

private:
    QTreeWidgetItem *addItem(QTreeWidgetItem *parent, const QString &text, const void *object);

    QTreeWidget *m_tree;
    QSet<const void *> m_selected;
};

#endif // JARA_PROJECTIMPORTDIALOG_H
