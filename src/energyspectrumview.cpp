#include "energyspectrumview.h"
#include "colorbutton.h"
#include "elements.h"
#include "project.h"
#include "rangewidget.h"
#include "tableexport.h"

#include <QActionGroup>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

namespace {
constexpr int CutIdRole = Qt::UserRole;
}

EnergySpectrumView::EnergySpectrumView(Measurement *measurement, EnergySpectrum *spectrum, QWidget *parent)
    : QWidget(parent), m_measurement(measurement), m_spectrum(spectrum) {
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
    m_plot->setLegendCorner(legendCorner(m_spectrum->legendPosition));
    connect(m_plot, &SpectrumPlotWidget::legendCornerChanged, this, [this](Qt::Corner corner) {
        m_spectrum->legendPosition = legendPosition(corner);
        emit modified();
    });
    m_statusLabel = new QLabel;
    m_statusLabel->setMinimumHeight(m_statusLabel->fontMetrics().height()); // Cursor info, or the hint of the mode
    m_errorLabel = new QLabel;
    m_errorLabel->setStyleSheet("color: red");
    m_errorLabel->setWordWrap(true);
    m_errorLabel->hide();

    auto *splitter = new QSplitter;
    splitter->addWidget(m_plot);
    splitter->addWidget(createSelectionPanel());
    splitter->setStretchFactor(0, 1);
    splitter->setSizes({800, 220});

    layout->addWidget(createToolBar());
    layout->addWidget(createStaleBar());
    layout->addWidget(m_errorLabel);
    layout->addWidget(splitter, 1);
    layout->addWidget(m_statusLabel);

    connect(m_plot, &SpectrumPlotWidget::cursorInfoChanged, this, [this](const QString &text) {
        m_statusLabel->setText(text.isEmpty() ? m_hint : text);
    });
    updateSelectionList();
    recalculate();
}

QString EnergySpectrumView::title() const {
    const QString title = QString("%1 — %2").arg(m_measurement->name, m_spectrum->name);
    return m_stale ? title + QStringLiteral(" •") : title;
}

QWidget *EnergySpectrumView::createToolBar() {
    auto *toolBar = new QToolBar;
    toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    QAction *zoomIn = toolBar->addAction("Zoom In", m_plot, &SpectrumPlotWidget::zoomIn);
    zoomIn->setShortcut(QKeySequence::ZoomIn);
    QAction *zoomOut = toolBar->addAction("Zoom Out", m_plot, &SpectrumPlotWidget::zoomOut);
    zoomOut->setShortcut(QKeySequence::ZoomOut);
    // Shortcuts apply only in this tab, several spectra can be open
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
    zoomMode->setToolTip("Drag to zoom, double-click to show everything");
    addMode("Move", SpectrumPlotWidget::Mode::Move, {})->setToolTip("Drag to move the view");
    toolBar->addSeparator();

    QAction *logScale = toolBar->addAction("Log Scale");
    logScale->setCheckable(true);
    logScale->setChecked(m_plot->logScale());
    connect(logScale, &QAction::toggled, m_plot, &SpectrumPlotWidget::setLogScale);
    connect(m_plot, &SpectrumPlotWidget::logScaleChanged, logScale, &QAction::setChecked);
    toolBar->addSeparator();

    m_binWidthSpin = new QDoubleSpinBox;
    m_binWidthSpin->setRange(0.1, 10000);
    m_binWidthSpin->setDecimals(1);
    m_binWidthSpin->setSuffix(" keV");
    m_binWidthSpin->setKeyboardTracking(false); // Recalculated when the value is entered, not on every key
    m_binWidthSpin->setValue(m_spectrum->binWidth / Units::Kiloelectronvolt);
    toolBar->addWidget(new QLabel("Bin width: "));
    toolBar->addWidget(m_binWidthSpin);
    connect(m_binWidthSpin, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        m_spectrum->binWidth = value * Units::Kiloelectronvolt;
        recalculate();
        emit modified();
    });
    toolBar->addSeparator();
    toolBar->addAction("Refresh", this, &EnergySpectrumView::recalculate);
    toolBar->addAction("Export...", this, &EnergySpectrumView::exportSpectrum);
    return toolBar;
}

