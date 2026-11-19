#include "depthprofileview.h"
#include "rangewidget.h"
#include "tableexport.h"
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
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
const QString ThinFilmText = QStringLiteral("10¹⁵ at/cm²");
constexpr double Nanometre = 1e-9;
constexpr double GramPerCubicCentimetre = 1000; // kg/m³
constexpr int CutIdRole = Qt::UserRole;
constexpr int CutIdsRole = Qt::UserRole + 1; // Of the selections summed in a row of the averages
constexpr int ConcentrationDecimals = 2; // at.%
constexpr int ExportConcentrationDecimals = 4; // at.%

constexpr double MaxDepthSpin = 1e6; // 1e15 at/cm²
const QColor NormalizationColor(0x1f, 0x77, 0xb4);
const QColor AverageColor(0xff, 0x7f, 0x0e);

// The selection column has a check box for showing the selection in the plot, the renormalized one for including it in
// the renormalized averages
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

// In at.%, the uncertainty rounded to one significant digit, or two when they are 15 or less, and the value to the same
// precision
QString percentWithUncertainty(double fraction, double uncertainty) {
    const double value = fraction * 100;
    const double deviation = uncertainty * 100;
    if(!std::isfinite(deviation) || deviation <= 0) {
        return QString("%1 at.%").arg(percent(fraction));
    }
    const int exponent = static_cast<int>(std::floor(std::log10(deviation)));
    const bool twoDigits = std::round(deviation / std::pow(10.0, exponent - 1)) <= 15;
    const int decimals = (twoDigits ? 1 : 0) - exponent;
    const double step = std::pow(10.0, -decimals);
    const auto rounded = [&](double x) { return QString::number(std::round(x / step) * step, 'f', std::max(decimals, 0)); };
    return QString("%1 ± %2 at.%").arg(rounded(value), rounded(deviation));
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
    m_viewPending = m_profile->view.has_value();
    m_viewSaveTimer = new QTimer(this);
    m_viewSaveTimer->setSingleShot(true);
    m_viewSaveTimer->setInterval(500);
    connect(m_viewSaveTimer, &QTimer::timeout, this, &DepthProfileView::modified);
    connect(m_plot, &SpectrumPlotWidget::viewChangedByUser, this, &DepthProfileView::storeView);
    // The legend is dragged to a corner, until then it is where the preferences put it
    connect(m_plot, &SpectrumPlotWidget::legendCornerChanged, this, [this](Qt::Corner corner) {
        m_profile->legendPosition = legendPosition(corner);
        emit modified();
    });
    auto *concentrations = new QAction("Concentrations in the Legend", this);
    concentrations->setCheckable(true);
    concentrations->setChecked(m_profile->showLegendConcentrations);
    concentrations->setToolTip("The average concentrations after the names, renormalized unless averaged over the "
                               "normalization interval");
    connect(concentrations, &QAction::toggled, this, [this](bool checked) {
        m_profile->showLegendConcentrations = checked;
        emit modified();
        updateProfile(SpectrumPlotWidget::ViewChange::Keep);
    });
    auto *shadeAverage = new QAction("Shade Averaging Interval", this);
    shadeAverage->setCheckable(true);
    shadeAverage->setChecked(m_profile->shadeAverage);
    shadeAverage->setToolTip("The ends of the interval are drawn either way");
    connect(shadeAverage, &QAction::toggled, this, [this](bool checked) {
        m_profile->shadeAverage = checked;
        emit modified();
        updateProfile(SpectrumPlotWidget::ViewChange::Keep);
    });
    m_labelMenu = new QMenu("Labels", this);
    connect(m_labelMenu, &QMenu::aboutToShow, this, &DepthProfileView::updateLabelMenu);
    connect(m_plot, &SpectrumPlotWidget::labelMoved, this, &DepthProfileView::labelMoved);
    m_plot->setContextActions({concentrations, m_labelMenu->menuAction(), shadeAverage});
    updateLegendPosition();
    m_statusLabel = new QLabel;
    m_statusLabel->setMinimumHeight(m_statusLabel->fontMetrics().height()); // Cursor info, or the hint of the mode
    m_statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); // Long texts do not move anything
    m_errorLabel = new QLabel;
    m_errorLabel->setStyleSheet("color: red");
    m_errorLabel->setWordWrap(true);
    m_errorLabel->hide();
    m_warningLabel = new QLabel;
    m_warningLabel->setStyleSheet("color: #b35900");
    m_warningLabel->setWordWrap(true);
    m_warningLabel->hide();

    auto *splitter = new QSplitter;
    splitter->addWidget(m_plot);
    splitter->addWidget(createSidePanel());
    splitter->setStretchFactor(0, 1);
    splitter->setSizes({850, 460});

    layout->addWidget(createToolBar());
    layout->addWidget(createStaleBar());
    layout->addWidget(m_errorLabel);
    layout->addWidget(m_warningLabel);
    layout->addWidget(splitter, 1);
    auto *statusRow = new QHBoxLayout;
    statusRow->addWidget(m_statusLabel, 1);
    statusRow->addWidget(m_iterationLabel);
    layout->addLayout(statusRow);

    connect(m_plot, &SpectrumPlotWidget::cursorInfoChanged, this, [this](const QString &text) {
        m_statusLabel->setText(text.isEmpty() ? m_hint : text);
    });
    updateSelectionSummary();
    updateCutRanges();
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
    QAction *zoomMode = addMode("Zoom Box", SpectrumPlotWidget::Mode::Zoom, {});
    zoomMode->setChecked(true);
    zoomMode->setToolTip("Drag to zoom, double-click to show everything, drag the dashed lines to change the intervals");
    addMode("Move", SpectrumPlotWidget::Mode::Move, {})->setToolTip("Drag to move the view");
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
    addToggle("Isotopes", "Show the isotopes of an element separately in the profile and the averages instead of "
              "their sum", &DepthProfile::separateIsotopes);
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
    auto *label = new QLabel("Out of date.");
    label->setToolTip("The selections, the selection that divides the measurement or the experimental, detector or "
                      "physics settings have changed since the profile was calculated.");
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
    m_selectionSummary = new QLabel;
    auto *chooseButton = new QPushButton("Choose...");
    chooseButton->setToolTip("Choose the selections included in the calculation");
    connect(chooseButton, &QPushButton::clicked, this, &DepthProfileView::chooseSelections);
    auto *summaryRow = new QHBoxLayout;
    summaryRow->addWidget(m_selectionSummary, 1);
    summaryRow->addWidget(chooseButton);
    auto *selectionGroup = new QGroupBox("Selections");
    auto *selectionLayout = new QVBoxLayout(selectionGroup);
    selectionLayout->setContentsMargins(6, 6, 6, 6);
    selectionLayout->addLayout(summaryRow);
    m_rangeWidget = new MeasurementRangeWidget(&m_events, m_measurement);
    m_rangeWidget->setRange(m_profile->rangeMin, m_profile->rangeMax);
    m_cutRangesButton = new QPushButton("Per Selection...");
    m_cutRangesButton->setToolTip("Narrower parts for some selections, e.g. hydrogen only from the beginning");
    connect(m_cutRangesButton, &QPushButton::clicked, this, &DepthProfileView::showCutRanges);
    auto *rangeRow = new QHBoxLayout;
    rangeRow->addWidget(new QLabel("Part of the measurement:"), 1);
    rangeRow->addWidget(m_cutRangesButton);
    selectionLayout->addLayout(rangeRow);
    selectionLayout->addWidget(m_rangeWidget);
    connect(m_rangeWidget, &MeasurementRangeWidget::rangeEdited, this, [this](double min, double max) {
        m_profile->rangeMin = min;
        m_profile->rangeMax = max;
        emit modified();
        recalculate();
    });

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
    m_followNormalizationCheck = new QCheckBox("Normalization");
    m_followNormalizationCheck->setChecked(m_profile->averageFollowsNormalization);
    m_followNormalizationCheck->setToolTip("Average over the normalization interval, where the total is 100 % on "
                                           "average, so no renormalization is needed");
    connect(m_followNormalizationCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if(!checked) {
            // Starts from the interval followed
            m_profile->averageMin = m_result.averageMin;
            m_profile->averageMax = m_result.averageMax;
        }
        m_profile->averageFollowsNormalization = checked;
        emit modified();
        updateProfile(SpectrumPlotWidget::ViewChange::Keep);
    });
    auto *averageRow = new QHBoxLayout;
    averageRow->addWidget(new QLabel(QString("Over (%1):").arg(ThinFilmText)));
    averageRow->addWidget(m_followNormalizationCheck);
    averageRow->addWidget(m_averageMinSpin, 1);
    averageRow->addWidget(new QLabel("–"));
    averageRow->addWidget(m_averageMaxSpin, 1);
    m_averageTable = new QTreeWidget;
    m_averageTable->setRootIsDecorated(false);
    m_averageTable->setHeaderLabels({"Selection", "at.%", "±", "Renorm. at.%", "±", "Events"});
    m_averageTable->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_averageTable->headerItem()->setToolTip(AverageSelectionColumn, "Checked selections are shown in the plot");
    m_averageTable->headerItem()->setToolTip(RenormalizedColumn, "Averages renormalized to 100 % among the checked "
                                                                 "selections, e.g. leaving out the substrate");
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

