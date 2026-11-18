#include "tofcalibrationwindow.h"
#include "calibrationplotwidget.h"
#include "colorbutton.h"
#include "elements.h"
#include "project.h"
#include "spectrumplotwidget.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

#include <QActionGroup>
#include <QApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
const QString ZoomHint = QStringLiteral(
        "Drag to zoom, double-click to show everything. Drag the dashed lines to change the fit window.");
const QString MoveHint = QStringLiteral("Drag to move the view. Scroll the mouse wheel or pinch to zoom.");
const QString PickHint = QStringLiteral("Click to set the edge by hand. Drag the dashed lines to fit again in another window.");
constexpr int MeasurementRole = Qt::UserRole;
constexpr int CutIdRole = Qt::UserRole + 1;
constexpr double OverlayStep = 0.25; // Channels between the points of the fitted curve
constexpr double ViewMargin = 0.1; // Of the channels with events, shown on both sides of them
const QColor WindowColor(0x80, 0x80, 0x80);
const QColor EdgeColor(0xd6, 0x27, 0x28);
const QColor FitColor(0x1f, 0x77, 0xb4);

enum TreeColumn { SelectionColumn, EdgeColumn, ExpectedColumn };
enum PointColumn { PointSelectionColumn, PointChannelColumn, PointExpectedColumn, PointLineColumn, PointResidualColumn };

QString nameOrUnnamed(const QString &name) {
    return name.isEmpty() ? QStringLiteral("(unnamed)") : name;
}

QString edgeText(const ToFEdge &edge) {
    return edge.manual ? QString("%1 (by hand)").arg(edge.channel, 0, 'f', 1) : QString::number(edge.channel, 'f', 2);
}

// "28Si recoils" or "4He scattered from Si"
QString particleText(const ExperimentalSettings *settings, const Cut &cut) {
    if(cut.type == CutType::Recoil) {
        return QString("%1 recoils").arg(Elements::displayLabel(cut.z, cut.a));
    }
    const QString beam = settings && settings->beamZ > 0 ? Elements::displayLabel(settings->beamZ, settings->beamA)
                                                          : QStringLiteral("Incident ions");
    return QString("%1 scattered from %2").arg(beam, Elements::displayLabel(cut.z, cut.a));
}

Measurement *measurementOf(const QTreeWidgetItem *item) {
    return reinterpret_cast<Measurement *>(item->data(SelectionColumn, MeasurementRole).value<quintptr>());
}

void setMeasurement(QTreeWidgetItem *item, Measurement *measurement) {
    item->setData(SelectionColumn, MeasurementRole, QVariant::fromValue(reinterpret_cast<quintptr>(measurement)));
}
}

ToFCalibrationWindow::ToFCalibrationWindow(Project *project, DetectorSettings *detector, QWidget *parent)
    : QWidget(parent, Qt::Window), m_project(project), m_detector(detector) {
    m_tree = new QTreeWidget;
    m_tree->setHeaderLabels({"Selection", "Edge (ch)", "Expected ToF (ns)"});
    m_tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, &ToFCalibrationWindow::currentItemChanged);

    auto *right = new QSplitter(Qt::Vertical);
    right->addWidget(createSpectrumPanel());
    right->addWidget(createCalibrationPanel());
    right->setStretchFactor(0, 3);
    right->setStretchFactor(1, 2);

    auto *splitter = new QSplitter;
    splitter->addWidget(m_tree);
    splitter->addWidget(right);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({320, 900});

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(splitter);
    resize(1250, 850);
    refresh();
}

