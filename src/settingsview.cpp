/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#include "settingsview.h"
#include "project.h"
#include "settingseditors.h"

#include <QLabel>
#include <QListWidget>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {
constexpr int MeasurementRole = Qt::UserRole;
}

SettingsView::SettingsView(Project *project, ExperimentalSettings *settings, QWidget *parent)
    : QWidget(parent), m_project(project), m_settings(settings) {
    auto *editor = new ExperimentalSettingsEditor;
    m_experimentalEditor = editor;
    editor->setSettings(settings);
    connect(editor, &ExperimentalSettingsEditor::changed, this, &SettingsView::modified);
    connect(editor, &ExperimentalSettingsEditor::nameChanged, this, &SettingsView::nameChanged);
    createLayout(editor);
}

SettingsView::SettingsView(Project *project, DetectorSettings *detector, QWidget *parent)
    : QWidget(parent), m_project(project), m_detector(detector) {
    auto *editor = new DetectorSettingsEditor;
    editor->setDetector(detector);
    editor->setCalibrationAvailable(true);
    m_detectorEditor = editor;
    connect(editor, &DetectorSettingsEditor::changed, this, &SettingsView::modified);
    connect(editor, &DetectorSettingsEditor::nameChanged, this, &SettingsView::nameChanged);
    connect(editor, &DetectorSettingsEditor::editingFinished, this, &SettingsView::editingFinished);
    connect(editor, &DetectorSettingsEditor::calibrationRequested, this, &SettingsView::calibrationRequested);
    connect(editor, &DetectorSettingsEditor::angleCalibrationRequested, this, &SettingsView::angleCalibrationRequested);
    createLayout(editor);
}

SettingsView::SettingsView(Project *project, PhysicsSettings *physics, QWidget *parent)
    : QWidget(parent), m_project(project), m_physics(physics) {
    auto *editor = new PhysicsSettingsEditor;
    editor->setPhysics(physics);
    m_physicsEditor = editor;
    connect(editor, &PhysicsSettingsEditor::changed, this, &SettingsView::modified);
    connect(editor, &PhysicsSettingsEditor::nameChanged, this, &SettingsView::nameChanged);
    connect(editor, &PhysicsSettingsEditor::editingFinished, this, &SettingsView::editingFinished);
    createLayout(editor);
}

void SettingsView::createLayout(QWidget *editor) {
    m_assignedList = new QListWidget;
    auto *title = new QLabel(m_settings ? "Experimental settings" : m_detector ? "Detector settings" : "Physics settings");
    QFont font = title->font();
    font.setBold(true);
    title->setFont(font);

    // The form keeps its natural size, the list of measurements takes the rest of the height. When the tab is too small
    // for them, it scrolls.
    editor->setSizePolicy(editor->sizePolicy().horizontalPolicy(), QSizePolicy::Fixed);
    m_assignedList->setMinimumHeight(m_assignedList->fontMetrics().height() * 6);
    auto *content = new QWidget;
    auto *column = new QVBoxLayout;
    column->addWidget(title);
    column->addWidget(editor);
    column->addWidget(new QLabel("Assigned measurements (double-click to open):"));
    column->addWidget(m_assignedList, 1);
    auto *row = new QHBoxLayout(content);
    row->addLayout(column);
    row->addStretch(1);
    auto *scrollArea = new QScrollArea;
    scrollArea->setWidget(content);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(scrollArea);

    connect(m_assignedList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        emit measurementActivated(reinterpret_cast<Measurement *>(item->data(MeasurementRole).value<quintptr>()));
    });
    updateAssigned();
}

void SettingsView::refresh() {
    if(m_experimentalEditor) {
        m_experimentalEditor->setSettings(m_settings);
    }
    if(m_physicsEditor) {
        m_physicsEditor->refresh();
    }
    if(m_detectorEditor) {
        m_detectorEditor->refresh();
    }
}

QString SettingsView::name() const {
    const QString &name = m_settings ? m_settings->name : m_detector ? m_detector->name : m_physics->name;
    return name.isEmpty() ? QStringLiteral("(unnamed)") : name;
}

void SettingsView::updateAssigned() {
    const QList<Measurement *> measurements = m_settings   ? m_project->measurementsUsing(m_settings)
                                              : m_detector ? m_project->measurementsUsing(m_detector)
                                                           : m_project->measurementsUsing(m_physics);
    m_assignedList->clear();
    for(size_t i = 0; i < m_project->sampleCount(); i++) {
        const Sample *sample = m_project->sample(i);
        const QString sampleName = sample->name.isEmpty() ? QStringLiteral("(unnamed)") : sample->name;
        for(const auto &measurement : sample->measurements) {
            if(measurements.contains(measurement.get())) {
                auto *item = new QListWidgetItem(QString("%1 / %2").arg(sampleName, measurement->name));
                item->setData(MeasurementRole, QVariant::fromValue(reinterpret_cast<quintptr>(measurement.get())));
                m_assignedList->addItem(item);
            }
        }
    }
}