void DepthProfileView::updateSelectionSummary() {
    int included = 0;
    int all = 0;
    for(const Cut &cut : m_measurement->cuts) {
        if(isAnalyzed(cut)) {
            all++;
            included += m_profile->cutIds.contains(cut.id) ? 1 : 0;
        }
    }
    m_selectionSummary->setText(QString("%1/%2 selections included").arg(included).arg(all));
}

void DepthProfileView::showCutRanges() {
    if(!m_cutRangesDialog) {
        m_cutRangesDialog = new QDialog(this); // Not modal, the profile can be worked with meanwhile
        auto *layout = new QVBoxLayout(m_cutRangesDialog);
        auto *intro = new QLabel("Parts of the profile's part of the measurement. Events are weighted to the whole part.");
        intro->setWordWrap(true);
        layout->addWidget(intro);
        m_cutRangesGrid = new QGridLayout;
        layout->addLayout(m_cutRangesGrid);
        layout->addStretch();
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
        connect(buttons, &QDialogButtonBox::rejected, m_cutRangesDialog, &QDialog::close);
        layout->addWidget(buttons);
        m_cutRangesDialog->resize(520, m_cutRangesDialog->sizeHint().height());
    }
    m_cutRangesDialog->show(); // Before updating, which skips a hidden dialog
    updateCutRanges();
    m_cutRangesDialog->raise();
    m_cutRangesDialog->activateWindow();
}