QWidget *ToFCalibrationWindow::createSpectrumPanel() {
    m_plot = new SpectrumPlotWidget;
    SpectrumPlotWidget::XAxis axis;
    axis.title = QStringLiteral("ToF (ch)");
    axis.unit = QStringLiteral("ch");
    axis.decimals = 1;
    axis.origin = -0.5; // Channels are integers, bins are centred on them
    axis.binWidthScale = 1;
    axis.binWidthUnit = QStringLiteral("ch");
    m_plot->setXAxis(axis);
    connect(m_plot, &SpectrumPlotWidget::markerMoved, this, &ToFCalibrationWindow::windowMarkerMoved);
    connect(m_plot, &SpectrumPlotWidget::picked, this, &ToFCalibrationWindow::edgePicked);

    auto *toolBar = new QToolBar;
    toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolBar->addAction("Zoom In", m_plot, &SpectrumPlotWidget::zoomIn);
    toolBar->addAction("Zoom Out", m_plot, &SpectrumPlotWidget::zoomOut);
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
    addMode("Set Edge", SpectrumPlotWidget::Mode::Pick, PickHint);
    toolBar->addSeparator();
    QAction *logScale = toolBar->addAction("Log Scale");
    logScale->setCheckable(true);
    logScale->setChecked(m_plot->logScale());
    connect(logScale, &QAction::toggled, m_plot, &SpectrumPlotWidget::setLogScale);
    connect(m_plot, &SpectrumPlotWidget::logScaleChanged, logScale, &QAction::setChecked);

    m_infoLabel = new QLabel;
    m_infoLabel->setWordWrap(true);
    m_infoLabel->setTextFormat(Qt::RichText);
    m_resetButton = new QPushButton("Fit Again");
    m_resetButton->setToolTip("Find the edge and the fit window automatically");
    m_acceptButton = new QPushButton("Accept Edge");
    m_acceptButton->setToolTip("Use this edge as a calibration point");
    m_removeButton = new QPushButton("Remove Point");
    connect(m_resetButton, &QPushButton::clicked, this, &ToFCalibrationWindow::resetToFit);
    connect(m_acceptButton, &QPushButton::clicked, this, &ToFCalibrationWindow::acceptEdge);
    connect(m_removeButton, &QPushButton::clicked, this, &ToFCalibrationWindow::removeEdge);
    auto *buttons = new QVBoxLayout;
    buttons->addWidget(m_resetButton);
    buttons->addWidget(m_acceptButton);
    buttons->addWidget(m_removeButton);
    buttons->addStretch(1);
    auto *infoRow = new QHBoxLayout;
    infoRow->addWidget(m_infoLabel, 1);
    infoRow->addLayout(buttons);

    m_hint = ZoomHint;
    m_statusLabel = new QLabel(m_hint);
    connect(m_plot, &SpectrumPlotWidget::cursorInfoChanged, this, [this](const QString &text) {
        m_statusLabel->setText(text.isEmpty() ? m_hint : text);
    });

    auto *panel = new QWidget;
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(toolBar);
    layout->addWidget(m_plot, 1);
    layout->addWidget(m_statusLabel);
    layout->addLayout(infoRow);
    return panel;
}

QWidget *ToFCalibrationWindow::createCalibrationPanel() {
    m_calibrationPlot = new CalibrationPlotWidget;
    m_pointTable = new QTreeWidget;
    m_pointTable->setRootIsDecorated(false);
    m_pointTable->setHeaderLabels({"Selection", "Edge (ch)", "Expected (ns)", "Line (ns)", "Residual (ns)"});
    m_pointTable->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    connect(m_pointTable, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        if(!item) {
            return;
        }
        // Show the selection of the point. Later, showing it rebuilds this table.
        const int cutId = item->data(SelectionColumn, CutIdRole).toInt();
        Measurement *measurement = measurementOf(item);
        QTimer::singleShot(0, this, [this, measurement, cutId]() {
            for(QTreeWidgetItemIterator it(m_tree); *it; ++it) {
                if(measurementOf(*it) == measurement && (*it)->data(SelectionColumn, CutIdRole).toInt() == cutId) {
                    m_tree->setCurrentItem(*it);
                    break;
                }
            }
        });
    });
    m_lineLabel = new QLabel;
    m_lineLabel->setWordWrap(true);
    m_applyButton = new QPushButton("Apply to Detector");
    m_applyButton->setToolTip("Set the ToF calibration slope and offset of the detector from the fitted line");
    connect(m_applyButton, &QPushButton::clicked, this, &ToFCalibrationWindow::applyCalibration);

    auto *lineRow = new QHBoxLayout;
    lineRow->addWidget(m_lineLabel, 1);
    lineRow->addWidget(m_applyButton);
    auto *tableColumn = new QVBoxLayout;
    tableColumn->addWidget(new QLabel("Calibration points"));
    tableColumn->addWidget(m_pointTable, 1);
    tableColumn->addLayout(lineRow);
    auto *panel = new QWidget;
    auto *layout = new QHBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_calibrationPlot, 1);
    layout->addLayout(tableColumn, 1);
    return panel;
}

