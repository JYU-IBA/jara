/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#ifndef JARA_DEPTHPROFILEVIEW_H
#define JARA_DEPTHPROFILEVIEW_H

#include <memory>
#include <vector>

#include <QWidget>

#include "depthprofile.h"
#include "spectrumplotwidget.h"
#include "tofedata.h"

class MeasurementRangeWidget;
class QCheckBox;
class QDialog;
class QGridLayout;
class QAction;
class QActionGroup;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

// Tab in the main work area showing a depth profile of a measurement. Loads the measurement's data file when created
// and iterates the profile, updating the plot after each iteration, until it converges or the maximum number of
// iterations from the preferences is reached. The user can continue iterating. Changes of the calculation made here
// start the iterations again, changes of the binning, normalization and averages only update the profile. Changes
// made elsewhere, to the selections or the settings, mark the profile as out of date until it is recalculated. A
// recalculation requested from elsewhere waits until the tab is shown in the active window.
class DepthProfileView : public QWidget {
    Q_OBJECT

public:
    DepthProfileView(Measurement *measurement, DepthProfile *profile, QWidget *parent = nullptr);
    Measurement *measurement() const { return m_measurement; }
    DepthProfile *profile() const { return m_profile; }
    QString title() const; // Measurement and profile names, marked when out of date
    bool isStale() const { return m_stale; }
    // For reports: calculates the profile from the beginning until it converges or the iterations run out, without
    // returning to the event loop. The plot then shows it, with the zoom stored in the profile.
    void calculateNow();
    SpectrumPlotWidget *plot() const { return m_plot; } // nullptr when the data file could not be loaded
    const DepthProfileResult &result() const { return m_result; }
    QString errorMessage() const; // Empty when the profile was calculated

public slots:
    void recalculate(); // Iterates from the beginning
    void checkStale(); // Call after the project has changed, also updates the selection summary
    void applyPreferences(); // Call after the preferences have changed
    // Recalculates when out of date, at once when the tab is shown in the active window, otherwise when it is
    void recalculateWhenShown();

signals:
    void modified();
    void titleChanged();

private slots:
    void iterateOrStop();
    void iterationStep();
    void chooseSelections(); // In a dialog
    void showCutRanges(); // The non-modal dialog of the parts of the measurement of selections
    void showLog(); // The non-modal dialog of the calculation as text, see depthProfileLog()
    void updateLog();
    void averageItemChanged(QTreeWidgetItem *item, int column);
    void markerMoved(int index, double x);
    void labelMoved(int index, QPointF position); // Of the centre in the depth unit shown and at.%
    void updateLabelMenu(); // Before it is shown, from the curves of the profile
    void exportProfile();

protected:
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void recalculatePending(); // When a recalculation waits and the tab is shown in the active window
    enum class DepthUnit { ThinFilm, Mass, Length };
    enum class Interval { Normalization, Average };

    QWidget *createToolBar();
    QWidget *createStaleBar();
    QWidget *createSidePanel();
    void updateSelectionSummary();
    void updateCutRanges(); // The button and, when shown, the dialog of the parts of the measurement of selections
    void updateIntervalSpins();
    void startIterating();
    void stopIterating();
    void updateProfile(SpectrumPlotWidget::ViewChange viewChange);
    void updateAverages();
    void updateIterationStatus();
    void updateLegendPosition(); // From the profile or the preferences
    void storeView(); // The zoom of the plot in the profile
    void applyStoredView(); // While the profile is calculated after the tab is opened
    void updateKinematicCheck(); // Shown when the detector has an angle calibration
    void setStale(bool stale);
    void setInterval(Interval interval, double min, double max); // Stored in the profile, depths in atoms/m²
    void setAutoNormalization(bool automatic); // A manual interval starts from the automatic one
    double toDisplay(double depth) const; // Depth in atoms/m² in the selected unit
    double fromDisplay(double value) const;
    QString depthUnitText() const;

    Measurement *m_measurement;
    DepthProfile *m_profile;
    std::vector<ToFEEvent> m_events;
    DepthProfileInputs m_calculatedInputs;
    std::unique_ptr<DepthProfileCalculation> m_calculation;
    DepthProfileResult m_result;
    bool m_stale = false;
    bool m_recalculationPending = false; // Requested while the tab was not shown in the active window
    bool m_iterating = false;
    int m_remainingIterations = 0;
    DepthUnit m_unit = DepthUnit::ThinFilm;
    QList<Interval> m_markerIntervals; // Interval of each pair of plot markers
    bool m_viewPending = false; // The stored zoom is applied when the profile has been calculated
    bool m_applyingView = false;
    QTimer *m_viewSaveTimer = nullptr; // Zooming modifies the project once it stops
    SpectrumPlotWidget *m_plot = nullptr;
    QLabel *m_selectionSummary = nullptr; // Selections included in the calculation
    MeasurementRangeWidget *m_rangeWidget = nullptr;
    QPushButton *m_cutRangesButton = nullptr;
    QDialog *m_cutRangesDialog = nullptr; // Created when first shown
    QDialog *m_logDialog = nullptr; // Created when first shown
    QPlainTextEdit *m_logText = nullptr;
    QGridLayout *m_cutRangesGrid = nullptr;
    QStringList m_cutRangesKey; // Selections in the dialog, it is rebuilt when they change
    QHash<int, QCheckBox *> m_cutRangeChecks; // By cut id
    QHash<int, MeasurementRangeWidget *> m_cutRangeWidgets;
    QCheckBox *m_autoMaxDepthCheck = nullptr;
    QCheckBox *m_kinematicCheck = nullptr; // Shown when the detector has an angle calibration
    QTimer *m_iterationTimer = nullptr;
    QDoubleSpinBox *m_stoppingBinSpin = nullptr;
    QDoubleSpinBox *m_maxDepthSpin = nullptr;
    QSpinBox *m_pseudoEventsSpin = nullptr;
    QDoubleSpinBox *m_binWidthSpin = nullptr;
    QDoubleSpinBox *m_densitySpin = nullptr; // Shown only with depths in nm
    QLabel *m_densityLabel = nullptr;
    QCheckBox *m_autoNormalizationCheck = nullptr;
    QDoubleSpinBox *m_normalizationMinSpin = nullptr;
    QDoubleSpinBox *m_normalizationMaxSpin = nullptr;
    QDoubleSpinBox *m_averageMinSpin = nullptr;
    QDoubleSpinBox *m_averageMaxSpin = nullptr;
    QCheckBox *m_followNormalizationCheck = nullptr;
    QTreeWidget *m_averageTable = nullptr;
    QPushButton *m_iterateButton = nullptr;
    QMenu *m_labelMenu = nullptr; // In the context menu of the plot
    QList<int> m_labelCutIds; // First selection of the curve of each label in the plot
    QWidget *m_staleBar = nullptr;
    QLabel *m_errorLabel = nullptr;
    QLabel *m_warningLabel = nullptr; // Elements counted more than once
    QLabel *m_statusLabel = nullptr;
    QLabel *m_iterationLabel = nullptr;
    QString m_hint;
};

#endif // JARA_DEPTHPROFILEVIEW_H
