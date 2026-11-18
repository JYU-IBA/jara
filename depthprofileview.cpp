#include "depthprofileview.h"
#include "colorbutton.h"
#include "defaults.h"
#include "elements.h"
#include "project.h"

#include <algorithm>
#include <cmath>

#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
const QString ZoomHint = QStringLiteral(
        "Drag to zoom, double-click to show everything. Drag the dashed lines to change the normalization and "
        "averaging intervals.");
const QString MoveHint = QStringLiteral("Drag to move the view. Scroll the mouse wheel or pinch to zoom.");
const QString ThinFilmText = QStringLiteral("10¹⁵ at/cm²");
constexpr double Nanometre = 1e-9;
constexpr double GramPerCubicCentimetre = 1000; // kg/m³
constexpr int CutIdRole = Qt::UserRole;
constexpr int ConcentrationDecimals = 2; // at.%

enum SelectionColumn { IncludedColumn, PlotColumn };
constexpr double MaxDepthSpin = 1e6; // 1e15 at/cm²
const QColor NormalizationColor(0x1f, 0x77, 0xb4);
const QColor AverageColor(0xff, 0x7f, 0x0e);

enum AverageColumn {
    AverageSelectionColumn,
    AverageColumn,
    UncertaintyColumn,
    RenormalizedColumn,
    RenormalizedUncertaintyColumn,
    EventsColumn
};

QDoubleSpinBox *depthSpinBox(double minimum) {
    auto *spinBox = new QDoubleSpinBox;
    spinBox->setDecimals(1);
    spinBox->setRange(minimum, MaxDepthSpin);
    spinBox->setKeyboardTracking(false); // Updated when the value is entered, not on every key
    return spinBox;
}

void setValueSilently(QDoubleSpinBox *spinBox, double value) {
    const QSignalBlocker blocker(spinBox);
    spinBox->setValue(value);
}

QString percent(double fraction) {
    return std::isnan(fraction) ? QStringLiteral("—") : QString::number(fraction * 100, 'f', ConcentrationDecimals);
}

// Largest change of the latest iteration in at.%
QString changeText(double change) {
    if(std::isnan(change)) {
        return QStringLiteral("depth range changed");
    }
    const double threshold = std::pow(10.0, -ConcentrationDecimals);
    return change * 100 < threshold ? QString("change < %1 at.%").arg(threshold)
                                    : QString("change %1 at.%").arg(change * 100, 0, 'f', ConcentrationDecimals);
}
}

DepthProfileView::DepthProfileView(Measurement *measurement, DepthProfile *profile, QWidget *parent)
    : QWidget(parent), m_measurement(measurement), m_profile(profile) {
    auto *layout = new QVBoxLayout(this);

    QString errorMessage;
    bool loaded = false;
    if(measurement->path.isEmpty()) {
        errorMessage = "The measurement has no data file.";
    } else {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        loaded = loadAscFile(measurement->path, m_events, errorMessage);
        QApplication::restoreOverrideCursor();
    }
    if(!loaded) {
        auto *label = new QLabel(QString("Could not load %1:\n%2").arg(measurement->path, errorMessage));
        label->setAlignment(Qt::AlignCenter);
        label->setWordWrap(true);
        layout->addWidget(label);
        return;
    }

    m_plot = new SpectrumPlotWidget;
    SpectrumPlotWidget::YAxis yAxis;
    yAxis.title = QStringLiteral("Concentration (at.%)");
    yAxis.maxUnit = QStringLiteral("at.%");
    yAxis.logFloor = 0.01;
    yAxis.decimals = ConcentrationDecimals;
    yAxis.legendEvents = false;
    m_plot->setYAxis(yAxis);
    m_iterationTimer = new QTimer(this);
    m_iterationTimer->setSingleShot(true);
    connect(m_iterationTimer, &QTimer::timeout, this, &DepthProfileView::iterationStep);
    connect(m_plot, &SpectrumPlotWidget::markerMoved, this, &DepthProfileView::markerMoved);
    m_hint = ZoomHint;
    m_statusLabel = new QLabel(m_hint);
    m_statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); // Long texts do not move anything
    m_errorLabel = new QLabel;
    m_errorLabel->setStyleSheet("color: red");
    m_errorLabel->setWordWrap(true);
    m_errorLabel->hide();

    auto *splitter = new QSplitter;
    splitter->addWidget(m_plot);
    splitter->addWidget(createSidePanel());
    splitter->setStretchFactor(0, 1);
    splitter->setSizes({850, 460});

    layout->addWidget(createToolBar());
    layout->addWidget(createStaleBar());
    layout->addWidget(m_errorLabel);
    layout->addWidget(splitter, 1);
    auto *statusRow = new QHBoxLayout;
    statusRow->addWidget(m_statusLabel, 1);
    statusRow->addWidget(m_iterationLabel);
    layout->addLayout(statusRow);

    connect(m_plot, &SpectrumPlotWidget::cursorInfoChanged, this, [this](const QString &text) {
        m_statusLabel->setText(text.isEmpty() ? m_hint : text);
    });
    updateSelectionList();
    recalculate();
}

