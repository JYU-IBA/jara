/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#ifndef JARA_SPLITVIEW_H
#define JARA_SPLITVIEW_H

#include <vector>

#include <QWidget>

#include "spectrumplotwidget.h"
#include "split.h"
#include "tofedata.h"

class QLabel;
class QListWidget;
class QListWidgetItem;
class QSpinBox;
class QTreeWidget;

// Tab in the main work area showing a split analysis of a measurement: the counts of selections in parts with equal
// counts of the measurement's reference, see split.h. Loads the measurement's data file when created. Counting is
// quick, so the analysis is recalculated whenever the selections, the reference or the parts change.
class SplitView : public QWidget {
    Q_OBJECT

public:
    SplitView(Measurement *measurement, SplitAnalysis *split, QWidget *parent = nullptr);
    Measurement *measurement() const { return m_measurement; }
    SplitAnalysis *split() const { return m_split; }
    QString title() const; // Measurement and analysis names
    SpectrumPlotWidget *plot() const { return m_plot; } // nullptr when the data file could not be loaded
    QString errorMessage() const { return m_result.errorMessage; }

public slots:
    void refresh(); // Call after the project has changed, recalculates when the inputs have changed

signals:
    void modified();

private slots:
    void selectionItemChanged(QListWidgetItem *item);
    void exportSplit();

private:
    QWidget *createToolBar();
    QWidget *createSelectionPanel();
    void updateSelectionList();
    void recalculate();
    void updatePlot(SpectrumPlotWidget::ViewChange viewChange);
    void updateTable();
    QString referenceText() const; // What divides the measurement
    QString partsText() const; // The reference counts of the measurement and of its parts

    Measurement *m_measurement;
    SplitAnalysis *m_split;
    std::vector<ToFEEvent> m_events;
    SplitInputs m_calculatedInputs;
    SplitResult m_result;
    SpectrumPlotWidget *m_plot = nullptr;
    QSpinBox *m_partsSpin = nullptr;
    QListWidget *m_selectionList = nullptr;
    QTreeWidget *m_table = nullptr;
    QLabel *m_referenceLabel = nullptr;
    QLabel *m_errorLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QString m_hint;
};

#endif // JARA_SPLITVIEW_H
