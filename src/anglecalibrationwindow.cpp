#include "anglecalibrationwindow.h"
#include "colorbutton.h"
#include "defaults.h"
#include "histogramwidget.h"
#include "project.h"
#include "spectrumplotwidget.h"
#include "tofcalibration.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
constexpr int MeasurementRole = Qt::UserRole;
constexpr int CutIdRole = Qt::UserRole + 1;
constexpr int EdgePoints = 41; // Across the window
constexpr int FitPointsPerBin = 4; // Of the fitted curve
const QColor RangeColor(0x80, 0x80, 0x80);
const QColor FitColor(0x1f, 0x77, 0xb4);
const QColor EdgeColor(0x80, 0x00, 0x00);
constexpr double EdgeWidth = 3; // Pixels
const QColor DataColor(0x40, 0x40, 0x40);

enum SliceColumn { SliceTofColumn, SliceEventsColumn, SliceCentreColumn };

QString nameOrUnnamed(const QString &name) {
    return name.isEmpty() ? QStringLiteral("(unnamed)") : name;
}

Measurement *measurementOf(const QVariant &data) {
    return reinterpret_cast<Measurement *>(data.value<quintptr>());
}

QVariant measurementData(Measurement *measurement) {
    return QVariant::fromValue(reinterpret_cast<quintptr>(measurement));
}
}