void DepthProfileView::updateCutRanges() {
    QList<const Cut *> cuts; // Included in the profile
    QStringList key;
    int own = 0;
    for(const Cut &cut : m_measurement->cuts) {
        if(isAnalyzed(cut) && m_profile->cutIds.contains(cut.id)) {
            cuts.append(&cut);
            key.append(QString("%1 %2 %3").arg(cut.id).arg(cutLabel(cut), cut.color.name()));
            own += m_profile->cutRanges.contains(cut.id) ? 1 : 0;
        }
    }
    m_cutRangesButton->setText(own > 0 ? QString("Per Selection (%1)...").arg(own) : QStringLiteral("Per Selection..."));
    if(!m_cutRangesDialog || !m_cutRangesDialog->isVisible()) {
        return; // Updated when shown
    }
    m_cutRangesDialog->setWindowTitle(QString("Parts of Selections — %1").arg(m_profile->name));
    // Rebuilt only when the selections change, not while the user works with the rows
    if(key != m_cutRangesKey) {
        m_cutRangesKey = key;
        while(QLayoutItem *item = m_cutRangesGrid->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        m_cutRangeChecks.clear();
        m_cutRangeWidgets.clear();
        int row = 0;
        for(const Cut *cut : cuts) {
            const int id = cut->id;
            auto *check = new QCheckBox(cutLabel(*cut));
            check->setIcon(colorSwatch(cut->color));
            check->setToolTip("Use a part of the profile's part of the measurement for this selection");
            auto *range = new MeasurementRangeWidget(&m_events, m_measurement);
            m_cutRangesGrid->addWidget(check, row, 0, Qt::AlignTop);
            m_cutRangesGrid->addWidget(range, row, 1);
            m_cutRangeChecks.insert(id, check);
            m_cutRangeWidgets.insert(id, range);
            row++;
            // Not in the handlers: the profile is modified, which updates this dialog
            connect(check, &QCheckBox::toggled, this, [this, id, range](bool checked) {
                range->setEnabled(checked);
                const std::pair<double, double> part{range->min(), range->max()};
                QTimer::singleShot(0, this, [this, id, checked, part]() {
                    if(checked) {
                        m_profile->cutRanges.insert(id, part);
                    } else {
                        m_profile->cutRanges.remove(id);
                    }
                    emit modified();
                    recalculate();
                });
            });
            connect(range, &MeasurementRangeWidget::rangeEdited, this, [this, id](double min, double max) {
                QTimer::singleShot(0, this, [this, id, min, max]() {
                    m_profile->cutRanges.insert(id, {min, max});
                    emit modified();
                    recalculate();
                });
            });
        }
    }
    for(auto i = m_cutRangeWidgets.begin(); i != m_cutRangeWidgets.end(); ++i) {
        const auto part = m_profile->cutRanges.find(i.key());
        const bool own = part != m_profile->cutRanges.end();
        QCheckBox *check = m_cutRangeChecks.value(i.key());
        const QSignalBlocker blocker(check);
        check->setChecked(own);
        i.value()->setEnabled(own);
        i.value()->setOuterRange(m_profile->rangeMin, m_profile->rangeMax);
        i.value()->setRange(own ? part->first : 0, own ? part->second : 1);
        i.value()->refresh();
    }
}

void DepthProfileView::chooseSelections() {
    QDialog dialog(this);
    dialog.setWindowTitle("Selections");
    auto *list = new QListWidget;
    for(const Cut &cut : m_measurement->cuts) {
        if(!isAnalyzed(cut)) {
            continue;
        }
        auto *item = new QListWidgetItem(colorSwatch(cut.color), cutLabel(cut), list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(m_profile->cutIds.contains(cut.id) ? Qt::Checked : Qt::Unchecked);
        item->setData(CutIdRole, cut.id);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto setAll = [list](Qt::CheckState state) {
        for(int i = 0; i < list->count(); i++) {
            list->item(i)->setCheckState(state);
        }
    };
    connect(buttons->addButton("All", QDialogButtonBox::ActionRole), &QPushButton::clicked, &dialog,
            [setAll]() { setAll(Qt::Checked); });
    connect(buttons->addButton("None", QDialogButtonBox::ActionRole), &QPushButton::clicked, &dialog,
            [setAll]() { setAll(Qt::Unchecked); });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel("Selections included in the calculation:"));
    layout->addWidget(list);
    layout->addWidget(buttons);
    if(dialog.exec() != QDialog::Accepted) {
        return;
    }
    QList<int> cutIds;
    for(int i = 0; i < list->count(); i++) {
        if(list->item(i)->checkState() == Qt::Checked) {
            cutIds.append(list->item(i)->data(CutIdRole).toInt());
        }
    }
    if(cutIds == m_profile->cutIds) {
        return;
    }
    m_profile->cutIds = cutIds;
    updateSelectionSummary();
    emit modified();
    recalculate();
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
    // The automatic interval is shown when there is one. The intervals are snapped to the bins, as they are used.
    const bool automatic = m_profile->autoNormalization && m_result.normalizationMax > m_result.normalizationMin;
    double normalizationMin = automatic ? m_result.normalizationMin : m_profile->normalizationMin;
    double normalizationMax = automatic ? m_result.normalizationMax : m_profile->normalizationMax;
    snapToBins(normalizationMin, normalizationMax, m_profile->binWidth);
    setValueSilently(m_normalizationMinSpin, normalizationMin / Units::ThinFilmUnit);
    setValueSilently(m_normalizationMaxSpin, normalizationMax / Units::ThinFilmUnit);
    m_normalizationMinSpin->setEnabled(!m_profile->autoNormalization);
    m_normalizationMaxSpin->setEnabled(!m_profile->autoNormalization);
    // The normalization interval when the averages follow it
    const bool follows = m_profile->averageFollowsNormalization;
    double averageMin = follows ? normalizationMin : m_profile->averageMin;
    double averageMax = follows ? normalizationMax : m_profile->averageMax;
    snapToBins(averageMin, averageMax, m_profile->binWidth);
    setValueSilently(m_averageMinSpin, averageMin / Units::ThinFilmUnit);
    setValueSilently(m_averageMaxSpin, averageMax / Units::ThinFilmUnit);
    m_averageMinSpin->setEnabled(!follows);
    m_averageMaxSpin->setEnabled(!follows);
}

void DepthProfileView::setInterval(Interval interval, double min, double max) {
    snapToBins(min, max, m_profile->binWidth);
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

void DepthProfileView::averageItemChanged(QTreeWidgetItem *item, int column) {
    if((column != AverageSelectionColumn && column != RenormalizedColumn)
       || !item->data(AverageSelectionColumn, CutIdsRole).isValid()) {
        return;
    }
    // All of the selections summed in the row
    QList<int> &ids = column == AverageSelectionColumn ? m_profile->cutIdsHiddenFromPlot : m_profile->cutIdsExcludedFromAverage;
    for(const int id : item->data(AverageSelectionColumn, CutIdsRole).value<QList<int>>()) {
        ids.removeAll(id);
        if(item->checkState(column) != Qt::Checked) {
            ids.append(id);
        }
    }
    // Not in the handler of the table: updating the profile, also after the project has been modified, rebuilds the
    // table and deletes the item being changed
    QTimer::singleShot(0, this, [this]() {
        emit modified();
        updateProfile(SpectrumPlotWidget::ViewChange::Keep);
    });
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
    const QString warning = elementOverlapWarning(m_calculatedInputs);
    m_warningLabel->setText(warning);
    m_warningLabel->setVisible(!warning.isEmpty());
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

void DepthProfileView::storeView() {
    if(m_applyingView || !m_calculation || !m_calculation->isValid()) {
        return;
    }
    m_viewPending = false; // The user's zoom from now on
    double xMin = 0;
    double xMax = 0;
    double yMin = 0;
    double yMax = 0;
    m_plot->viewRange(xMin, xMax, yMin, yMax);
    m_profile->view = PlotView{fromDisplay(xMin), fromDisplay(xMax), yMin / 100, yMax / 100, m_plot->logScale()};
    m_viewSaveTimer->start();
}

void DepthProfileView::applyStoredView() {
    if(!m_viewPending || !m_profile->view) {
        return;
    }
    const PlotView &view = *m_profile->view;
    m_applyingView = true;
    m_plot->setLogScale(view.logScale);
    m_plot->showRange(toDisplay(view.depthMin), toDisplay(view.depthMax), view.concentrationMin * 100,
                      view.concentrationMax * 100);
    m_applyingView = false;
    // Kept until the iterations end, the last one scales the plot to the profile
    m_viewPending = m_iterating;
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
        m_plot->setLabels({});
        m_labelCutIds.clear();
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
        m_plot->setLabels({});
        m_labelCutIds.clear();
        updateAverages();
        return;
    }

    std::vector<double> edges;
    for(size_t i = 0; i <= m_result.bins(); i++) {
        edges.push_back(toDisplay(m_result.binStart(i)));
    }
    QList<SpectrumPlotWidget::Curve> curves;
    size_t events = 0;
    for(size_t c = 0; c < m_result.curves.size(); c++) {
        const DepthProfileCurve &curve = m_result.curves[c];
        events += curve.events;
        if(curve.allIn(m_profile->cutIdsHiddenFromPlot)) {
            continue;
        }
        const Cut *cut = m_measurement->cut(curve.cutId);
        SpectrumPlotWidget::Curve plotCurve{curve.label(), cut ? cut->color : QColor(Qt::gray),
                                            curve.events, {}};
        const DepthProfileResult::Average &average = m_result.averages[c];
        // Over the normalization interval the averages need no renormalization. Otherwise left out selections have
        // no renormalized average.
        if(m_profile->showLegendConcentrations && m_profile->averageFollowsNormalization) {
            plotCurve.legendSuffix = QString(": %1").arg(percentWithUncertainty(average.concentration, average.uncertainty));
        } else if(m_profile->showLegendConcentrations && !std::isnan(average.renormalized)) {
            plotCurve.legendSuffix = QString(": %1").arg(percentWithUncertainty(average.renormalized,
                                                                                average.renormalizedUncertainty));
        }
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
    applyStoredView();

    QColor normalizationFill = NormalizationColor;
    normalizationFill.setAlpha(30);
    QColor averageFill = AverageColor;
    averageFill.setAlpha(30);
    // Both intervals can be dragged, dragging the normalization interval makes it manual. The ends of the averaging
    // interval are always drawn, the shading is optional. When the averages follow the normalization interval, the
    // averaging interval stands for both.
    const bool follows = m_profile->averageFollowsNormalization;
    const double averageMin = toDisplay(m_result.averageMin);
    const double averageMax = toDisplay(m_result.averageMax);
    QList<SpectrumPlotWidget::Region> regions;
    if(m_profile->shadeAverage) {
        regions.append({averageMin, averageMax, averageFill});
    }
    QList<SpectrumPlotWidget::Marker> markers{{averageMin, AverageColor, Qt::DashLine, true},
                                              {averageMax, AverageColor, Qt::DashLine, true}};
    const Interval averageInterval = follows ? Interval::Normalization : Interval::Average;
    m_markerIntervals = {averageInterval, averageInterval};
    if(m_profile->showNormalization) {
        const double normalizationMin = toDisplay(m_result.normalizationMin);
        const double normalizationMax = toDisplay(m_result.normalizationMax);
        regions.prepend({normalizationMin, normalizationMax, normalizationFill});
        if(!follows) {
            markers.append({normalizationMin, NormalizationColor, Qt::DashLine, true});
            markers.append({normalizationMax, NormalizationColor, Qt::DashLine, true});
            m_markerIntervals.append({Interval::Normalization, Interval::Normalization});
        }
    }
    m_plot->setRegions(regions);
    m_plot->setMarkers(markers);
    // Names of the shown selections in the middle of the averaging interval at their averages, which are
    // unrenormalized as the curves are
    QList<SpectrumPlotWidget::Label> labels;
    m_labelCutIds.clear();
    if(m_profile->showLabels) {
        for(size_t c = 0; c < m_result.curves.size(); c++) {
            const DepthProfileCurve &curve = m_result.curves[c];
            if(curve.allIn(m_profile->cutIdsHiddenFromPlot) || curve.allIn(m_profile->cutIdsWithoutLabel)) {
                continue;
            }
            const Cut *cut = m_measurement->cut(curve.cutId);
            SpectrumPlotWidget::Label label{(averageMin + averageMax) / 2, m_result.averages[c].concentration * 100,
                                            curve.label(), cut ? cut->color : QColor(Qt::gray), std::nullopt};
            // Dragged by the user to a depth and concentration
            if(const auto position = m_profile->labelPositions.find(curve.cutId); position != m_profile->labelPositions.end()) {
                label.position = QPointF(toDisplay(position->x()), position->y() * 100);
            }
            labels.append(label);
            m_labelCutIds.append(curve.cutId);
        }
    }
    m_plot->setLabels(labels);
    m_plot->setInfoText(QString("Bin %1 %2").arg(m_profile->binWidth / Units::ThinFilmUnit, 0, 'g', 6).arg(ThinFilmText));
    updateAverages();
}

void DepthProfileView::applyPreferences() {
    if(m_plot) {
        updateLegendPosition();
    }
}

void DepthProfileView::updateLegendPosition() {
    m_plot->setLegendCorner(legendCorner(m_profile->legendPosition == LegendPosition::Default
                                                 ? Defaults::depthProfilePreferences().legendPosition
                                                 : m_profile->legendPosition));
}

void DepthProfileView::updateAverages() {
    const QSignalBlocker blocker(m_averageTable);
    m_averageTable->clear();
    // Over the normalization interval the averages are already normalized to 100 %
    for(const int column : {RenormalizedColumn, RenormalizedUncertaintyColumn}) {
        m_averageTable->setColumnHidden(column, m_profile->averageFollowsNormalization);
    }
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
        item->setText(AverageSelectionColumn, curve.label());
        if(const Cut *cut = m_measurement->cut(curve.cutId)) {
            item->setIcon(AverageSelectionColumn, colorSwatch(cut->color));
        }
        item->setData(AverageSelectionColumn, CutIdsRole, QVariant::fromValue(curve.cutIds));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        const bool plotted = !curve.allIn(m_profile->cutIdsHiddenFromPlot);
        item->setCheckState(AverageSelectionColumn, plotted ? Qt::Checked : Qt::Unchecked);
        const bool included = !curve.allIn(m_profile->cutIdsExcludedFromAverage);
        item->setCheckState(RenormalizedColumn, included ? Qt::Checked : Qt::Unchecked);
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

void DepthProfileView::labelMoved(int index, QPointF position) {
    if(index < 0 || index >= m_labelCutIds.size()) {
        return;
    }
    m_profile->labelPositions.insert(m_labelCutIds[index], QPointF(fromDisplay(position.x()), position.y() / 100));
    emit modified();
    updateProfile(SpectrumPlotWidget::ViewChange::Keep);
}

void DepthProfileView::updateLabelMenu() {
    m_labelMenu->clear();
    QAction *show = m_labelMenu->addAction("Show Labels");
    show->setCheckable(true);
    show->setChecked(m_profile->showLabels);
    show->setToolTip("Names of the selections in the plot at their averages, they can be dragged");
    connect(show, &QAction::toggled, this, [this](bool checked) {
        m_profile->showLabels = checked;
        emit modified();
        updateProfile(SpectrumPlotWidget::ViewChange::Keep);
    });
    m_labelMenu->addSeparator();
    // The shown curves, all of the selections summed in a curve share its label
    for(const DepthProfileCurve &curve : m_result.curves) {
        if(curve.allIn(m_profile->cutIdsHiddenFromPlot)) {
            continue;
        }
        QAction *action = m_labelMenu->addAction(curve.label());
        action->setCheckable(true);
        action->setChecked(!curve.allIn(m_profile->cutIdsWithoutLabel));
        action->setEnabled(m_profile->showLabels);
        const QList<int> ids = curve.cutIds;
        connect(action, &QAction::toggled, this, [this, ids](bool checked) {
            for(const int id : ids) {
                m_profile->cutIdsWithoutLabel.removeAll(id);
                if(!checked) {
                    m_profile->cutIdsWithoutLabel.append(id);
                }
            }
            emit modified();
            updateProfile(SpectrumPlotWidget::ViewChange::Keep);
        });
    }
    m_labelMenu->addSeparator();
    QAction *reset = m_labelMenu->addAction("Reset Label Positions", this, [this]() {
        m_profile->labelPositions.clear();
        emit modified();
        updateProfile(SpectrumPlotWidget::ViewChange::Keep);
    });
    reset->setEnabled(m_profile->showLabels && !m_profile->labelPositions.isEmpty());
}

void DepthProfileView::checkStale() {
    if(!m_plot) {
        return;
    }
    // Selections may have been added, removed or recolored, which only needs the summary and the curves updated
    updateSelectionSummary();
    m_rangeWidget->refresh();
    updateCutRanges();
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
    auto number = [](double value, int precision = 6) { return QString::number(value, 'g', precision); };
    // In at.% to 0.0001 at.%, so that trace amounts are kept, "nan" for none
    auto value = [](double fraction) {
        return std::isnan(fraction) ? QStringLiteral("nan") : QString::number(fraction * 100, 'f', ExportConcentrationDecimals);
    };
    const double thinFilm = Units::ThinFilmUnit;
    ExportTable table;
    table.comments.append(QString("JaRA depth profile: %1 / %2 (%3)").arg(m_measurement->name, m_profile->name,
                                                                        m_measurement->path));
    table.comments.append(QString("%1 %2 MeV, incident %3 deg, scattering %4 deg from the surface; screening %5; "
                                  "iteration %6, %7")
                                  .arg(Elements::label(m_calculatedInputs.beamZ, m_calculatedInputs.beamA),
                                       number(m_calculatedInputs.beamEnergy / Units::Megaelectronvolt),
                                       number(m_calculatedInputs.incidentAngle / Units::Degree),
                                       number(m_calculatedInputs.scatteringAngle / Units::Degree),
                                       screeningModelName(m_calculatedInputs.physics.screening))
                                  .arg(m_calculation->iterations())
                                  .arg(changeText(m_calculation->lastChange())));
    table.comments.append(QString("Depths in 1e15 at/cm2: stopping bins %1 with %2 smoothing events, maximum %3, 100 % "
                                  "over %4-%5, averages over %6-%7; density %8 g/cm3 for nm")
                                  .arg(number(m_profile->stoppingBinWidth / thinFilm))
                                  .arg(m_profile->minStoppingBinEvents)
                                  .arg(number(m_calculation->maxDepth() / thinFilm), number(m_result.normalizationMin / thinFilm),
                                       number(m_result.normalizationMax / thinFilm), number(m_result.averageMin / thinFilm),
                                       number(m_result.averageMax / thinFilm),
                                       number(m_profile->density / GramPerCubicCentimetre)));
    if(m_calculatedInputs.rangeMin > 0 || m_calculatedInputs.rangeMax < 1) {
        const Cut *reference = m_measurement->referenceCut();
        table.comments.append(QString("Events of %1-%2 % of the counts of %3")
                                      .arg(number(m_calculatedInputs.rangeMin * 100), number(m_calculatedInputs.rangeMax * 100),
                                           reference ? cutLabel(*reference, false) : QStringLiteral("all events")));
    }
    if(m_stale) {
        table.comments.append(QStringLiteral("Out of date"));
    }
    // Renormalized averages only when they differ from the averages
    QStringList averages;
    for(size_t c = 0; c < m_result.curves.size(); c++) {
        const DepthProfileResult::Average &average = m_result.averages[c];
        QString text = QString("%1 %2 +- %3").arg(m_result.curves[c].label(false), value(average.concentration),
                                                  value(average.uncertainty));
        if(!m_profile->averageFollowsNormalization && !std::isnan(average.renormalized)) {
            text += QString(" (renormalized %1 +- %2)").arg(value(average.renormalized), value(average.renormalizedUncertainty));
        }
        averages.append(text);
    }
    table.comments.append(QStringLiteral("Averages (at.%): ") + averages.join(QStringLiteral(", ")));
    // Edges and centres of the bins in 1e15 at/cm2, centres in the other units
    table.columns = QStringList{QStringLiteral("Depth min (1e15 at/cm2)"), QStringLiteral("Depth (1e15 at/cm2)"),
                                QStringLiteral("Depth max (1e15 at/cm2)"), QStringLiteral("Depth (ug/cm2)"),
                                QStringLiteral("Depth (nm)")};
    for(const DepthProfileCurve &curve : m_result.curves) {
        const QString label = curve.label(false);
        table.columns.append({label + QStringLiteral(" (at.%)"), label + QStringLiteral(" uncertainty"),
                              label + QStringLiteral(" events")});
    }
    table.columns.append(QStringLiteral("Total (at.%)"));
    for(size_t i = 0; i < m_result.bins(); i++) {
        const double depth = m_result.binStart(i) + m_result.binWidth / 2;
        const double mass = m_calculation->massDepth(depth);
        QStringList row{number(m_result.binStart(i) / thinFilm, 8), number(depth / thinFilm, 8),
                        number(m_result.binStart(i + 1) / thinFilm, 8), number(mass / Units::MicrogramPerSquareCentimetre),
                        number(mass / m_profile->density / Nanometre)};
        for(const DepthProfileCurve &curve : m_result.curves) {
            row.append({value(curve.concentrations[i]), value(curve.uncertainties[i]), QString::number(curve.counts[i])});
        }
        row.append(value(m_result.totals[i]));
        table.rows.append(row);
    }
    const QString fileName = exportTable(this, "Export Depth Profile",
                                         QString("%1 %2").arg(m_measurement->name, m_profile->name), table);
    if(!fileName.isEmpty()) {
        m_statusLabel->setText(QString("Exported %1").arg(fileName));
    }
}