QWidget *EnergySpectrumView::createStaleBar() {
    m_staleBar = new QWidget;
    m_staleBar->setAutoFillBackground(true);
    QPalette palette = m_staleBar->palette();
    palette.setColor(QPalette::Window, QColor(0xff, 0xe0, 0x82));
    palette.setColor(QPalette::WindowText, Qt::black);
    m_staleBar->setPalette(palette);
    auto *label = new QLabel("Out of date.");
    label->setToolTip("The selections, the selection that divides the measurement, the ToF calibration, the timing "
                      "detector foil, the incident ion or the physics settings have changed since the spectrum was "
                      "calculated.");
    auto *refresh = new QPushButton("Refresh");
    auto *layout = new QHBoxLayout(m_staleBar);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->addWidget(label, 1);
    layout->addWidget(refresh);
    connect(refresh, &QPushButton::clicked, this, &EnergySpectrumView::recalculate);
    m_staleBar->hide();
    return m_staleBar;
}

QWidget *EnergySpectrumView::createSelectionPanel() {
    m_selectionList = new QListWidget;
    auto *panel = new QWidget;
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new QLabel("Selections"));
    layout->addWidget(m_selectionList);
    m_rangeWidget = new MeasurementRangeWidget(&m_events, m_measurement);
    m_rangeWidget->setRange(m_spectrum->rangeMin, m_spectrum->rangeMax);
    layout->addWidget(new QLabel("Part of the measurement"));
    layout->addWidget(m_rangeWidget);
    connect(m_selectionList, &QListWidget::itemChanged, this, &EnergySpectrumView::selectionItemChanged);
    connect(m_rangeWidget, &MeasurementRangeWidget::rangeEdited, this, [this](double min, double max) {
        m_spectrum->rangeMin = min;
        m_spectrum->rangeMax = max;
        recalculate();
        emit modified();
    });
    return panel;
}

void EnergySpectrumView::updateSelectionList() {
    const QSignalBlocker blocker(m_selectionList);
    m_selectionList->clear();
    for(const Cut &cut : m_measurement->cuts) {
        if(!isAnalyzed(cut)) {
            continue;
        }
        auto *item = new QListWidgetItem(colorSwatch(cut.color), cutLabel(cut));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(m_spectrum->cutIds.contains(cut.id) ? Qt::Checked : Qt::Unchecked);
        item->setData(CutIdRole, cut.id);
        m_selectionList->addItem(item);
    }
}

void EnergySpectrumView::selectionItemChanged(QListWidgetItem *item) {
    const int id = item->data(CutIdRole).toInt();
    m_spectrum->cutIds.removeAll(id);
    if(item->checkState() == Qt::Checked) {
        m_spectrum->cutIds.append(id);
    }
    // Not in the handler of the list: after the project has been modified, the list is rebuilt, which deletes the item
    // being changed
    QTimer::singleShot(0, this, [this]() {
        recalculate();
        emit modified();
    });
}

void EnergySpectrumView::recalculate() {
    if(!m_plot) {
        return;
    }
    // The counts change with the bin width and the selections, so the counts axis is scaled to show them all
    const auto viewChange = m_result.histograms.empty() ? SpectrumPlotWidget::ViewChange::ShowAll
                                                        : SpectrumPlotWidget::ViewChange::KeepEnergyRange;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_calculatedInputs = energySpectrumInputs(*m_measurement, *m_spectrum);
    m_result = calculateEnergySpectrum(m_events, m_calculatedInputs);
    QApplication::restoreOverrideCursor();
    m_errorLabel->setText(m_result.errorMessage);
    m_errorLabel->setVisible(!m_result.isValid());
    updateCurves(viewChange);
    setStale(false);
}

void EnergySpectrumView::checkStale() {
    if(!m_plot) {
        return;
    }
    // Selections may have been added, removed or recolored, which only needs the list and the curves updated
    updateSelectionList();
    m_rangeWidget->refresh();
    updateCurves(SpectrumPlotWidget::ViewChange::Keep);
    setStale(energySpectrumInputs(*m_measurement, *m_spectrum) != m_calculatedInputs);
}