AngleCalibrationWindow::AngleCalibrationWindow(Project *project, DetectorSettings *detector, QWidget *parent)
    : QWidget(parent, Qt::Window), m_project(project), m_detector(detector), m_fitMin(NaN), m_fitMax(NaN) {
    m_list = new QListWidget;
    m_list->setToolTip("The events of the checked measurements are used together");
    m_edgeCombo = new QComboBox;
    m_edgeCombo->setToolTip("The expected leading edge of the selection over the window is drawn for the chosen "
                            "direction of the angle. Needs the experimental settings and the ToF calibration.");
    m_maxTofSpin = new QSpinBox;
    m_maxTofSpin->setRange(0, 1000000);
    m_maxTofSpin->setSuffix(" ch");
    m_maxTofSpin->setToolTip("Events with a longer ToF are left out of the fit, also by dragging the line in the plot");
    m_binWidthSpin = new QSpinBox;
    m_binWidthSpin->setRange(1, 100);
    m_binWidthSpin->setSuffix(" ticks");
    m_binWidthSpin->setToolTip("Of the distribution and the fit. Wider bins even out ticks of unequal widths.");
    m_slicesSpin = new QSpinBox;
    m_slicesSpin->setRange(1, 50);
    m_slicesSpin->setValue(5);
    m_slicesSpin->setToolTip("The centre of the window is fitted in this many slices of ToF with equal counts, and a "
                             "straight line through them gives its shift with ToF. One slice for a constant centre.");
    m_smoothingSpin = new QDoubleSpinBox;
    m_smoothingSpin->setRange(0, 50);
    m_smoothingSpin->setDecimals(1);
    m_smoothingSpin->setSingleStep(0.5);
    m_smoothingSpin->setPrefix("± ");
    m_smoothingSpin->setSuffix(" ticks");
    m_smoothingSpin->setSpecialValueText("None");
    m_smoothingSpin->setToolTip("Each timing value is spread uniformly over this many ticks on both sides, in the plots "
                                "and the fit. Evens out structure of the ticks, e.g. ticks of unequal widths. Widens "
                                "the fitted resolution σ, not the window.");
    m_smoothingSpin->setValue(detector->angleCalibration.timingSmoothing);
    m_reversedCheck = new QCheckBox("Larger timing is a smaller angle");
    m_reversedCheck->setToolTip("The direction of the angle, not known from the fit. The expected edge tells it: it "
                                "should follow the leading edge of the data.");
    m_reversedCheck->setChecked(detector->angleCalibration.reversed);
    m_edgeNote = new QLabel;
    m_edgeNote->setWordWrap(true);

    auto *form = new QFormLayout;
    form->addRow("Maximum ToF:", m_maxTofSpin);
    form->addRow("Timing smoothing:", m_smoothingSpin);
    form->addRow("Bin width:", m_binWidthSpin);
    form->addRow("ToF slices:", m_slicesSpin);
    form->addRow("Expected edge of:", m_edgeCombo);
    form->addRow(m_reversedCheck);
    form->addRow(m_edgeNote);

    m_sliceTable = new QTreeWidget;
    m_sliceTable->setRootIsDecorated(false);
    m_sliceTable->setHeaderLabels({"ToF (ch)", "Events", "Centre (ticks)"});
    m_sliceTable->header()->setSectionResizeMode(QHeaderView::ResizeToContents);

    auto *left = new QWidget;
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(new QLabel("Measurements"));
    leftLayout->addWidget(m_list, 1);
    leftLayout->addLayout(form);
    leftLayout->addWidget(new QLabel("Centres of the ToF slices"));
    leftLayout->addWidget(m_sliceTable, 1);

    m_histogram = new HistogramWidget;
    m_histogram->setAxes("ToF", "ch", "Timing", "ticks");
    m_histogram->applyPreferences(Defaults::histogramPreferences());
    m_histogram->setLogScale(true);
    m_histogram->setEvents(&m_plotEvents);
    m_histogram->setMarkersDraggable(true);
    m_histogram->setToolTip("Drag the dashed line to change the maximum ToF");
    m_distribution = new SpectrumPlotWidget;
    SpectrumPlotWidget::YAxis yAxis;
    yAxis.legendEvents = false;
    m_distribution->setYAxis(yAxis);
    m_distribution->setAutoYScale(true);
    m_distribution->setToolTip("Drag the dashed lines to change the fit range");
    auto *plots = new QSplitter(Qt::Vertical);
    plots->addWidget(m_histogram);
    plots->addWidget(m_distribution);
    plots->setSizes({450, 300});

    m_cursorInfo = new QLabel;
    m_cursorInfo->setMinimumHeight(m_cursorInfo->fontMetrics().height());
    m_resultLabel = new QLabel;
    m_resultLabel->setWordWrap(true);
    m_resultLabel->setTextFormat(Qt::RichText);
    m_resultLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_autoRangeButton = new QPushButton("Automatic Range");
    m_autoRangeButton->setToolTip("Find the fit range from the data again");
    m_applyButton = new QPushButton("Apply to Detector");
    m_applyButton->setToolTip("Set the angle calibration of the detector from the fit");
    auto *buttons = new QVBoxLayout;
    buttons->addWidget(m_autoRangeButton);
    buttons->addWidget(m_applyButton);
    buttons->addStretch(1);
    auto *resultRow = new QHBoxLayout;
    resultRow->addWidget(m_resultLabel, 1);
    resultRow->addLayout(buttons);

    auto *right = new QWidget;
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(plots, 1);
    rightLayout->addWidget(m_cursorInfo);
    rightLayout->addLayout(resultRow);

    auto *splitter = new QSplitter;
    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({330, 900});
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(splitter);
    resize(1250, 850);

    m_fitTimer = new QTimer(this);
    m_fitTimer->setSingleShot(true);
    m_fitTimer->setInterval(200);
    connect(m_fitTimer, &QTimer::timeout, this, [this]() {
        if(m_smoothingSpin->value() != m_smoothedWith) {
            smoothEvents();
            return;
        }
        QApplication::setOverrideCursor(Qt::WaitCursor);
        m_fit = fitAngleCalibration(m_smoothedEvents, {static_cast<double>(m_maxTofSpin->value()),
                                               static_cast<double>(m_binWidthSpin->value()), m_slicesSpin->value(),
                                               m_fitMin, m_fitMax});
        QApplication::restoreOverrideCursor();
        showFit();
    });
    connect(m_list, &QListWidget::itemChanged, this, [this]() {
        m_checked.clear();
        for(int i = 0; i < m_list->count(); i++) {
            if(m_list->item(i)->checkState() == Qt::Checked) {
                m_checked.append(measurementOf(m_list->item(i)->data(MeasurementRole)));
            }
        }
        collectEvents();
        updateEdgeChoices();
    });
    for(QSpinBox *spinBox : {m_maxTofSpin, m_binWidthSpin, m_slicesSpin}) {
        connect(spinBox, &QSpinBox::valueChanged, this, &AngleCalibrationWindow::fit);
    }
    connect(m_smoothingSpin, &QDoubleSpinBox::valueChanged, this, &AngleCalibrationWindow::fit);
    connect(m_maxTofSpin, &QSpinBox::valueChanged, this, [this](int value) {
        m_histogram->setVerticalMarkers({value + 0.5});
    });
    connect(m_histogram, &HistogramWidget::markerMoved, this, [this](int, double x) {
        m_maxTofSpin->setValue(static_cast<int>(std::floor(x)));
    });
    connect(m_distribution, &SpectrumPlotWidget::markerMoved, this, [this](int index, double x) {
        if(index > 1 || (!m_fit.window.ok && std::isnan(m_fitMin))) {
            return;
        }
        if(std::isnan(m_fitMin)) {
            m_fitMin = m_fit.window.fitMin;
            m_fitMax = m_fit.window.fitMax;
        }
        (index == 0 ? m_fitMin : m_fitMax) = x;
        fit();
    });
    connect(m_autoRangeButton, &QPushButton::clicked, this, [this]() {
        m_fitMin = NaN;
        m_fitMax = NaN;
        fit();
    });
    connect(m_edgeCombo, &QComboBox::currentIndexChanged, this, &AngleCalibrationWindow::updateOverlay);
    connect(m_reversedCheck, &QCheckBox::toggled, this, &AngleCalibrationWindow::updateOverlay);
    connect(m_applyButton, &QPushButton::clicked, this, &AngleCalibrationWindow::apply);
    // The angle at the cursor follows the info of the plots, the distribution is at the reference ToF
    connect(m_histogram, &HistogramWidget::cursorMoved, this, [this](QPointF position) { m_cursor = position; });
    connect(m_distribution, &SpectrumPlotWidget::cursorMoved, this, [this](double x) {
        m_cursor = {m_fit.referenceTof, x};
    });
    auto showCursorInfo = [this](const QString &text) {
        const QString angle = text.isEmpty() ? QString() : angleText(m_cursor.x(), m_cursor.y());
        m_cursorInfo->setText(angle.isEmpty() ? text : QString("%1   %2").arg(text, angle));
    };
    connect(m_histogram, &HistogramWidget::cursorInfoChanged, this, showCursorInfo);
    connect(m_distribution, &SpectrumPlotWidget::cursorInfoChanged, this, showCursorInfo);

    const QList<Measurement *> assigned = measurements();
    if(!assigned.isEmpty()) {
        m_checked.append(assigned.first());
    }
    refresh();
}