QList<Measurement *> ToFCalibrationWindow::measurements() const {
    const QList<Measurement *> assigned = m_project->measurementsUsing(m_detector);
    QList<Measurement *> result;
    m_project->forEachMeasurement([&](Measurement *measurement) {
        if(assigned.contains(measurement)) {
            result.append(measurement);
        }
    });
    return result;
}

Cut *ToFCalibrationWindow::currentCut() const {
    if(!m_measurement) {
        return nullptr;
    }
    for(Cut &cut : m_measurement->cuts) {
        if(cut.id == m_cutId) {
            return &cut;
        }
    }
    return nullptr;
}

const ToFCalibrationWindow::LoadedData &ToFCalibrationWindow::loadData(Measurement *measurement) {
    LoadedData &data = m_data[measurement];
    if(data.loaded && data.path == measurement->path) {
        return data;
    }
    data = LoadedData{measurement->path};
    if(measurement->path.isEmpty()) {
        data.errorMessage = QStringLiteral("The measurement has no data file.");
        return data;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    data.loaded = loadAscFile(measurement->path, data.events, data.errorMessage);
    QApplication::restoreOverrideCursor();
    if(!data.loaded) {
        data.errorMessage = QString("Could not load %1: %2").arg(measurement->path, data.errorMessage);
    }
    return data;
}

void ToFCalibrationWindow::refresh() {
    setWindowTitle(QString("ToF Calibration — %1").arg(nameOrUnnamed(m_detector->name)));
    const QList<Measurement *> current = measurements();
    // Measurements may have been removed or assigned to other detectors, their data is not needed any more
    std::erase_if(m_data, [&current](const auto &entry) {
        return !current.contains(entry.first) || entry.second.path != entry.first->path;
    });
    const Cut *cut = current.contains(m_measurement) ? currentCut() : nullptr;
    if(!cut) {
        m_measurement = nullptr;
        m_counts.clear();
        m_hasEdge = false;
        m_plot->setCurves(1, {}, SpectrumPlotWidget::ViewChange::ShowAll);
        m_plot->setMarkers({});
        m_plot->setOverlay({}, FitColor);
    } else if(cut->polygon != m_polygon) {
        showSelection(m_measurement, m_cutId);
    }
    updateTree();
    updateInfo();
    updateCalibration();
}

void ToFCalibrationWindow::updateTree() {
    const QSignalBlocker blocker(m_tree);
    m_tree->clear();
    const QList<Measurement *> assigned = measurements();
    for(size_t i = 0; i < m_project->sampleCount(); i++) {
        const Sample *sample = m_project->sample(i);
        for(const auto &measurement : sample->measurements) {
            if(!assigned.contains(measurement.get())) {
                continue;
            }
            auto *measurementItem = new QTreeWidgetItem(m_tree, {QString("%1 / %2").arg(nameOrUnnamed(sample->name),
                                                                                       nameOrUnnamed(measurement->name))});
            setMeasurement(measurementItem, measurement.get());
            measurementItem->setFlags(Qt::ItemIsEnabled);
            if(measurement->path.isEmpty()) {
                measurementItem->setForeground(SelectionColumn, palette().color(QPalette::Disabled, QPalette::Text));
                measurementItem->setToolTip(SelectionColumn, "The measurement has no data file.");
            }
            for(const Cut &cut : measurement->cuts) {
                auto *item = new QTreeWidgetItem(measurementItem, {cutLabel(cut)});
                item->setIcon(SelectionColumn, colorSwatch(cut.color));
                setMeasurement(item, measurement.get());
                item->setData(SelectionColumn, CutIdRole, cut.id);
                if(cut.tofEdge) {
                    item->setText(EdgeColumn, edgeText(*cut.tofEdge));
                }
                const ExpectedTof expected = expectedSurfaceTof(measurement->settings, *m_detector, cut);
                if(!expected.isValid()) {
                    item->setText(ExpectedColumn, QStringLiteral("—"));
                    item->setToolTip(ExpectedColumn, expected.errorMessage);
                    item->setForeground(ExpectedColumn, palette().color(QPalette::Disabled, QPalette::Text));
                } else {
                    item->setText(ExpectedColumn, QString::number(expected.tof / Units::Nanosecond, 'f', 2));
                }
                if(measurement.get() == m_measurement && cut.id == m_cutId) {
                    m_tree->setCurrentItem(item);
                }
            }
        }
    }
    m_tree->expandAll();
}

void ToFCalibrationWindow::currentItemChanged(QTreeWidgetItem *item) {
    if(!item || !item->data(SelectionColumn, CutIdRole).isValid()) {
        return;
    }
    showSelection(measurementOf(item), item->data(SelectionColumn, CutIdRole).toInt());
    updateInfo();
    updateCalibration();
}

void ToFCalibrationWindow::showSelection(Measurement *measurement, int cutId) {
    m_measurement = measurement;
    m_cutId = cutId;
    const Cut *cut = currentCut();
    const LoadedData &data = loadData(measurement);
    m_counts.clear();
    m_polygon = cut->polygon;
    if(data.loaded) {
        m_counts = tofHistogram(data.events, cut->polygon);
    }
    m_fit = findAndFitEdge(m_counts);
    if(cut->tofEdge) {
        m_edge = cut->tofEdge->channel;
        m_manual = cut->tofEdge->manual;
        m_hasEdge = true;
    } else {
        m_edge = m_fit.mu;
        m_manual = false;
        m_hasEdge = m_fit.ok;
    }
    const size_t events = std::accumulate(m_counts.begin(), m_counts.end(), size_t{0});
    QList<SpectrumPlotWidget::Curve> curves;
    if(!m_counts.empty()) {
        curves.append({cutLabel(*cut), cut->color, events, {m_counts.begin(), m_counts.end()}});
    }
    m_plot->setCurves(1, curves, SpectrumPlotWidget::ViewChange::ShowAll);
    // The channels with events, the spectrum starts from channel zero
    const auto first = std::find_if(m_counts.begin(), m_counts.end(), [](size_t count) { return count > 0; });
    if(first != m_counts.end()) {
        const double min = static_cast<double>(first - m_counts.begin());
        const double max = static_cast<double>(m_counts.size());
        const double margin = std::max(10.0, ViewMargin * (max - min));
        m_plot->setXView(min - margin, max + margin);
    }
    showFit();
}

void ToFCalibrationWindow::showFit() {
    QList<SpectrumPlotWidget::Marker> markers;
    QList<QPointF> overlay;
    if(!m_counts.empty() && m_fit.windowMax > m_fit.windowMin) {
        markers.append({m_fit.windowMin - 0.5, WindowColor, Qt::DashLine, true});
        markers.append({m_fit.windowMax + 0.5, WindowColor, Qt::DashLine, true});
        if(m_fit.ok) {
            for(double x = m_fit.windowMin - 0.5; x <= m_fit.windowMax + 0.5; x += OverlayStep) {
                overlay.append({x, m_fit.value(x)});
            }
        }
    }
    if(m_hasEdge) {
        markers.append({m_edge, EdgeColor, m_manual ? Qt::DashDotLine : Qt::SolidLine, false});
    }
    m_plot->setMarkers(markers);
    m_plot->setOverlay(overlay, FitColor);
}

void ToFCalibrationWindow::windowMarkerMoved(int index, double x) {
    if(index > 1) {
        return;
    }
    // A failed fit has no useful values to start from, the guess is used instead
    EdgeFit initial = m_fit.ok ? m_fit : findEdge(m_counts);
    initial.windowMin = m_fit.windowMin;
    initial.windowMax = m_fit.windowMax;
    if(index == 0) {
        initial.windowMin = static_cast<int>(std::lround(x + 0.5));
    } else {
        initial.windowMax = static_cast<int>(std::lround(x - 0.5));
    }
    m_fit = fitEdge(m_counts, initial);
    m_edge = m_fit.mu;
    m_hasEdge = m_fit.ok;
    m_manual = false;
    showFit();
    updateInfo();
}

void ToFCalibrationWindow::edgePicked(double x) {
    if(m_counts.empty()) {
        return;
    }
    m_edge = x;
    m_hasEdge = true;
    m_manual = true;
    showFit();
    updateInfo();
}

void ToFCalibrationWindow::resetToFit() {
    m_fit = findAndFitEdge(m_counts);
    m_edge = m_fit.mu;
    m_hasEdge = m_fit.ok;
    m_manual = false;
    showFit();
    updateInfo();
}

void ToFCalibrationWindow::acceptEdge() {
    Cut *cut = currentCut();
    if(!cut || !m_hasEdge) {
        return;
    }
    cut->tofEdge = ToFEdge{m_edge, m_manual ? 0 : m_fit.sigma, m_manual};
    updateTree();
    updateInfo();
    updateCalibration();
    emit modified();
}

void ToFCalibrationWindow::removeEdge() {
    Cut *cut = currentCut();
    if(!cut || !cut->tofEdge) {
        return;
    }
    cut->tofEdge.reset();
    updateTree();
    updateInfo();
    updateCalibration();
    emit modified();
}

void ToFCalibrationWindow::updateInfo() {
    const Cut *cut = currentCut();
    m_resetButton->setEnabled(cut && !m_counts.empty());
    m_removeButton->setEnabled(cut && cut->tofEdge);
    m_acceptButton->setEnabled(false);
    if(!cut) {
        m_infoLabel->setText("Click a selection to see its ToF spectrum.");
        return;
    }
    QStringList lines;
    const LoadedData &data = loadData(m_measurement);
    if(!data.loaded) {
        lines.append(QString("<span style='color: red'>%1</span>").arg(data.errorMessage.toHtmlEscaped()));
    } else if(!m_hasEdge) {
        lines.append(QString("<span style='color: red'>%1</span> Set the edge by hand or change the fit window.")
                             .arg(m_fit.errorMessage.toHtmlEscaped()));
    } else if(m_manual) {
        lines.append(QString("<b>Edge %1 ch</b>, set by hand").arg(m_edge, 0, 'f', 1));
    } else {
        lines.append(QString("<b>Edge μ = %1 ± %2 ch</b>, σ = %3 ch (fit in channels %4–%5)")
                             .arg(m_edge, 0, 'f', 2).arg(m_fit.muError, 0, 'f', 2).arg(m_fit.sigma, 0, 'f', 2)
                             .arg(m_fit.windowMin).arg(m_fit.windowMax));
    }
    if(cut->tofEdge) {
        lines.append(QString("Accepted edge: %1 ch").arg(edgeText(*cut->tofEdge)));
    }

    const ExperimentalSettings *settings = m_measurement->settings;
    const ExpectedTof expected = expectedSurfaceTof(settings, *m_detector, *cut);
    if(!expected.isValid()) {
        lines.append(QString("<span style='color: red'>Expected ToF: %1</span>").arg(expected.errorMessage.toHtmlEscaped()));
    } else {
        lines.append(QString("%1 from the surface: %2 MeV, %3 keV lost in the timing detector foil, "
                             "<b>expected ToF %4 ns</b>")
                             .arg(particleText(settings, *cut).toHtmlEscaped())
                             .arg(expected.energy / Units::Megaelectronvolt, 0, 'f', 4)
                             .arg(expected.foilLoss / Units::Kiloelectronvolt, 0, 'f', 1)
                             .arg(expected.tof / Units::Nanosecond, 0, 'f', 3));
        m_acceptButton->setEnabled(m_hasEdge);
    }
    if(m_hasEdge && m_detector->tofSlope > 0) {
        lines.append(QString("With the current calibration the edge is at %1 ns.")
                             .arg((m_detector->tofSlope * m_edge + m_detector->tofOffset) / Units::Nanosecond, 0, 'f', 3));
    }
    m_infoLabel->setText(lines.join("<br>"));
}

void ToFCalibrationWindow::updateCalibration() {
    m_points.clear();
    QList<QPointF> fitPoints;
    for(Measurement *measurement : measurements()) {
        for(const Cut &cut : measurement->cuts) {
            if(!cut.tofEdge) {
                continue;
            }
            CalibrationPoint point{measurement, cut.id, QString("%1 / %2").arg(nameOrUnnamed(measurement->name), cutLabel(cut)),
                                   cut.tofEdge->channel};
            const ExpectedTof expected = expectedSurfaceTof(measurement->settings, *m_detector, cut);
            point.tof = expected.isValid() ? expected.tof : std::numeric_limits<double>::quiet_NaN();
            point.errorMessage = expected.errorMessage;
            if(expected.isValid()) {
                fitPoints.append({point.channel, point.tof});
            }
            m_points.append(point);
        }
    }
    m_line = fitLine(fitPoints);

    const QSignalBlocker blocker(m_pointTable);
    m_pointTable->clear();
    QList<QPointF> plotPoints;
    int highlighted = -1;
    for(const CalibrationPoint &point : m_points) {
        auto *item = new QTreeWidgetItem(m_pointTable, {point.label, QString::number(point.channel, 'f', 2)});
        setMeasurement(item, point.measurement);
        item->setData(SelectionColumn, CutIdRole, point.cutId);
        if(std::isnan(point.tof)) {
            item->setText(PointExpectedColumn, QStringLiteral("—"));
            item->setToolTip(PointExpectedColumn, point.errorMessage);
            continue;
        }
        const double tof = point.tof / Units::Nanosecond;
        item->setText(PointExpectedColumn, QString::number(tof, 'f', 3));
        if(m_line.ok) {
            const double line = (m_line.slope * point.channel + m_line.offset) / Units::Nanosecond;
            item->setText(PointLineColumn, QString::number(line, 'f', 3));
            const double residual = tof - line;
            item->setText(PointResidualColumn, QString::number(std::abs(residual) < 0.0005 ? 0 : residual, 'f', 3));
        }
        if(point.measurement == m_measurement && point.cutId == m_cutId) {
            highlighted = static_cast<int>(plotPoints.size());
            m_pointTable->setCurrentItem(item);
        }
        plotPoints.append({point.channel, tof});
    }
    m_calibrationPlot->setPoints(plotPoints);
    m_calibrationPlot->setHighlighted(highlighted);

    const QString current = QString("Detector now: slope %1 ns/ch, offset %2 ns.")
                                    .arg(m_detector->tofSlope / Units::Nanosecond, 0, 'f', 6)
                                    .arg(m_detector->tofOffset / Units::Nanosecond, 0, 'f', 3);
    if(m_line.ok) {
        m_calibrationPlot->setLine(m_line.slope / Units::Nanosecond, m_line.offset / Units::Nanosecond);
        m_lineLabel->setText(QString("Fit: slope %1 ns/ch, offset %2 ns.\n%3")
                                     .arg(m_line.slope / Units::Nanosecond, 0, 'f', 6)
                                     .arg(m_line.offset / Units::Nanosecond, 0, 'f', 3).arg(current));
    } else {
        m_calibrationPlot->clearLine();
        m_lineLabel->setText(QString("%1\n%2").arg(m_line.errorMessage, current));
    }
    m_applyButton->setEnabled(m_line.ok);
}

void ToFCalibrationWindow::applyCalibration() {
    if(!m_line.ok) {
        return;
    }
    m_detector->tofSlope = m_line.slope;
    m_detector->tofOffset = m_line.offset;
    updateInfo();
    updateCalibration();
    emit calibrationApplied();
}
