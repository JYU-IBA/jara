/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#ifndef JARA_TOFCALIBRATIONWINDOW_H
#define JARA_TOFCALIBRATIONWINDOW_H

#include <map>
#include <vector>

#include <QList>
#include <QPointF>
#include <QWidget>

#include "tofcalibration.h"
#include "tofedata.h"

class CalibrationPlotWidget;
class Project;
class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class SpectrumPlotWidget;
struct Cut;
struct DetectorSettings;
struct Measurement;

// Window of the ToF calibration tool for a detector. Lists the selections of the measurements assigned to the
// detector. Clicking one shows its ToF spectrum in channels and fits its leading edge. The edge can also be set by
// hand. Accepted edges are stored in the selections and give the points of the calibration line. Data files are
// loaded when their selections are first clicked.
class ToFCalibrationWindow : public QWidget {
    Q_OBJECT

public:
    ToFCalibrationWindow(Project *project, DetectorSettings *detector, QWidget *parent = nullptr);
    DetectorSettings *detector() const { return m_detector; }

public slots:
    void refresh(); // Call after the project has changed

signals:
    void modified(); // An edge was accepted or removed
    void calibrationApplied(); // The slope and offset of the detector were changed

private slots:
    void currentItemChanged(QTreeWidgetItem *item);
    void windowMarkerMoved(int index, double x);
    void edgePicked(double x);
    void resetToFit();
    void acceptEdge();
    void removeEdge();
    void applyCalibration();

private:
    struct LoadedData {
        QString path;
        bool loaded = false;
        QString errorMessage;
        std::vector<ToFEEvent> events;
    };

    // A selection with an accepted edge
    struct CalibrationPoint {
        Measurement *measurement;
        int cutId;
        QString label;
        double channel;
        double tof; // s, NaN when it cannot be calculated
        QString errorMessage;
    };

    QWidget *createSpectrumPanel();
    QWidget *createCalibrationPanel();
    QList<Measurement *> measurements() const; // Assigned to the detector, in project order
    Cut *currentCut() const; // nullptr when no selection is shown
    const LoadedData &loadData(Measurement *measurement);
    void updateTree();
    void showSelection(Measurement *measurement, int cutId); // Fits the edge again
    void showFit();
    void updateInfo();
    void updateCalibration();

    Project *m_project;
    DetectorSettings *m_detector;
    std::map<Measurement *, LoadedData> m_data;

    // Shown selection
    Measurement *m_measurement = nullptr;
    int m_cutId = 0;
    QList<QPointF> m_polygon; // The histogram is made again when the polygon changes
    std::vector<size_t> m_counts;
    EdgeFit m_fit;
    double m_edge = 0; // Channel
    bool m_hasEdge = false;
    bool m_manual = false; // The edge was set by hand

    QList<CalibrationPoint> m_points;
    LinearFit m_line;

    QTreeWidget *m_tree;
    SpectrumPlotWidget *m_plot;
    QLabel *m_infoLabel;
    QLabel *m_statusLabel;
    QPushButton *m_acceptButton;
    QPushButton *m_resetButton;
    QPushButton *m_removeButton;
    CalibrationPlotWidget *m_calibrationPlot;
    QTreeWidget *m_pointTable;
    QLabel *m_lineLabel;
    QPushButton *m_applyButton;
    QString m_hint;
};

#endif // JARA_TOFCALIBRATIONWINDOW_H