QList<Measurement *> AngleCalibrationWindow::measurements() const {
    const QList<Measurement *> assigned = m_project->measurementsUsing(m_detector);
    QList<Measurement *> result;
    m_project->forEachMeasurement([&](Measurement *measurement) {
        if(assigned.contains(measurement)) {
            result.append(measurement);
        }
    });
    return result;
}

const AngleCalibrationWindow::LoadedData &AngleCalibrationWindow::loadData(Measurement *measurement) {
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
        return data;
    }
    data.withTiming = static_cast<size_t>(std::count_if(data.events.begin(), data.events.end(),
                                                        [](const ToFEEvent &event) { return std::isfinite(event.timing); }));
    if(data.withTiming == 0) {
        data.errorMessage = QStringLiteral("The data file has no timing column (the third column).");
    }
    return data;
}

void AngleCalibrationWindow::refresh() {
    setWindowTitle(QString("Angle Calibration — %1").arg(nameOrUnnamed(m_detector->name)));
    const QList<Measurement *> current = measurements();
    std::erase_if(m_data, [&current](const auto &entry) {
        return !current.contains(entry.first) || entry.second.path != entry.first->path;
    });
    m_checked.removeIf([&current](Measurement *measurement) { return !current.contains(measurement); });
    updateList();
    // The events are collected again only when the checked measurements or their files have changed
    QStringList collected;
    for(const Measurement *measurement : m_checked) {
        collected.append(measurement->path);
    }
    if(collected != m_collectedPaths || m_checked.size() != m_collectedCount) {
        collectEvents();
    } else {
        updateResults();
    }
    updateEdgeChoices();
}

void AngleCalibrationWindow::updateList() {
    const QSignalBlocker blocker(m_list);
    m_list->clear();
    const QList<Measurement *> assigned = measurements();
    for(size_t i = 0; i < m_project->sampleCount(); i++) {
        const Sample *sample = m_project->sample(i);
        for(const auto &measurement : sample->measurements) {
            if(!assigned.contains(measurement.get())) {
                continue;
            }
            auto *item = new QListWidgetItem(QString("%1 / %2").arg(nameOrUnnamed(sample->name), nameOrUnnamed(measurement->name)),
                                             m_list);
            item->setData(MeasurementRole, measurementData(measurement.get()));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            item->setCheckState(m_checked.contains(measurement.get()) ? Qt::Checked : Qt::Unchecked);
            if(measurement->path.isEmpty()) {
                item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
                item->setToolTip("The measurement has no data file.");
            }
        }
    }
}

