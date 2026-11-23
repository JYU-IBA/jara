/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#ifndef JARA_MEASUREMENTINFOVIEW_H
#define JARA_MEASUREMENTINFOVIEW_H

#include <QWidget>

class Project;
class QComboBox;
class QLabel;
class QLineEdit;
struct Measurement;

// Tab in the main work area with a measurement's name, description and data file, the settings assigned to it and the
// beam and angles that follow from them. The ToF-E histogram of the measurement has a tab of its own.
class MeasurementInfoView : public QWidget {
    Q_OBJECT

public:
    MeasurementInfoView(Project *project, Measurement *measurement, QWidget *parent = nullptr);
    Measurement *measurement() const { return m_measurement; }

public slots:
    void refresh(); // Call after the project has changed, e.g. settings were added, renamed or edited

signals:
    void modified();
    void renamed(Measurement *measurement); // While the name is typed
    void renameFinished(Measurement *measurement); // When the name has been entered, measurements are sorted by it

private:
    void updateSettingsCombos();
    void showGeometry();

    Project *m_project;
    Measurement *m_measurement;
    QLineEdit *m_nameEdit;
    QLineEdit *m_descriptionEdit;
    QLabel *m_fileLabel;
    QLabel *m_folderLabel;
    QComboBox *m_settingsCombo;
    QComboBox *m_detectorCombo;
    QComboBox *m_physicsCombo;
    QLabel *m_beamIonLabel;
    QLabel *m_beamEnergyLabel;
    QLabel *m_incidentAngleLabel;
    QLabel *m_scatteringAngleLabel;
    QLabel *m_exitAngleLabel;
};

#endif // JARA_MEASUREMENTINFOVIEW_H
