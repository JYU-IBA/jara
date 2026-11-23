/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#include "preferencesdialog.h"
#include "formlayout.h"
#include "defaults.h"
#include "elements.h"
#include "settingseditors.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent), m_experimental(Defaults::experimentalSettings()), m_detector(Defaults::detectorSettings()),
      m_physics(Defaults::physicsSettings()),
      m_histogram(Defaults::histogramPreferences()), m_depthProfile(Defaults::depthProfilePreferences()),
      m_coincidence(Defaults::coincidencePreferences()), m_elementColors(Defaults::customElementColors()) {
    setWindowTitle("Preferences");

    auto *tabs = new QTabWidget;
    tabs->addTab(createExperimentalPage(), "Experimental");
    tabs->addTab(createDetectorPage(), "Detector");
    tabs->addTab(createPhysicsPage(), "Physics");
    tabs->addTab(createHistogramPage(), "Histogram");
    tabs->addTab(createDepthProfilePage(), "Depth Profiles");
    tabs->addTab(createCoincidencePage(), "Coincidences");
    tabs->addTab(createElementColorsPage(), "Element Colors");
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

namespace {
// A page of default settings, the editor stretched when it can use the space. The editors have group boxes of their own.
QWidget *settingsPage(QWidget *editor, bool stretch) {
    auto *note = new QLabel("New projects start with these settings, and new settings sets are created from them.");
    note->setWordWrap(true);

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(note);
    layout->addWidget(editor, stretch ? 1 : 0);
    if(!stretch) {
        layout->addStretch();
    }
    return page;
}
}

QWidget *PreferencesDialog::createExperimentalPage() {
    auto *editor = new ExperimentalSettingsEditor;
    editor->setSettings(&m_experimental);
    return settingsPage(editor, false);
}

QWidget *PreferencesDialog::createDetectorPage() {
    auto *editor = new DetectorSettingsEditor;
    editor->setDetector(&m_detector);
    editor->setCoincidenceVisible(false); // On the coincidence page
    return settingsPage(editor, false);
}

QWidget *PreferencesDialog::createPhysicsPage() {
    auto *editor = new PhysicsSettingsEditor;
    editor->setPhysics(&m_physics);
    return settingsPage(editor, true);
}

QWidget *PreferencesDialog::createHistogramPage() {
    auto binSpinBox = [](int value) {
        auto *spinBox = new QSpinBox;
        spinBox->setRange(1, 1024);
        spinBox->setValue(value);
        return spinBox;
    };
    auto *minX = binSpinBox(m_histogram.minBinX);
    auto *minY = binSpinBox(m_histogram.minBinY);
    auto *maxX = binSpinBox(m_histogram.maxBinX);
    auto *maxY = binSpinBox(m_histogram.maxBinY);
    auto binRow = [](QSpinBox *x, QSpinBox *y) {
        auto *row = new QHBoxLayout;
        row->addWidget(x);
        row->addWidget(new QLabel("×"));
        row->addWidget(y);
        row->addWidget(new QLabel("ch (ToF × E)"));
        row->addStretch();
        return row;
    };

    auto *schemeCombo = new QComboBox;
    for(ColorScheme scheme : ColorSchemes) {
        schemeCombo->addItem(colorSchemeName(scheme), static_cast<int>(scheme));
    }
    schemeCombo->setCurrentIndex(schemeCombo->findData(static_cast<int>(m_histogram.colorScheme)));
    auto *scaleCombo = new QComboBox;
    scaleCombo->addItem("Logarithmic", true);
    scaleCombo->addItem("Linear", false);
    scaleCombo->setCurrentIndex(m_histogram.logScale ? 0 : 1);

    auto *note = new QLabel("The bin size follows the zoom level within these limits. The color scale can be changed "
                            "in each histogram from its context menu.");
    note->setWordWrap(true);

    auto *binGroup = new QGroupBox("Bin size");
    auto *binForm = createFormLayout(binGroup);
    binForm->addRow(note);
    binForm->addRow("Minimum:", binRow(minX, minY));
    binForm->addRow("Maximum:", binRow(maxX, maxY));
    auto *colorGroup = new QGroupBox("Colors");
    auto *colorForm = createFormLayout(colorGroup);
    colorForm->addRow("Color scheme:", schemeCombo);
    colorForm->addRow("Default color scale:", scaleCombo);
    alignFormLabels({binForm, colorForm});
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(binGroup);
    layout->addWidget(colorGroup);
    layout->addStretch();

    // The maximum is kept at least the minimum by moving the other value
    connect(minX, &QSpinBox::valueChanged, this, [this, maxX](int value) {
        m_histogram.minBinX = value;
        maxX->setValue(std::max(maxX->value(), value));
    });
    connect(minY, &QSpinBox::valueChanged, this, [this, maxY](int value) {
        m_histogram.minBinY = value;
        maxY->setValue(std::max(maxY->value(), value));
    });
    connect(maxX, &QSpinBox::valueChanged, this, [this, minX](int value) {
        m_histogram.maxBinX = value;
        minX->setValue(std::min(minX->value(), value));
    });
    connect(maxY, &QSpinBox::valueChanged, this, [this, minY](int value) {
        m_histogram.maxBinY = value;
        minY->setValue(std::min(minY->value(), value));
    });
    connect(schemeCombo, &QComboBox::currentIndexChanged, this, [this, schemeCombo]() {
        m_histogram.colorScheme = static_cast<ColorScheme>(schemeCombo->currentData().toInt());
    });
    connect(scaleCombo, &QComboBox::currentIndexChanged, this, [this, scaleCombo]() {
        m_histogram.logScale = scaleCombo->currentData().toBool();
    });
    return page;
}

QWidget *PreferencesDialog::createCoincidencePage() {
    auto *margin = new QSpinBox;
    margin->setRange(0, 1000000);
    margin->setSuffix(" ticks");
    margin->setValue(m_coincidence.plotMargin);
    margin->setToolTip("The plot of the time differences shows this much more on both sides of the window");
    auto *preview = new QSpinBox;
    preview->setRange(1000, 1000000000);
    preview->setGroupSeparatorShown(true);
    preview->setValue(m_coincidence.previewSignals);
    preview->setToolTip("Long files are previewed from their beginning. Converting always reads the whole file.");
    auto *scaleCombo = new QComboBox;
    scaleCombo->addItem("Logarithmic", true);
    scaleCombo->addItem("Linear", false);
    scaleCombo->setCurrentIndex(m_coincidence.logScale ? 0 : 1);

    auto *coincidenceEditor = new CoincidenceSettingsEditor;
    coincidenceEditor->setSettings(&m_detector.coincidence);
    auto *note = new QLabel("Importing event data (*.evnt). The coincidence parameters are part of the detector "
                            "settings, new detector settings start with these.");
    note->setWordWrap(true);
    auto *parametersGroup = new QGroupBox("Default coincidence parameters");
    auto *parametersLayout = new QVBoxLayout(parametersGroup);
    parametersLayout->addWidget(coincidenceEditor);
    auto *previewGroup = new QGroupBox("Preview");
    auto *previewForm = createFormLayout(previewGroup);
    previewForm->addRow("Shown around the window:", margin);
    previewForm->addRow("Events in the preview:", preview);
    previewForm->addRow("Time difference counts:", scaleCombo);
    alignFormLabels({coincidenceEditor->formLayout(), previewForm});

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(note);
    layout->addWidget(parametersGroup);
    layout->addWidget(previewGroup);
    layout->addStretch();

    connect(margin, &QSpinBox::valueChanged, this, [this](int value) { m_coincidence.plotMargin = value; });
    connect(preview, &QSpinBox::valueChanged, this, [this](int value) { m_coincidence.previewSignals = value; });
    connect(scaleCombo, &QComboBox::currentIndexChanged, this, [this, scaleCombo]() {
        m_coincidence.logScale = scaleCombo->currentData().toBool();
    });
    return page;
}

QWidget *PreferencesDialog::createDepthProfilePage() {
    auto *iterations = new QSpinBox;
    iterations->setRange(1, 1000);
    iterations->setValue(m_depthProfile.maxIterations);
    auto *tolerance = new QDoubleSpinBox;
    tolerance->setDecimals(4);
    tolerance->setRange(0.0001, 10);
    tolerance->setSuffix(" at.%");
    tolerance->setValue(m_depthProfile.tolerance * 100);
    auto *note = new QLabel("Depth profiles are iterated until no concentration used for stopping changes more than "
                            "the tolerance, or until the maximum number of iterations. Iterating can be continued in "
                            "each depth profile.");
    note->setWordWrap(true);
    auto *iterationGroup = new QGroupBox("Iterations");
    auto *iterationForm = createFormLayout(iterationGroup);
    iterationForm->addRow(note);
    iterationForm->addRow("Maximum iterations:", iterations);
    iterationForm->addRow("Tolerance:", tolerance);

    // Depths are shown in 1e15 at./cm², stored in atoms/m²
    auto depthSpin = [](double value, double minimum) {
        auto *spinBox = new QDoubleSpinBox;
        spinBox->setDecimals(1);
        spinBox->setRange(minimum, 1e6);
        spinBox->setValue(value / Units::ThinFilmUnit);
        return spinBox;
    };
    auto *binWidth = depthSpin(m_depthProfile.binWidth, 0.1);
    auto *stoppingBinWidth = depthSpin(m_depthProfile.stoppingBinWidth, 0.1);
    auto *maxDepth = depthSpin(m_depthProfile.maxDepth, 1);
    auto *autoMaxDepth = new QCheckBox("Automatic");
    autoMaxDepth->setChecked(m_depthProfile.autoMaxDepth);
    autoMaxDepth->setToolTip("The maximum depth follows the depth the events reach, starting from the value given");
    auto *maxDepthRow = new QHBoxLayout;
    maxDepthRow->addWidget(maxDepth);
    maxDepthRow->addWidget(autoMaxDepth);
    maxDepthRow->addStretch(1);
    auto *smoothing = new QSpinBox;
    smoothing->setRange(1, 100000);
    smoothing->setValue(m_depthProfile.smoothingEvents);
    smoothing->setToolTip("Each stopping bin gets this many events with the composition of the bin above it");
    auto *parameterGroup = new QGroupBox("Parameters (depths in 10¹⁵ at./cm²)");
    auto *parameterForm = createFormLayout(parameterGroup);
    parameterForm->addRow("Profile bin:", binWidth);
    parameterForm->addRow("Stopping bin:", stoppingBinWidth);
    parameterForm->addRow("Smoothing events:", smoothing);
    parameterForm->addRow("Maximum depth:", maxDepthRow);

    auto *legend = new QComboBox;
    for(const LegendPosition position : LegendCorners) {
        legend->addItem(legendPositionName(position), static_cast<int>(position));
    }
    legend->setCurrentIndex(legend->findData(static_cast<int>(m_depthProfile.legendPosition)));
    auto *plotGroup = new QGroupBox("Plot");
    auto *plotForm = createFormLayout(plotGroup);
    legend->setToolTip("Dragging the legend of a depth profile to another corner gives it its own position");
    plotForm->addRow("Legend:", legend);
    auto *legendConcentrations = new QCheckBox("Renormalized concentrations in the legend");
    legendConcentrations->setChecked(m_depthProfile.showLegendConcentrations);
    plotForm->addRow(legendConcentrations);
    const QString roundTip = QStringLiteral("Averages rounded to their uncertainties, upper limits for those less than "
                                            "twice their uncertainty");
    auto *roundLegend = new QCheckBox("Round the concentrations in the legend");
    roundLegend->setChecked(m_depthProfile.roundLegendConcentrations);
    roundLegend->setToolTip(roundTip);
    plotForm->addRow(roundLegend);
    auto *roundTable = new QCheckBox("Round the concentrations in the table of averages");
    roundTable->setChecked(m_depthProfile.roundTableConcentrations);
    roundTable->setToolTip(roundTip + QStringLiteral(", also in the export and reports"));
    plotForm->addRow(roundTable);
    alignFormLabels({iterationForm, parameterForm, plotForm});

    auto *pageNote = new QLabel("Depth profiles start with these parameters and plot settings and can change them.");
    pageNote->setWordWrap(true);
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(pageNote);
    layout->addWidget(iterationGroup);
    layout->addWidget(parameterGroup);
    layout->addWidget(plotGroup);
    layout->addStretch();
    connect(iterations, &QSpinBox::valueChanged, this, [this](int value) { m_depthProfile.maxIterations = value; });
    connect(tolerance, &QDoubleSpinBox::valueChanged, this, [this](double value) { m_depthProfile.tolerance = value / 100; });
    auto connectDepth = [this](QDoubleSpinBox *spinBox, double DepthProfilePreferences::*member) {
        connect(spinBox, &QDoubleSpinBox::valueChanged, this, [this, member](double value) {
            m_depthProfile.*member = value * Units::ThinFilmUnit;
        });
    };
    connectDepth(binWidth, &DepthProfilePreferences::binWidth);
    connectDepth(stoppingBinWidth, &DepthProfilePreferences::stoppingBinWidth);
    connectDepth(maxDepth, &DepthProfilePreferences::maxDepth);
    connect(smoothing, &QSpinBox::valueChanged, this, [this](int value) { m_depthProfile.smoothingEvents = value; });
    connect(autoMaxDepth, &QCheckBox::toggled, this, [this](bool checked) { m_depthProfile.autoMaxDepth = checked; });
    connect(legend, &QComboBox::currentIndexChanged, this, [this, legend]() {
        m_depthProfile.legendPosition = static_cast<LegendPosition>(legend->currentData().toInt());
    });
    connect(legendConcentrations, &QCheckBox::toggled, this, [this](bool checked) {
        m_depthProfile.showLegendConcentrations = checked;
    });
    connect(roundLegend, &QCheckBox::toggled, this, [this](bool checked) {
        m_depthProfile.roundLegendConcentrations = checked;
    });
    connect(roundTable, &QCheckBox::toggled, this, [this](bool checked) {
        m_depthProfile.roundTableConcentrations = checked;
    });
    return page;
}

QWidget *PreferencesDialog::createElementColorsPage() {
    auto *table = new QGridLayout;
    table->setSpacing(2);
    table->setRowMinimumHeight(Elements::LanthanideRow - 1, 8);
    m_elementGroup = new QButtonGroup(this);
    for(int z = 1; z <= std::min(Elements::maxZ(), 118); z++) {
        auto *button = new QToolButton;
        button->setText(Elements::symbol(z));
        button->setFixedSize(34, 28);
        m_elementGroup->addButton(button, z);
        const QPoint position = Elements::tablePosition(z);
        table->addWidget(button, position.y(), position.x());
        updateElementButton(z);
    }

    auto *note = new QLabel("New selections get the color of their element. Click an element to change its color.");
    note->setWordWrap(true);
    auto *resetButton = new QPushButton("Reset All Colors");
    auto *resetLayout = new QHBoxLayout;
    resetLayout->addStretch();
    resetLayout->addWidget(resetButton);

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(note);
    layout->addLayout(table);
    layout->addLayout(resetLayout);
    layout->addStretch();

    connect(m_elementGroup, &QButtonGroup::idClicked, this, [this](int z) {
        const QColor color = QColorDialog::getColor(m_elementColors.value(z, Defaults::builtInElementColor(z)), this,
                                                    QString("Color of %1").arg(Elements::symbol(z)));
        if(!color.isValid()) {
            return;
        }
        if(color == Defaults::builtInElementColor(z)) {
            m_elementColors.remove(z);
        } else {
            m_elementColors.insert(z, color);
        }
        updateElementButton(z);
    });
    connect(resetButton, &QPushButton::clicked, this, [this]() {
        const QList<int> changed = m_elementColors.keys();
        m_elementColors.clear();
        for(int z : changed) {
            updateElementButton(z);
        }
    });
    return page;
}

void PreferencesDialog::updateElementButton(int z) {
    QAbstractButton *button = m_elementGroup->button(z);
    const QColor color = m_elementColors.value(z, Defaults::builtInElementColor(z));
    const QColor text = color.lightnessF() > 0.6 ? Qt::black : Qt::white;
    // Changed colors are shown with a thicker border
    button->setStyleSheet(QString("QToolButton { background-color: %1; color: %2; border: %3px solid %4; border-radius: 3px; }")
                                  .arg(color.name(), text.name())
                                  .arg(m_elementColors.contains(z) ? 2 : 1)
                                  .arg(color.darker(160).name()));
    button->setToolTip(QString("Z = %1, %2%3")
                               .arg(z)
                               .arg(color.name())
                               .arg(m_elementColors.contains(z) ? QString(" (changed)") : QString()));
}

void PreferencesDialog::accept() {
    Defaults::setExperimentalSettings(m_experimental);
    Defaults::setDetectorSettings(m_detector);
    Defaults::setPhysicsSettings(m_physics);
    Defaults::setHistogramPreferences(m_histogram);
    Defaults::setDepthProfilePreferences(m_depthProfile);
    Defaults::setCoincidencePreferences(m_coincidence);
    Defaults::setCustomElementColors(m_elementColors);
    QDialog::accept();
}