void AngleCalibrationWindow::updateEdgeChoices() {
    // The choice is kept when the selection is still there
    const Measurement *measurement = measurementOf(m_edgeCombo->currentData(MeasurementRole));
    const int cutId = m_edgeCombo->currentData(CutIdRole).toInt();
    const QSignalBlocker blocker(m_edgeCombo);
    m_edgeCombo->clear();
    m_edgeCombo->addItem("None");
    for(Measurement *checked : m_checked) {
        for(const Cut &cut : checked->cuts) {
            if(!isAnalyzed(cut)) {
                continue;
            }
            const QString text = m_checked.size() > 1 ? QString("%1: %2").arg(nameOrUnnamed(checked->name), cutLabel(cut))
                                                      : cutLabel(cut);
            m_edgeCombo->addItem(colorSwatch(cut.color), text);
            m_edgeCombo->setItemData(m_edgeCombo->count() - 1, measurementData(checked), MeasurementRole);
            m_edgeCombo->setItemData(m_edgeCombo->count() - 1, cut.id, CutIdRole);
            if(checked == measurement && cut.id == cutId) {
                m_edgeCombo->setCurrentIndex(m_edgeCombo->count() - 1);
            }
        }
    }
    updateOverlay();
}

void AngleCalibrationWindow::collectEvents() {
    m_events.clear();
    m_collectedPaths.clear();
    m_collectedCount = m_checked.size();
    QStringList problems;
    for(Measurement *measurement : m_checked) {
        m_collectedPaths.append(measurement->path);
        const LoadedData &data = loadData(measurement);
        if(!data.errorMessage.isEmpty()) {
            problems.append(QString("%1: %2").arg(nameOrUnnamed(measurement->name), data.errorMessage));
        }
        for(const ToFEEvent &event : data.events) {
            if(std::isfinite(event.timing)) {
                m_events.push_back(event);
            }
        }
    }
    m_list->setToolTip(problems.isEmpty() ? QStringLiteral("The events of the checked measurements are used together")
                                          : problems.join('\n'));
    m_minTof = 0;
    m_maxTof = 0;
    if(!m_events.empty()) {
        const auto [min, max] = std::minmax_element(m_events.begin(), m_events.end(),
                                                    [](const ToFEEvent &a, const ToFEEvent &b) { return a.tof < b.tof; });
        m_minTof = min->tof;
        m_maxTof = max->tof;
    }
    // At first the maximum ToF of the calibration, or the median ToF of the events
    const QSignalBlocker blocker(m_maxTofSpin);
    if(m_maxTofSpin->value() > 0) {
        // Kept
    } else if(m_detector->angleCalibration.maxTof > 0) {
        m_maxTofSpin->setValue(static_cast<int>(m_detector->angleCalibration.maxTof));
    } else if(!m_events.empty()) {
        std::vector<double> tofs(m_events.size());
        std::transform(m_events.begin(), m_events.end(), tofs.begin(), [](const ToFEEvent &event) { return event.tof; });
        std::nth_element(tofs.begin(), tofs.begin() + static_cast<std::ptrdiff_t>(tofs.size() / 2), tofs.end());
        m_maxTofSpin->setValue(static_cast<int>(tofs[tofs.size() / 2]));
    }
    smoothEvents();
}

void AngleCalibrationWindow::smoothEvents() {
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_smoothedWith = m_smoothingSpin->value();
    m_smoothedEvents = smoothTiming(m_events, m_smoothedWith);
    m_plotEvents.clear();
    m_plotEvents.reserve(m_smoothedEvents.size());
    for(const ToFEEvent &event : m_smoothedEvents) {
        m_plotEvents.push_back({event.tof, event.timing});
    }
    m_histogram->setEvents(&m_plotEvents);
    m_histogram->setVerticalMarkers({m_maxTofSpin->value() + 0.5});
    m_fitTimer->stop();
    m_fit = fitAngleCalibration(m_smoothedEvents, {static_cast<double>(m_maxTofSpin->value()),
                                           static_cast<double>(m_binWidthSpin->value()), m_slicesSpin->value(), m_fitMin,
                                           m_fitMax});
    // The timing axis covers the distribution, without outliers
    if(!m_fit.histogram.counts.empty()) {
        m_histogram->setFullYRange(m_fit.histogram.binStart(0), m_fit.histogram.binStart(m_fit.histogram.counts.size()));
    }
    QApplication::restoreOverrideCursor();
    showFit();
    m_distribution->resetZoom();
}