void EnergySpectrumView::updateCurves(SpectrumPlotWidget::ViewChange viewChange) {
    QList<SpectrumPlotWidget::Curve> curves;
    for(const EnergyHistogram &histogram : m_result.histograms) {
        const Cut *cut = m_measurement->cut(histogram.cutId);
        curves.append({cutLabel(histogram.z, histogram.a, histogram.type), cut ? cut->color : QColor(Qt::gray),
                       histogram.events, {histogram.counts.begin(), histogram.counts.end()}});
    }
    m_plot->setCurves(m_result.binWidth / Units::Megaelectronvolt, curves, viewChange);
}

void EnergySpectrumView::setStale(bool stale) {
    m_plot->setStale(stale);
    m_staleBar->setVisible(stale);
    if(stale != m_stale) {
        m_stale = stale;
        emit titleChanged();
    }
}

void EnergySpectrumView::exportSpectrum() {
    if(!m_plot) {
        return;
    }
    if(m_stale) {
        const auto answer = QMessageBox::question(this, "Export Energy Spectrum",
                                                  "The spectrum is out of date. Refresh it before exporting?",
                                                  QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if(answer == QMessageBox::Cancel) {
            return;
        }
        if(answer == QMessageBox::Yes) {
            recalculate();
        }
    }
    if(!m_result.isValid() || m_result.histograms.empty()) {
        QMessageBox::warning(this, "Export Energy Spectrum", "There is no spectrum to export.");
        return;
    }
    auto number = [](double value) { return QString::number(value, 'g', 10); };
    ExportTable table;
    table.comments.append(QString("JaRA energy spectrum: %1 / %2 (%3)").arg(m_measurement->name, m_spectrum->name,
                                                                         m_measurement->path));
    QString parameters = QString("ToF length %1 m, slope %2 s/ch, offset %3 s; ")
                                 .arg(number(m_calculatedInputs.tofLength), number(m_calculatedInputs.tofSlope),
                                      number(m_calculatedInputs.tofOffset));
    if(m_calculatedInputs.beamZ > 0) {
        parameters += QString("incident ion %1, the mass of scattered ions (*); ")
                              .arg(Elements::label(m_calculatedInputs.beamZ, m_calculatedInputs.beamA));
    }
    table.comments.append(parameters + QString("bin %1 MeV").arg(number(m_result.binWidth / Units::Megaelectronvolt)));
    if(m_calculatedInputs.rangeMin > 0 || m_calculatedInputs.rangeMax < 1) {
        const Cut *reference = m_measurement->referenceCut();
        table.comments.append(QString("Events of %1-%2 % of the counts of %3")
                                      .arg(number(m_calculatedInputs.rangeMin * 100), number(m_calculatedInputs.rangeMax * 100),
                                           reference ? cutLabel(*reference, false) : QStringLiteral("all events")));
    }
    if(m_stale) {
        table.comments.append(QStringLiteral("Out of date"));
    }
    // Edges and centres of the bins
    table.columns = QStringList{QStringLiteral("E min (MeV)"), QStringLiteral("E (MeV)"), QStringLiteral("E max (MeV)")};
    for(const EnergyHistogram &histogram : m_result.histograms) {
        table.columns.append(cutLabel(histogram.z, histogram.a, histogram.type, false));
    }
    const size_t bins = m_result.histograms.front().counts.size();
    for(size_t i = 0; i < bins; i++) {
        const double width = m_result.binWidth / Units::Megaelectronvolt;
        const auto bin = static_cast<double>(i);
        QStringList row{number(bin * width), number((bin + 0.5) * width), number((bin + 1) * width)};
        for(const EnergyHistogram &histogram : m_result.histograms) {
            row.append(QString::number(histogram.counts[i]));
        }
        table.rows.append(row);
    }
    const QString fileName = exportTable(this, "Export Energy Spectrum",
                                         QString("%1 %2").arg(m_measurement->name, m_spectrum->name), table);
    if(!fileName.isEmpty()) {
        m_statusLabel->setText(QString("Exported %1").arg(fileName));
    }
}