QString DepthProfileView::title() const {
    const QString title = QString("%1 — %2").arg(m_measurement->name, m_profile->name);
    return m_stale ? title + QStringLiteral(" •") : title;
}

QWidget *DepthProfileView::createToolBar() {
    auto *toolBar = new QToolBar;
    toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    QAction *zoomIn = toolBar->addAction("Zoom In", m_plot, &SpectrumPlotWidget::zoomIn);
    zoomIn->setShortcut(QKeySequence::ZoomIn);
    QAction *zoomOut = toolBar->addAction("Zoom Out", m_plot, &SpectrumPlotWidget::zoomOut);
    zoomOut->setShortcut(QKeySequence::ZoomOut);
    // Shortcuts apply only in this tab, several profiles can be open
    for(QAction *action : {zoomIn, zoomOut}) {
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        addAction(action);
    }
    toolBar->addAction("Show All", m_plot, &SpectrumPlotWidget::resetZoom);
    toolBar->addSeparator();

    auto *modes = new QActionGroup(this);
    auto addMode = [this, toolBar, modes](const QString &text, SpectrumPlotWidget::Mode mode, const QString &hint) {
        QAction *action = toolBar->addAction(text);
        action->setCheckable(true);
        modes->addAction(action);
        connect(action, &QAction::triggered, this, [this, mode, hint]() {
            m_plot->setMode(mode);
            m_hint = hint;
            m_statusLabel->setText(hint);
            m_plot->setFocus();
        });
        return action;
    };
    addMode("Zoom Box", SpectrumPlotWidget::Mode::Zoom, ZoomHint)->setChecked(true);
    addMode("Move", SpectrumPlotWidget::Mode::Move, MoveHint);
    toolBar->addSeparator();

    QAction *logScale = toolBar->addAction("Log Scale");
    logScale->setCheckable(true);
    logScale->setChecked(m_plot->logScale());
    connect(logScale, &QAction::toggled, m_plot, &SpectrumPlotWidget::setLogScale);
    connect(m_plot, &SpectrumPlotWidget::logScaleChanged, logScale, &QAction::setChecked);
    auto addToggle = [this, toolBar](const QString &text, const QString &toolTip, bool DepthProfile::*member) {
        QAction *action = toolBar->addAction(text);
        action->setCheckable(true);
        action->setChecked(m_profile->*member);
        action->setToolTip(toolTip);
        connect(action, &QAction::toggled, this, [this, member](bool checked) {
            m_profile->*member = checked;
            emit modified();
            updateProfile(SpectrumPlotWidget::ViewChange::Keep);
        });
    };
    addToggle("Total", "Show the sum of the concentrations", &DepthProfile::showTotal);
    addToggle("Normalization", "Show the interval where the total is 100 % on average", &DepthProfile::showNormalization);
    toolBar->addSeparator();

    auto *unitCombo = new QComboBox;
    unitCombo->addItem(ThinFilmText);
    unitCombo->addItem("µg/cm²");
    unitCombo->addItem("nm");
    toolBar->addWidget(new QLabel("Depth: "));
    toolBar->addWidget(unitCombo);
    connect(unitCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_unit = static_cast<DepthUnit>(index);
        // The density only converts depths to nm, it is shown with them
        m_densityLabel->setVisible(m_unit == DepthUnit::Length);
        m_densitySpin->setVisible(m_unit == DepthUnit::Length);
        updateProfile(SpectrumPlotWidget::ViewChange::ShowAll);
    });
    toolBar->addSeparator();

    m_iterateButton = new QPushButton("Iterate More");
    m_iterateButton->setToolTip("Continue iterating until the profile converges or the maximum number of iterations is reached");
    toolBar->addWidget(m_iterateButton);
    connect(m_iterateButton, &QPushButton::clicked, this, &DepthProfileView::iterateOrStop);
    toolBar->addAction("Recalculate", this, &DepthProfileView::recalculate)->setToolTip("Iterate from the beginning");
    toolBar->addAction("Export...", this, &DepthProfileView::exportProfile);
    m_iterationLabel = new QLabel; // Shown in the status row, wide enough for its longest text
    m_iterationLabel->setMinimumWidth(m_iterationLabel->fontMetrics().horizontalAdvance(
            QStringLiteral("Iteration 9999, depth range changed, not converged")));
    m_iterationLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return toolBar;
}

QWidget *DepthProfileView::createStaleBar() {
    m_staleBar = new QWidget;
    m_staleBar->setAutoFillBackground(true);
    QPalette palette = m_staleBar->palette();
    palette.setColor(QPalette::Window, QColor(0xff, 0xe0, 0x82));
    palette.setColor(QPalette::WindowText, Qt::black);
    m_staleBar->setPalette(palette);
    auto *label = new QLabel("The profile is out of date: the selections or the experimental or detector settings have "
                             "changed since it was calculated.");
    label->setWordWrap(true);
    auto *recalculate = new QPushButton("Recalculate");
    auto *layout = new QHBoxLayout(m_staleBar);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->addWidget(label, 1);
    layout->addWidget(recalculate);
    connect(recalculate, &QPushButton::clicked, this, &DepthProfileView::recalculate);
    m_staleBar->hide();
    return m_staleBar;
}