void AngleCalibrationWindow::fit() {
    m_fitTimer->start();
}

void AngleCalibrationWindow::showFit() {
    const TimingHistogram &histogram = m_fit.histogram;
    SpectrumPlotWidget::XAxis axis;
    axis.title = m_fit.centreSlope != 0 ? QString("Timing at ToF %1 ch (ticks)").arg(m_fit.referenceTof, 0, 'f', 0)
                                        : QStringLiteral("Timing (ticks)");
    axis.unit = QStringLiteral("ticks");
    axis.decimals = 1;
    axis.origin = histogram.binStart(0);
    axis.binWidthScale = 1;
    axis.binWidthUnit = QStringLiteral("ticks");
    m_distribution->setXAxis(axis);
    SpectrumPlotWidget::Curve curve{QStringLiteral("Events"), DataColor, m_fit.events, histogram.counts};
    const WindowFit &window = m_fit.window;
    if(window.ok) {
        const double step = histogram.binWidth / FitPointsPerBin;
        for(double x = window.fitMin + histogram.binWidth / 2; x <= window.fitMax - histogram.binWidth / 2 + 1e-9; x += step) {
            curve.line.append({x, window.value(x, histogram.binWidth)});
        }
    }
    m_distribution->setCurves(histogram.binWidth, {curve}, SpectrumPlotWidget::ViewChange::Keep);
    QList<SpectrumPlotWidget::Marker> markers;
    if(window.ok || std::isfinite(m_fitMin)) {
        markers.append({window.fitMin, RangeColor, Qt::DashLine, true});
        markers.append({window.fitMax, RangeColor, Qt::DashLine, true});
    }
    if(window.ok) {
        markers.append({window.centre - window.halfWidth, FitColor, Qt::SolidLine, false});
        markers.append({window.centre, FitColor, Qt::DotLine, false});
        markers.append({window.centre + window.halfWidth, FitColor, Qt::SolidLine, false});
    }
    m_distribution->setMarkers(markers);

    m_sliceTable->clear();
    for(const SliceCentre &slice : m_fit.slices) {
        auto *item = new QTreeWidgetItem(m_sliceTable);
        item->setText(SliceTofColumn, QString::number(slice.tof, 'f', 0));
        item->setText(SliceEventsColumn, QString::number(slice.events));
        item->setText(SliceCentreColumn, slice.ok ? QString("%1 ± %2").arg(slice.centre, 0, 'f', 2).arg(slice.centreError, 0, 'f', 2)
                                                  : QStringLiteral("—"));
    }
    updateOverlay();
    updateResults();
}

void AngleCalibrationWindow::updateOverlay() {
    QList<HistogramWidget::OverlayLine> lines;
    m_edgeNote->clear();
    if(!m_fit.ok) {
        m_histogram->setOverlayLines(lines);
        return;
    }
    const WindowFit &window = m_fit.window;
    // The fitted window over the ToF of the events
    for(const double offset : {-window.halfWidth, 0.0, window.halfWidth}) {
        QPolygonF line{{m_minTof, m_fit.centreAt(m_minTof) + offset}, {m_maxTof, m_fit.centreAt(m_maxTof) + offset}};
        lines.append({line, FitColor, offset == 0 ? Qt::DotLine : Qt::SolidLine});
    }

    // Expected leading edge of the chosen selection at each angle over the window, for the chosen direction
    Measurement *measurement = measurementOf(m_edgeCombo->currentData(MeasurementRole));
    const Cut *cut = measurement ? measurement->cut(m_edgeCombo->currentData(CutIdRole).toInt()) : nullptr;
    if(cut) {
        const double halfAngle = windowHalfAngle(*m_detector);
        if(halfAngle <= 0) {
            m_edgeNote->setText("The detector distance and window width are needed for the expected edge.");
        } else if(m_detector->tofSlope <= 0) {
            m_edgeNote->setText("The ToF calibration is needed for the expected edge.");
        } else {
            QString error;
            const bool reversed = m_reversedCheck->isChecked();
            QPolygonF edge;
            for(int i = 0; i < EdgePoints; i++) {
                const double position = -1 + 2.0 * i / (EdgePoints - 1); // From the low timing edge to the high
                DetectorSettings detector = *m_detector;
                detector.scatteringAngle += std::atan((reversed ? -position : position) * std::tan(halfAngle));
                const ExpectedTof expected = expectedSurfaceTof(measurement->settings, detector,
                                                                measurement->physicsOrDefault(), *cut);
                if(!expected.isValid()) {
                    error = expected.errorMessage;
                    continue;
                }
                const double channel = (expected.tof - m_detector->tofOffset) / m_detector->tofSlope;
                edge.append({channel, m_fit.centreAt(channel) + position * window.halfWidth});
            }
            lines.append({edge, EdgeColor, Qt::DashLine, EdgeWidth});
            m_edgeNote->setText(error.isEmpty() ? QStringLiteral("The edge of the data should follow the dashed edge. "
                                                                 "If it slopes the other way, change the direction.")
                                                : error);
        }
    }
    m_histogram->setOverlayLines(lines);
}

