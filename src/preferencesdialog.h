/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#ifndef JARA_PREFERENCESDIALOG_H
#define JARA_PREFERENCESDIALOG_H

#include <QColor>
#include <QDialog>
#include <QMap>

#include "defaults.h"
#include "project.h"

class QButtonGroup;

// Edits the program-wide default experimental, detector and physics settings, histogram, depth profile and
// coincidence preferences and the default colors of elements. They are stored when the dialog is accepted.
class PreferencesDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget *parent = nullptr);
    void accept() override;

private:
    QWidget *createExperimentalPage();
    QWidget *createDetectorPage();
    QWidget *createPhysicsPage();
    QWidget *createHistogramPage();
    QWidget *createElementColorsPage();
    QWidget *createDepthProfilePage();
    QWidget *createCoincidencePage();
    void updateElementButton(int z);

    ExperimentalSettings m_experimental;
    DetectorSettings m_detector;
    PhysicsSettings m_physics;
    HistogramPreferences m_histogram;
    DepthProfilePreferences m_depthProfile;
    CoincidencePreferences m_coincidence;
    QMap<int, QColor> m_elementColors; // Colors changed by the user, by Z
    QButtonGroup *m_elementGroup = nullptr;
};

#endif // JARA_PREFERENCESDIALOG_H