QWidget *DepthProfileView::createSidePanel() {
    m_selectionList = new QTreeWidget;
    m_selectionList->setRootIsDecorated(false);
    m_selectionList->setHeaderLabels({"Included", "Plot"});
    m_selectionList->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_selectionList->setToolTip("Included selections are part of the calculation, the plot shows some of them");
    connect(m_selectionList, &QTreeWidget::itemChanged, this, &DepthProfileView::selectionItemChanged);
    auto *selectionGroup = new QGroupBox("Selections");
    auto *selectionLayout = new QVBoxLayout(selectionGroup);
    selectionLayout->setContentsMargins(6, 6, 6, 6);
    selectionLayout->addWidget(m_selectionList);

    // Calculation: changes start the iterations again
    m_stoppingBinSpin = depthSpinBox(0.1);
    m_stoppingBinSpin->setValue(m_profile->stoppingBinWidth / Units::ThinFilmUnit);
    m_stoppingBinSpin->setToolTip("Depth bins of the composition used for stopping, changes start the iterations again");
    m_maxDepthSpin = depthSpinBox(1);
    m_maxDepthSpin->setValue(m_profile->maxDepth / Units::ThinFilmUnit);
    m_maxDepthSpin->setEnabled(!m_profile->autoMaxDepth);
    m_maxDepthSpin->setToolTip("Of the calculation");
    m_autoMaxDepthCheck = new QCheckBox("Auto");
    m_autoMaxDepthCheck->setChecked(m_profile->autoMaxDepth);
    m_autoMaxDepthCheck->setToolTip("Extended when events come from deeper, reduced when the events end well before it");
    auto *maxDepthRow = new QHBoxLayout;
    maxDepthRow->addWidget(m_maxDepthSpin, 1);
    maxDepthRow->addWidget(m_autoMaxDepthCheck);
    connect(m_autoMaxDepthCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_profile->autoMaxDepth = checked;
        if(!checked && m_calculation && m_calculation->isValid()) {
            m_profile->maxDepth = m_calculation->maxDepth(); // Continues from where the automatic one got
        }
        m_maxDepthSpin->setEnabled(!checked);
        emit modified();
        recalculate();
    });
    m_pseudoEventsSpin = new QSpinBox;
    m_pseudoEventsSpin->setRange(1, 100000);
    m_pseudoEventsSpin->setValue(m_profile->minStoppingBinEvents);
    m_pseudoEventsSpin->setKeyboardTracking(false);
    m_pseudoEventsSpin->setToolTip("Each stopping bin gets this many events with the composition of the bin above it. "
                                   "Bins with fewer events mostly follow the bins above them.");
    auto restartOnChange = [this](QDoubleSpinBox *spinBox, double DepthProfile::*member) {
        connect(spinBox, &QDoubleSpinBox::valueChanged, this, [this, member](double value) {
            m_profile->*member = value * Units::ThinFilmUnit;
            emit modified();
            recalculate();
        });
    };
    restartOnChange(m_stoppingBinSpin, &DepthProfile::stoppingBinWidth);
    restartOnChange(m_maxDepthSpin, &DepthProfile::maxDepth);
    connect(m_pseudoEventsSpin, &QSpinBox::valueChanged, this, [this](int value) {
        m_profile->minStoppingBinEvents = value;
        emit modified();
        recalculate();
    });

    // Profile: changes only bin the events again
    m_binWidthSpin = depthSpinBox(0.1);
    m_binWidthSpin->setValue(m_profile->binWidth / Units::ThinFilmUnit);
    m_binWidthSpin->setToolTip("Depth bins of the profile");
    m_autoNormalizationCheck = new QCheckBox("Auto");
    m_autoNormalizationCheck->setChecked(m_profile->autoNormalization);
    m_autoNormalizationCheck->setToolTip("The longest interval where the moving average of the total stays within "
                                         "10 % of its typical level, or within its statistical deviation");
    m_normalizationMinSpin = depthSpinBox(-MaxDepthSpin);
    m_normalizationMaxSpin = depthSpinBox(-MaxDepthSpin);
    auto *normalizationRow = new QHBoxLayout;
    normalizationRow->addWidget(m_autoNormalizationCheck);
    normalizationRow->addWidget(m_normalizationMinSpin, 1);
    normalizationRow->addWidget(new QLabel("–"));
    normalizationRow->addWidget(m_normalizationMaxSpin, 1);

    // Two fields in a row, the depths share the unit in the title
    auto *parameterGroup = new QGroupBox(QString("Parameters (depths in %1)").arg(ThinFilmText));
    auto *grid = new QGridLayout(parameterGroup);
    grid->setContentsMargins(6, 6, 6, 6);
    grid->setVerticalSpacing(4);
    grid->addWidget(new QLabel("Stopping bin:"), 0, 0);
    grid->addWidget(m_stoppingBinSpin, 0, 1);
    grid->addWidget(new QLabel("Max. depth:"), 0, 2);
    grid->addLayout(maxDepthRow, 0, 3);
    grid->addWidget(new QLabel("Smoothing:"), 1, 0);
    grid->addWidget(m_pseudoEventsSpin, 1, 1);
    grid->addWidget(new QLabel("Profile bin:"), 1, 2);
    grid->addWidget(m_binWidthSpin, 1, 3);
    m_densitySpin = new QDoubleSpinBox;
    m_densitySpin->setDecimals(3);
    m_densitySpin->setRange(0.001, 100);
    m_densitySpin->setKeyboardTracking(false);
    m_densitySpin->setValue(m_profile->density / GramPerCubicCentimetre);
    m_densitySpin->setSuffix(" g/cm³");
    m_densitySpin->setToolTip("Density of the sample, converts the depth to nm");
    m_densityLabel = new QLabel("Density:");
    m_densityLabel->hide(); // Until depths are shown in nm
    m_densitySpin->hide();
    grid->addWidget(m_densityLabel, 3, 0);
    grid->addWidget(m_densitySpin, 3, 1);
    connect(m_densitySpin, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        m_profile->density = value * GramPerCubicCentimetre;
        emit modified();
        updateProfile(SpectrumPlotWidget::ViewChange::ShowAll);
    });
    auto *normalizationLabel = new QLabel("100 % over:");
    normalizationLabel->setToolTip("The total concentration is 100 % on average over this interval");
    grid->addWidget(normalizationLabel, 2, 0);
    grid->addLayout(normalizationRow, 2, 1, 1, 3);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);
    connect(m_binWidthSpin, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        m_profile->binWidth = value * Units::ThinFilmUnit;
        emit modified();
        updateProfile(SpectrumPlotWidget::ViewChange::Keep);
    });
    connect(m_autoNormalizationCheck, &QCheckBox::toggled, this, [this](bool checked) {
        setAutoNormalization(checked);
        emit modified();
        updateProfile(SpectrumPlotWidget::ViewChange::Keep);
    });
    auto intervalSpin = [this](QDoubleSpinBox *spinBox, Interval interval, bool isMin) {
        connect(spinBox, &QDoubleSpinBox::valueChanged, this, [this, interval, isMin](double value) {
            const double depth = value * Units::ThinFilmUnit;
            if(interval == Interval::Normalization) {
                setInterval(interval, isMin ? depth : m_profile->normalizationMin, isMin ? m_profile->normalizationMax : depth);
            } else {
                setInterval(interval, isMin ? depth : m_profile->averageMin, isMin ? m_profile->averageMax : depth);
            }
        });
    };
    intervalSpin(m_normalizationMinSpin, Interval::Normalization, true);
    intervalSpin(m_normalizationMaxSpin, Interval::Normalization, false);

    // Averages
    m_averageMinSpin = depthSpinBox(-MaxDepthSpin);
    m_averageMaxSpin = depthSpinBox(-MaxDepthSpin);
    intervalSpin(m_averageMinSpin, Interval::Average, true);
    intervalSpin(m_averageMaxSpin, Interval::Average, false);
    auto *averageRow = new QHBoxLayout;
    averageRow->addWidget(new QLabel(QString("Over (%1):").arg(ThinFilmText)));
    averageRow->addWidget(m_averageMinSpin, 1);
    averageRow->addWidget(new QLabel("–"));
    averageRow->addWidget(m_averageMaxSpin, 1);
    m_averageTable = new QTreeWidget;
    m_averageTable->setRootIsDecorated(false);
    m_averageTable->setHeaderLabels({"Selection", "at.%", "±", "Renorm. at.%", "±", "Events"});
    m_averageTable->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_averageTable->setToolTip("Unchecked selections, e.g. the substrate, are left out of the renormalized averages");
    connect(m_averageTable, &QTreeWidget::itemChanged, this, &DepthProfileView::averageItemChanged);
    auto *averageGroup = new QGroupBox("Average concentrations");
    auto *averageLayout = new QVBoxLayout(averageGroup);
    averageLayout->setContentsMargins(6, 6, 6, 6);
    averageLayout->addLayout(averageRow);
    averageLayout->addWidget(m_averageTable);
    updateIntervalSpins();

    auto *panel = new QWidget;
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(selectionGroup);
    layout->addWidget(parameterGroup);
    layout->addWidget(averageGroup, 1);
    auto *scroll = new QScrollArea;
    scroll->setWidget(panel);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    return scroll;
}