void AngleCalibrationWindow::updateResults() {
    m_applyButton->setEnabled(m_fit.ok);
    m_autoRangeButton->setEnabled(std::isfinite(m_fitMin));
    if(m_events.empty()) {
        m_resultLabel->setText(m_checked.isEmpty() ? QStringLiteral("Check the measurements to use.")
                                                   : QStringLiteral("The checked measurements have no timing values."));
        return;
    }
    QString text = QString("%1 events up to ToF %2 ch.").arg(m_fit.events).arg(m_maxTofSpin->value());
    if(!m_fit.ok) {
        m_resultLabel->setText(QString("%1 <b>%2</b>").arg(text, m_fit.errorMessage.toHtmlEscaped()));
        return;
    }
    const WindowFit &window = m_fit.window;
    text += QString(" Centre <b>%1 ± %2 ticks</b> at ToF %3 ch")
                    .arg(window.centre, 0, 'f', 2)
                    .arg(window.centreError, 0, 'f', 2)
                    .arg(m_fit.referenceTof, 0, 'f', 0);
    if(m_fit.slopeMessage.isEmpty() && !m_fit.slices.empty()) {
        text += QString(", shifting %1 ± %2 ticks/ch").arg(m_fit.centreSlope, 0, 'g', 3).arg(m_fit.centreSlopeError, 0, 'g', 2);
    } else if(!m_fit.slopeMessage.isEmpty()) {
        text += QString(" (%1)").arg(m_fit.slopeMessage.toHtmlEscaped());
    }
    text += QString(". Half width <b>%1 ± %2 ticks</b>, resolution σ %3 ticks.")
                    .arg(window.halfWidth, 0, 'f', 2)
                    .arg(window.halfWidthError, 0, 'f', 2)
                    .arg(window.sigma, 0, 'f', 2);
    const double halfAngle = windowHalfAngle(*m_detector);
    if(halfAngle > 0) {
        text += QString(" The window covers ± %1°, %2° per tick.")
                        .arg(halfAngle / Units::Degree, 0, 'f', 3)
                        .arg(halfAngle / window.halfWidth / Units::Degree, 0, 'g', 3);
    } else {
        text += QStringLiteral(" Set the detector distance and window width in the detector settings for the angles.");
    }
    m_resultLabel->setText(text);
}

QString AngleCalibrationWindow::angleText(double tof, double timing) const {
    const double halfAngle = windowHalfAngle(*m_detector);
    if(!m_fit.ok || halfAngle <= 0) {
        return {};
    }
    double position = (timing - m_fit.centreAt(tof)) / m_fit.window.halfWidth;
    if(m_reversedCheck->isChecked()) {
        position = -position;
    }
    // Clamped to the window as in the correction
    const bool outside = std::abs(position) > 1;
    const double offset = std::atan(std::clamp(position, -1.0, 1.0) * std::tan(halfAngle));
    return QString("Angle %1° (%2%3°)%4")
            .arg((m_detector->scatteringAngle + offset) / Units::Degree, 0, 'f', 3)
            .arg(offset < 0 ? "−" : "+")
            .arg(std::abs(offset) / Units::Degree, 0, 'f', 3)
            .arg(outside ? QStringLiteral(", outside the window") : QString());
}

void AngleCalibrationWindow::apply() {
    if(!m_fit.ok) {
        return;
    }
    AngleCalibration calibration = m_fit.calibration(m_reversedCheck->isChecked());
    calibration.maxTof = m_maxTofSpin->value();
    calibration.timingSmoothing = m_smoothingSpin->value();
    m_detector->angleCalibration = calibration;
    emit calibrationApplied();
}
