#include "energyspectrumview.h"
#include "colorbutton.h"
#include "elements.h"
#include "project.h"

#include <QActionGroup>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QTextStream>
#include <QToolBar>
#include <QVBoxLayout>

namespace {
const QString ZoomHint = QStringLiteral(
        "Drag to zoom, double-click to show everything. Scroll the mouse wheel or pinch to zoom, "
        "scroll with two fingers to move.");
const QString MoveHint = QStringLiteral("Drag to move the view. Scroll the mouse wheel or pinch to zoom.");
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
    m_hint = ZoomHint;
    m_statusLabel = new QLabel(m_hint);
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
    addMode("Zoom Box", SpectrumPlotWidget::Mode::Zoom, ZoomHint)->setChecked(true);
    addMode("Move", SpectrumPlotWidget::Mode::Move, MoveHint);
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
    auto *label = new QLabel("The spectrum is out of date: the selections, the ToF calibration, the timing detector foil or the incident ion have changed since it was calculated.");
    label->setWordWrap(true);
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
    connect(m_selectionList, &QListWidget::itemChanged, this, &EnergySpectrumView::selectionItemChanged);
    return panel;
}

void EnergySpectrumView::updateSelectionList() {
    const QSignalBlocker blocker(m_selectionList);
    m_selectionList->clear();
    for(const Cut &cut : m_measurement->cuts) {
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
    recalculate();
    emit modified();
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
    updateCurves(SpectrumPlotWidget::ViewChange::Keep);
    setStale(energySpectrumInputs(*m_measurement, *m_spectrum) != m_calculatedInputs);
}

void EnergySpectrumView::updateCurves(SpectrumPlotWidget::ViewChange viewChange) {
    QList<SpectrumPlotWidget::Curve> curves;
    for(const EnergyHistogram &histogram : m_result.histograms) {
        const Cut *cut = m_measurement->cut(histogram.cutId);
        curves.append({cutLabel(histogram.z, histogram.a, histogram.type), cut ? cut->color : QColor(Qt::gray),
                       histogram.events, histogram.counts});
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
    const QString fileName = QFileDialog::getSaveFileName(this, "Export Energy Spectrum",
                                                          QString("%1 %2.txt").arg(m_measurement->name, m_spectrum->name),
                                                          "Text files (*.txt);;All files (*)");
    if(fileName.isEmpty()) {
        return;
    }
    // Numbers are written without the locale, so that other programs can read them
    QString text;
    QTextStream out(&text);
    out << "# JaRA energy spectrum\n";
    out << "# Measurement: " << m_measurement->name << " (" << m_measurement->path << ")\n";
    out << "# Spectrum: " << m_spectrum->name << "\n";
    out << "# ToF length " << QString::number(m_calculatedInputs.tofLength, 'g', 10) << " m, slope "
        << QString::number(m_calculatedInputs.tofSlope, 'g', 10) << " s/ch, offset "
        << QString::number(m_calculatedInputs.tofOffset, 'g', 10) << " s\n";
    if(m_calculatedInputs.beamZ > 0) {
        out << "# Incident ion " << Elements::label(m_calculatedInputs.beamZ, m_calculatedInputs.beamA)
            << ", its mass is used for scattered ions (marked with *)\n";
    }
    out << "# Bin width " << QString::number(m_result.binWidth / Units::Megaelectronvolt, 'g', 10) << " MeV\n";
    out << "# Events are spread uniformly within their ToF channel before the conversion to energy\n";
    if(m_stale) {
        out << "# Out of date: the selections, the ToF calibration or the incident ion have changed since the spectrum was calculated\n";
    }
    out << "# E (MeV, bin centre)";
    for(const EnergyHistogram &histogram : m_result.histograms) {
        out << "\t" << cutLabel(histogram.z, histogram.a, histogram.type, false);
    }
    out << "\n";
    const size_t bins = m_result.histograms.front().counts.size();
    for(size_t i = 0; i < bins; i++) {
        out << QString::number((static_cast<double>(i) + 0.5) * m_result.binWidth / Units::Megaelectronvolt, 'g', 10);
        for(const EnergyHistogram &histogram : m_result.histograms) {
            out << "\t" << histogram.counts[i];
        }
        out << "\n";
    }
    QSaveFile file(fileName);
    if(!file.open(QIODevice::WriteOnly) || file.write(text.toUtf8()) < 0 || !file.commit()) {
        QMessageBox::critical(this, "Export Energy Spectrum", QString("Could not write %1:\n%2").arg(fileName, file.errorString()));
        return;
    }
    m_statusLabel->setText(QString("Exported %1").arg(fileName));
}