void DepthProfileView::updateSelectionList() {
    const QSignalBlocker blocker(m_selectionList);
    m_selectionList->clear();
    for(const Cut &cut : m_measurement->cuts) {
        auto *item = new QTreeWidgetItem(m_selectionList, {cutLabel(cut)});
        item->setIcon(IncludedColumn, colorSwatch(cut.color));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(IncludedColumn, m_profile->cutIds.contains(cut.id) ? Qt::Checked : Qt::Unchecked);
        item->setCheckState(PlotColumn, m_profile->cutIdsHiddenFromPlot.contains(cut.id) ? Qt::Unchecked : Qt::Checked);
        item->setData(IncludedColumn, CutIdRole, cut.id);
    }
    // As tall as the selections need, up to a limit, the list scrolls after it
    constexpr int MaxVisibleSelections = 10;
    const int rows = std::clamp(m_selectionList->topLevelItemCount(), 1, MaxVisibleSelections);
    const int rowHeight = m_selectionList->topLevelItemCount() > 0 ? m_selectionList->sizeHintForRow(0)
                                                                    : m_selectionList->fontMetrics().height() + 4;
    m_selectionList->setFixedHeight(m_selectionList->header()->sizeHint().height() + rows * rowHeight
                                    + 2 * m_selectionList->frameWidth());
}

