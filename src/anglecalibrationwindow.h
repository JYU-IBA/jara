/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#ifndef JARA_ANGLECALIBRATIONWINDOW_H
#define JARA_ANGLECALIBRATIONWINDOW_H

#include <map>
#include <vector>

#include <QStringList>
#include <QWidget>

#include "anglecalibration.h"
#include "tofedata.h"

class HistogramWidget;
class Project;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTimer;
class QTreeWidget;
class SpectrumPlotWidget;
struct DetectorSettings;
struct Measurement;

// Window of the angle calibration tool for a detector, see anglecalibration.h. The events of the checked measurements
// assigned to the detector are pooled. Their timing is plotted against ToF, where the longest ToF used is dragged, and
// as a distribution with the fit of the window, where the fit range can be dragged. The expected leading edge of a
// selection over the window, from its kinematics at each angle, is drawn for the chosen direction of the angle: it
// should follow the edge of the data. The calibration is applied to the detector.
class AngleCalibrationWindow : public QWidget {
    Q_OBJECT

public:
    AngleCalibrationWindow(Project *project, DetectorSettings *detector, QWidget *parent = nullptr);
    DetectorSettings *detector() const { return m_detector; }

public slots:
    void refresh(); // Call after the project has changed

signals:
    void calibrationApplied(); // The angle calibration of the detector was changed

private:
    struct LoadedData {
        QString path;
        bool loaded = false;
        QString errorMessage;
        std::vector<ToFEEvent> events;
        size_t withTiming = 0; // Events with a timing value
    };

    QList<Measurement *> measurements() const; // Assigned to the detector, in project order
    const LoadedData &loadData(Measurement *measurement);
    void updateList();
    void updateEdgeChoices();
    void collectEvents(); // Of the checked measurements, the plots start over
    void smoothEvents(); // Spreads their timing values, the plots start over
    void fit(); // With the current options, after a moment so that the controls can settle
    void showFit();
    AngleFitOptions fitOptions() const;
    void updateTofMarkers(); // At the minimum and maximum ToF
    void updateTofLimits(); // The minimum ToF can not go above the maximum, nor the maximum below the minimum
    void updateOverlay(); // Fitted window and expected edges in the timing against ToF plot
    void updateResults();
    QString angleText(double tof, double timing) const; // At the position, empty when not known
    void apply();

    Project *m_project;
    DetectorSettings *m_detector;
    std::map<Measurement *, LoadedData> m_data;
    QList<Measurement *> m_checked;
    QStringList m_collectedPaths; // Of the checked measurements when the events were collected
    qsizetype m_collectedCount = -1;
    std::vector<ToFEEvent> m_events; // Pooled, with timing values
    std::vector<ToFEEvent> m_smoothedEvents; // The above with the timing smoothing, plotted and fitted
    double m_smoothedWith = 0; // Smoothing of the events above, ticks
    std::vector<ToFEEvent> m_plotEvents; // x the ToF channel, y the smoothed timing
    double m_minTof = 0; // Of the pooled events
    double m_maxTof = 0;
    AngleFit m_fit;
    QPointF m_cursor; // ToF channel and timing at the mouse in the plots
    double m_fitMin; // Fit range dragged by the user, NaN when found automatically
    double m_fitMax;

    QListWidget *m_list;
    QComboBox *m_edgeCombo;
    QSpinBox *m_minTofSpin;
    QSpinBox *m_maxTofSpin;
    QSpinBox *m_binWidthSpin;
    QSpinBox *m_slicesSpin;
    QComboBox *m_orderCombo;
    QComboBox *m_sliceModeCombo;
    QDoubleSpinBox *m_smoothingSpin;
    QCheckBox *m_reversedCheck;
    HistogramWidget *m_histogram;
    SpectrumPlotWidget *m_distribution;
    QLabel *m_cursorInfo;
    QLabel *m_resultLabel;
    QLabel *m_edgeNote;
    QTreeWidget *m_sliceTable;
    QPushButton *m_autoRangeButton;
    QPushButton *m_applyButton;
    QTimer *m_fitTimer;
};

#endif // JARA_ANGLECALIBRATIONWINDOW_H