void DepthProfileView::setAutoNormalization(bool automatic) {
    if(!automatic && m_profile->autoNormalization) {
        // Starts from the interval found automatically
        m_profile->normalizationMin = m_result.normalizationMin;
        m_profile->normalizationMax = m_result.normalizationMax;
    }
    m_profile->autoNormalization = automatic;
    const QSignalBlocker blocker(m_autoNormalizationCheck);
    m_autoNormalizationCheck->setChecked(automatic);
}

void DepthProfileView::updateIntervalSpins() {
    // The automatic interval is shown when there is one
    const bool automatic = m_profile->autoNormalization && m_result.normalizationMax > m_result.normalizationMin;
    const double normalizationMin = automatic ? m_result.normalizationMin : m_profile->normalizationMin;
    const double normalizationMax = automatic ? m_result.normalizationMax : m_profile->normalizationMax;
    setValueSilently(m_normalizationMinSpin, normalizationMin / Units::ThinFilmUnit);
    setValueSilently(m_normalizationMaxSpin, normalizationMax / Units::ThinFilmUnit);
    m_normalizationMinSpin->setEnabled(!m_profile->autoNormalization);
    m_normalizationMaxSpin->setEnabled(!m_profile->autoNormalization);
    setValueSilently(m_averageMinSpin, m_profile->averageMin / Units::ThinFilmUnit);
    setValueSilently(m_averageMaxSpin, m_profile->averageMax / Units::ThinFilmUnit);
}

void DepthProfileView::setInterval(Interval interval, double min, double max) {
    if(min > max) {
        std::swap(min, max);
    }
    if(interval == Interval::Normalization) {
        m_profile->normalizationMin = min;
        m_profile->normalizationMax = max;
    } else {
        m_profile->averageMin = min;
        m_profile->averageMax = max;
    }
    emit modified();
    updateProfile(SpectrumPlotWidget::ViewChange::Keep);
}

void DepthProfileView::selectionItemChanged(QTreeWidgetItem *item, int column) {
    const int id = item->data(IncludedColumn, CutIdRole).toInt();
    if(column == PlotColumn) {
        m_profile->cutIdsHiddenFromPlot.removeAll(id);
        if(item->checkState(PlotColumn) != Qt::Checked) {
            m_profile->cutIdsHiddenFromPlot.append(id);
        }
        emit modified();
        updateProfile(SpectrumPlotWidget::ViewChange::Keep);
        return;
    }
    m_profile->cutIds.removeAll(id);
    if(item->checkState(IncludedColumn) == Qt::Checked) {
        m_profile->cutIds.append(id);
    }
    emit modified();
    recalculate();
}

void DepthProfileView::averageItemChanged(QTreeWidgetItem *item, int column) {
    if(column != AverageSelectionColumn || !item->data(AverageSelectionColumn, CutIdRole).isValid()) {
        return;
    }
    const int id = item->data(AverageSelectionColumn, CutIdRole).toInt();
    m_profile->cutIdsExcludedFromAverage.removeAll(id);
    if(item->checkState(AverageSelectionColumn) != Qt::Checked) {
        m_profile->cutIdsExcludedFromAverage.append(id);
    }
    emit modified();
    updateProfile(SpectrumPlotWidget::ViewChange::Keep);
}

void DepthProfileView::recalculate() {
    if(!m_plot) {
        return;
    }
    stopIterating();
    m_calculatedInputs = depthProfileInputs(*m_measurement, *m_profile);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_calculation = std::make_unique<DepthProfileCalculation>(m_events, m_calculatedInputs);
    QApplication::restoreOverrideCursor();
    setStale(false);
    if(!m_calculation->isValid()) {
        m_result = {};
        m_errorLabel->setText(m_calculation->errorMessage());
        m_errorLabel->show();
        updateProfile(SpectrumPlotWidget::ViewChange::ShowAll);
        updateIterationStatus();
        return;
    }
    m_errorLabel->hide();
    startIterating();
}

void DepthProfileView::startIterating() {
    m_remainingIterations = Defaults::depthProfilePreferences().maxIterations;
    m_iterating = true;
    m_iterateButton->setText("Stop");
    m_iterationTimer->start(0);
}

void DepthProfileView::stopIterating() {
    m_iterating = false;
    if(m_iterationTimer) {
        m_iterationTimer->stop();
    }
    if(m_iterateButton) {
        m_iterateButton->setText("Iterate More");
    }
    updateIterationStatus();
}

void DepthProfileView::iterateOrStop() {
    if(m_iterating) {
        stopIterating();
    } else if(m_calculation && m_calculation->isValid()) {
        startIterating();
    }
}

void DepthProfileView::iterationStep() {
    // One iteration at a time from the event loop, so that the plot is updated and the user can stop
    if(!m_iterating || !m_calculation) {
        return;
    }
    const bool first = m_calculation->iterations() == 0;
    m_calculation->iterate();
    m_remainingIterations--;
    const bool converged = m_calculation->lastChange() < Defaults::depthProfilePreferences().tolerance;
    const bool finished = converged || m_remainingIterations <= 0;
    // The concentration axis is scaled to the final profile, the depth range is kept
    updateProfile(first ? SpectrumPlotWidget::ViewChange::ShowAll
                        : finished ? SpectrumPlotWidget::ViewChange::KeepEnergyRange : SpectrumPlotWidget::ViewChange::Keep);
    if(finished) {
        stopIterating();
        return;
    }
    updateIterationStatus();
    m_iterationTimer->start(0);
}

void DepthProfileView::updateIterationStatus() {
    if(!m_calculation || !m_calculation->isValid() || m_calculation->iterations() == 0) {
        m_iterationLabel->setText(m_iterating ? QStringLiteral("Iterating...") : QString());
        return;
    }
    const double change = m_calculation->lastChange();
    const bool converged = change < Defaults::depthProfilePreferences().tolerance;
    QString text = QString("Iteration %1, %2").arg(m_calculation->iterations()).arg(changeText(change));
    if(m_iterating) {
        text += QStringLiteral("...");
    } else if(converged) {
        text += QStringLiteral(", converged");
    } else {
        text += QStringLiteral(", not converged");
    }
    m_iterationLabel->setText(text);
}

double DepthProfileView::toDisplay(double depth) const {
    switch(m_unit) {
        case DepthUnit::ThinFilm:
            return depth / Units::ThinFilmUnit;
        case DepthUnit::Mass:
            return m_calculation->massDepth(depth) / Units::MicrogramPerSquareCentimetre;
        case DepthUnit::Length:
            return m_calculation->massDepth(depth) / m_profile->density / Nanometre;
    }
    return depth;
}

double DepthProfileView::fromDisplay(double value) const {
    if(m_unit == DepthUnit::ThinFilm) {
        return value * Units::ThinFilmUnit;
    }
    // The mass depth increases with depth, bisection finds the depth
    double low = -MaxDepthSpin * Units::ThinFilmUnit;
    double high = MaxDepthSpin * Units::ThinFilmUnit;
    for(int i = 0; i < 100; i++) {
        const double middle = (low + high) / 2;
        (toDisplay(middle) < value ? low : high) = middle;
    }
    return (low + high) / 2;
}

QString DepthProfileView::depthUnitText() const {
    switch(m_unit) {
        case DepthUnit::ThinFilm:
            return ThinFilmText;
        case DepthUnit::Mass:
            return QStringLiteral("µg/cm²");
        case DepthUnit::Length:
            return QStringLiteral("nm");
    }
    return {};
}

void DepthProfileView::updateProfile(SpectrumPlotWidget::ViewChange viewChange) {
    if(!m_plot) {
        return;
    }
    updateIntervalSpins();
    SpectrumPlotWidget::XAxis axis;
    axis.title = QString("Depth (%1)").arg(depthUnitText());
    axis.unit = depthUnitText();
    axis.decimals = m_unit == DepthUnit::ThinFilm ? 1 : 2;
    m_plot->setXAxis(axis);
    m_markerIntervals.clear();
    if(!m_calculation || !m_calculation->isValid() || m_calculation->iterations() == 0) {
        m_plot->setBinEdges({});
        m_plot->setCurves(1, {}, viewChange);
        m_plot->setRegions({});
        m_plot->setMarkers({});
        m_plot->setInfoText({});
        updateAverages();
        return;
    }
    m_result = binDepthProfile(*m_calculation, *m_profile);
    updateIntervalSpins();
    if(m_profile->autoMaxDepth) {
        setValueSilently(m_maxDepthSpin, m_calculation->maxDepth() / Units::ThinFilmUnit);
    }
    m_errorLabel->setText(m_result.errorMessage);
    m_errorLabel->setVisible(!m_result.errorMessage.isEmpty());
    if(!m_result.errorMessage.isEmpty()) {
        m_plot->setCurves(1, {}, viewChange);
        updateAverages();
        return;
    }

    std::vector<double> edges;
    for(size_t i = 0; i <= m_result.bins(); i++) {
        edges.push_back(toDisplay(m_result.binStart(i)));
    }
    QList<SpectrumPlotWidget::Curve> curves;
    size_t events = 0;
    for(const DepthProfileCurve &curve : m_result.curves) {
        events += curve.events;
        if(m_profile->cutIdsHiddenFromPlot.contains(curve.cutId)) {
            continue;
        }
        const Cut *cut = m_measurement->cut(curve.cutId);
        SpectrumPlotWidget::Curve plotCurve{cutLabel(curve.z, curve.a, curve.type), cut ? cut->color : QColor(Qt::gray),
                                            curve.events, {}};
        for(const double concentration : curve.concentrations) {
            plotCurve.values.push_back(concentration * 100);
        }
        curves.append(plotCurve);
    }
    if(m_profile->showTotal) {
        SpectrumPlotWidget::Curve total{QStringLiteral("Total"), Qt::black, events, {}, Qt::DashLine};
        for(const double value : m_result.totals) {
            total.values.push_back(value * 100);
        }
        curves.append(total);
    }
    m_plot->setBinEdges(edges);
    m_plot->setCurves(1, curves, viewChange);

    QColor normalizationFill = NormalizationColor;
    normalizationFill.setAlpha(30);
    QColor averageFill = AverageColor;
    averageFill.setAlpha(30);
    // Both intervals can be dragged, dragging the normalization interval makes it manual
    const double averageMin = toDisplay(m_profile->averageMin);
    const double averageMax = toDisplay(m_profile->averageMax);
    QList<SpectrumPlotWidget::Region> regions{{averageMin, averageMax, averageFill}};
    QList<SpectrumPlotWidget::Marker> markers{{averageMin, AverageColor, Qt::DashLine, true},
                                              {averageMax, AverageColor, Qt::DashLine, true}};
    m_markerIntervals = {Interval::Average, Interval::Average};
    if(m_profile->showNormalization) {
        const double normalizationMin = toDisplay(m_result.normalizationMin);
        const double normalizationMax = toDisplay(m_result.normalizationMax);
        regions.prepend({normalizationMin, normalizationMax, normalizationFill});
        markers.append({normalizationMin, NormalizationColor, Qt::DashLine, true});
        markers.append({normalizationMax, NormalizationColor, Qt::DashLine, true});
        m_markerIntervals.append({Interval::Normalization, Interval::Normalization});
    }
    m_plot->setRegions(regions);
    m_plot->setMarkers(markers);
    m_plot->setInfoText(QString("Bin %1 %2, averages over the orange interval%3")
                                .arg(m_profile->binWidth / Units::ThinFilmUnit, 0, 'g', 6).arg(ThinFilmText)
                                .arg(m_profile->showNormalization ? QStringLiteral(", total 100 % over the blue one") : QString()));
    updateAverages();
}

void DepthProfileView::updateAverages() {
    const QSignalBlocker blocker(m_averageTable);
    m_averageTable->clear();
    if(m_result.curves.empty() || !m_result.errorMessage.isEmpty()) {
        return;
    }
    double sum = 0;
    double renormalizedSum = 0;
    size_t events = 0;
    for(size_t c = 0; c < m_result.curves.size(); c++) {
        const DepthProfileCurve &curve = m_result.curves[c];
        const DepthProfileResult::Average &average = m_result.averages[c];
        auto *item = new QTreeWidgetItem(m_averageTable);
        item->setText(AverageSelectionColumn, cutLabel(curve.z, curve.a, curve.type));
        if(const Cut *cut = m_measurement->cut(curve.cutId)) {
            item->setIcon(AverageSelectionColumn, colorSwatch(cut->color));
        }
        item->setData(AverageSelectionColumn, CutIdRole, curve.cutId);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        const bool included = !m_profile->cutIdsExcludedFromAverage.contains(curve.cutId);
        item->setCheckState(AverageSelectionColumn, included ? Qt::Checked : Qt::Unchecked);
        item->setText(AverageColumn, percent(average.concentration));
        item->setText(UncertaintyColumn, percent(average.uncertainty));
        item->setText(RenormalizedColumn, percent(average.renormalized));
        item->setText(RenormalizedUncertaintyColumn, percent(average.renormalizedUncertainty));
        item->setText(EventsColumn, QString::number(average.counts));
        for(const int column : {AverageColumn, UncertaintyColumn, RenormalizedColumn, RenormalizedUncertaintyColumn, EventsColumn}) {
            item->setTextAlignment(column, Qt::AlignRight | Qt::AlignVCenter);
        }
        sum += average.concentration;
        if(included) {
            renormalizedSum += average.renormalized;
        }
        events += average.counts;
    }
    auto *total = new QTreeWidgetItem(m_averageTable);
    total->setText(AverageSelectionColumn, "Sum");
    total->setText(AverageColumn, percent(sum));
    total->setText(RenormalizedColumn, percent(renormalizedSum));
    total->setText(EventsColumn, QString::number(events));
    QFont font = total->font(0);
    font.setBold(true);
    for(const int column : {AverageSelectionColumn, AverageColumn, UncertaintyColumn, RenormalizedColumn,
                            RenormalizedUncertaintyColumn, EventsColumn}) {
        total->setFont(column, font);
        if(column != AverageSelectionColumn) {
            total->setTextAlignment(column, Qt::AlignRight | Qt::AlignVCenter);
        }
    }
}

void DepthProfileView::markerMoved(int index, double x) {
    if(index < 0 || index >= m_markerIntervals.size()) {
        return;
    }
    const Interval interval = m_markerIntervals[index];
    const bool isMin = index % 2 == 0; // Markers come in pairs, the lower end first
    const double depth = fromDisplay(x);
    if(interval == Interval::Normalization) {
        setAutoNormalization(false);
        setInterval(interval, isMin ? depth : m_profile->normalizationMin, isMin ? m_profile->normalizationMax : depth);
    } else {
        setInterval(interval, isMin ? depth : m_profile->averageMin, isMin ? m_profile->averageMax : depth);
    }
}

void DepthProfileView::checkStale() {
    if(!m_plot) {
        return;
    }
    // Selections may have been added, removed or recolored, which only needs the list and the curves updated
    updateSelectionList();
    updateProfile(SpectrumPlotWidget::ViewChange::Keep);
    setStale(depthProfileInputs(*m_measurement, *m_profile) != m_calculatedInputs);
}

void DepthProfileView::setStale(bool stale) {
    m_plot->setStale(stale);
    m_staleBar->setVisible(stale);
    if(stale != m_stale) {
        m_stale = stale;
        emit titleChanged();
    }
}

void DepthProfileView::exportProfile() {
    if(!m_plot) {
        return;
    }
    if(!m_calculation || m_result.curves.empty() || !m_result.errorMessage.isEmpty()) {
        QMessageBox::warning(this, "Export Depth Profile", "There is no depth profile to export.");
        return;
    }
    const QString fileName = QFileDialog::getSaveFileName(this, "Export Depth Profile",
                                                          QString("%1 %2.txt").arg(m_measurement->name, m_profile->name),
                                                          "Text files (*.txt);;All files (*)");
    if(fileName.isEmpty()) {
        return;
    }
    // Numbers are written without the locale, so that other programs can read them
    QString text;
    QTextStream out(&text);
    auto number = [](double value, int precision = 6) { return QString::number(value, 'g', precision); };
    out << "# JaRA depth profile\n";
    out << "# Measurement: " << m_measurement->name << " (" << m_measurement->path << ")\n";
    out << "# Profile: " << m_profile->name << "\n";
    out << "# Incident ion " << Elements::label(m_calculatedInputs.beamZ, m_calculatedInputs.beamA) << ", "
        << number(m_calculatedInputs.beamEnergy / Units::Megaelectronvolt) << " MeV, incident angle "
        << number(m_calculatedInputs.incidentAngle / Units::Degree) << " deg, scattering angle "
        << number(m_calculatedInputs.scatteringAngle / Units::Degree) << " deg from the surface\n";
    out << "# Single scattering, Rutherford cross sections with the Andersen correction, iteration "
        << m_calculation->iterations() << ", " << changeText(m_calculation->lastChange()) << " in the last one\n";
    out << "# Stopping bins " << number(m_profile->stoppingBinWidth / Units::ThinFilmUnit) << " 1e15 at/cm2, "
        << m_profile->minStoppingBinEvents << " smoothing events, density " << number(m_profile->density / GramPerCubicCentimetre)
        << " g/cm3 for nm, maximum depth " << number(m_calculation->maxDepth() / Units::ThinFilmUnit) << " 1e15 at/cm2\n";
    out << "# Total 100 % on average over " << number(m_result.normalizationMin / Units::ThinFilmUnit) << "-"
        << number(m_result.normalizationMax / Units::ThinFilmUnit) << " 1e15 at/cm2\n";
    if(m_stale) {
        out << "# Out of date: the selections or the settings have changed since the profile was calculated\n";
    }
    out << "# Averages over " << number(m_profile->averageMin / Units::ThinFilmUnit) << "-"
        << number(m_profile->averageMax / Units::ThinFilmUnit) << " 1e15 at/cm2 (at.% +- uncertainty, renormalized at.% +- "
           "uncertainty, statistical, including the normalization):";
    for(size_t c = 0; c < m_result.curves.size(); c++) {
        const DepthProfileCurve &curve = m_result.curves[c];
        const DepthProfileResult::Average &average = m_result.averages[c];
        out << " " << cutLabel(curve.z, curve.a, curve.type, false) << " " << percent(average.concentration) << " +- "
            << percent(average.uncertainty) << " " << (std::isnan(average.renormalized) ? QStringLiteral("-")
                                                       : percent(average.renormalized) + " +- " + percent(average.renormalizedUncertainty));
    }
    out << "\n# Depth (1e15 at/cm2, bin centre)\tDepth (ug/cm2)\tDepth (nm)";
    for(const DepthProfileCurve &curve : m_result.curves) {
        const QString label = cutLabel(curve.z, curve.a, curve.type, false);
        out << "\t" << label << " (at.%)\t" << label << " uncertainty\t" << label << " events";
    }
    out << "\tTotal (at.%)\n";
    for(size_t i = 0; i < m_result.bins(); i++) {
        const double depth = m_result.binStart(i) + m_result.binWidth / 2;
        const double mass = m_calculation->massDepth(depth);
        out << number(depth / Units::ThinFilmUnit, 8) << "\t" << number(mass / Units::MicrogramPerSquareCentimetre) << "\t"
            << number(mass / m_profile->density / Nanometre);
        for(const DepthProfileCurve &curve : m_result.curves) {
            out << "\t" << percent(curve.concentrations[i]) << "\t" << percent(curve.uncertainties[i]) << "\t"
                << curve.counts[i];
        }
        out << "\t" << percent(m_result.totals[i]) << "\n";
    }
    QSaveFile file(fileName);
    if(!file.open(QIODevice::WriteOnly) || file.write(text.toUtf8()) < 0 || !file.commit()) {
        QMessageBox::critical(this, "Export Depth Profile", QString("Could not write %1:\n%2").arg(fileName, file.errorString()));
        return;
    }
    m_statusLabel->setText(QString("Exported %1").arg(fileName));
}
