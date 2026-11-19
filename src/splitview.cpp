#include "splitview.h"
#include "colorbutton.h"
#include "project.h"

#include <cmath>

#include <QActionGroup>
#include <QApplication>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QSaveFile>
#include <QSpinBox>
#include <QSplitter>
#include <QTextStream>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
const QString ZoomHint = QStringLiteral(
        "Drag to zoom, double-click to show everything. Scroll the mouse wheel or pinch to zoom, "
        "scroll with two fingers to move.");
const QString MoveHint = QStringLiteral("Drag to move the view. Scroll the mouse wheel or pinch to zoom.");
constexpr int CutIdRole = Qt::UserRole;

enum TableColumn { SelectionColumn, CountsColumn, MeanColumn, ChangeColumn, StartColumn, ColumnCount };

// "-3.2 ± 0.8"
QString valueWithUncertainty(double value, double uncertainty) {
    return QString("%1 ± %2").arg(value, 0, 'f', 1).arg(uncertainty, 0, 'f', 1);
}
}

SplitView::SplitView(Measurement *measurement, SplitAnalysis *split, QWidget *parent)
    : QWidget(parent), m_measurement(measurement), m_split(split) {
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
    SpectrumPlotWidget::XAxis xAxis;
    xAxis.title = QStringLiteral("Reference counts (%)");
    xAxis.unit = QStringLiteral("%");
    xAxis.decimals = 1;
    m_plot->setXAxis(xAxis);
    m_hint = ZoomHint;
    m_statusLabel = new QLabel(m_hint);
    m_referenceLabel = new QLabel;
    m_referenceLabel->setWordWrap(true);
    m_errorLabel = new QLabel;
    m_errorLabel->setStyleSheet("color: red");
    m_errorLabel->setWordWrap(true);
    m_errorLabel->hide();

    m_table = new QTreeWidget;
    m_table->setRootIsDecorated(false);
    m_table->setColumnCount(ColumnCount);
    m_table->setHeaderLabels({"Selection", "Counts", "Mean per part", "Change over the measurement (%)",
                              "Fit at 0 % / mean"});
    m_table->headerItem()->setToolTip(ChangeColumn, "Slope of the line fitted to the counts of the parts times 100 %, "
                                                    "relative to the mean, with its standard deviation");
    m_table->headerItem()->setToolTip(StartColumn, "The fitted line at the start of the measurement relative to the "
                                                   "mean, e.g. to correct for losses during the measurement");
    m_table->header()->setSectionResizeMode(QHeaderView::ResizeToContents);

    auto *plotSplitter = new QSplitter(Qt::Vertical);
    plotSplitter->addWidget(m_plot);
    plotSplitter->addWidget(m_table);
    plotSplitter->setStretchFactor(0, 1);
    plotSplitter->setSizes({600, 160});

    auto *splitter = new QSplitter;
    splitter->addWidget(plotSplitter);
    splitter->addWidget(createSelectionPanel());
    splitter->setStretchFactor(0, 1);
    splitter->setSizes({800, 220});

    layout->addWidget(createToolBar());
    layout->addWidget(m_referenceLabel);
    layout->addWidget(m_errorLabel);
    layout->addWidget(splitter, 1);
    layout->addWidget(m_statusLabel);

    connect(m_plot, &SpectrumPlotWidget::cursorInfoChanged, this, [this](const QString &text) {
        m_statusLabel->setText(text.isEmpty() ? m_hint : text);
    });
    updateSelectionList();
    recalculate();
}

QString SplitView::title() const {
    return QString("%1 — %2").arg(m_measurement->name, m_split->name);
}

QWidget *SplitView::createToolBar() {
    auto *toolBar = new QToolBar;
    toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    QAction *zoomIn = toolBar->addAction("Zoom In", m_plot, &SpectrumPlotWidget::zoomIn);
    zoomIn->setShortcut(QKeySequence::ZoomIn);
    QAction *zoomOut = toolBar->addAction("Zoom Out", m_plot, &SpectrumPlotWidget::zoomOut);
    zoomOut->setShortcut(QKeySequence::ZoomOut);
    // Shortcuts apply only in this tab, several analyses can be open
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

    QAction *relative = toolBar->addAction("Relative to Mean");
    relative->setCheckable(true);
    relative->setChecked(m_split->relative);
    relative->setToolTip("The counts of each selection divided by its mean count per part");
    connect(relative, &QAction::toggled, this, [this](bool checked) {
        m_split->relative = checked;
        updatePlot(SpectrumPlotWidget::ViewChange::ShowAll);
        emit modified();
    });
    toolBar->addSeparator();

    m_partsSpin = new QSpinBox;
    m_partsSpin->setRange(MinSplitParts, MaxSplitParts);
    m_partsSpin->setKeyboardTracking(false); // Recalculated when the value is entered, not on every key
    m_partsSpin->setValue(m_split->parts);
    toolBar->addWidget(new QLabel("Parts: "));
    toolBar->addWidget(m_partsSpin);
    connect(m_partsSpin, &QSpinBox::valueChanged, this, [this](int value) {
        m_split->parts = value;
        refresh();
        emit modified();
    });
    toolBar->addSeparator();
    toolBar->addAction("Export...", this, &SplitView::exportSplit);
    return toolBar;
}

QWidget *SplitView::createSelectionPanel() {
    m_selectionList = new QListWidget;
    auto *panel = new QWidget;
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new QLabel("Selections"));
    layout->addWidget(m_selectionList);
    connect(m_selectionList, &QListWidget::itemChanged, this, &SplitView::selectionItemChanged);
    return panel;
}

void SplitView::updateSelectionList() {
    const QSignalBlocker blocker(m_selectionList);
    m_selectionList->clear();
    for(const Cut &cut : m_measurement->cuts) {
        auto *item = new QListWidgetItem(colorSwatch(cut.color), cutLabel(cut));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(m_split->cutIds.contains(cut.id) ? Qt::Checked : Qt::Unchecked);
        item->setData(CutIdRole, cut.id);
        m_selectionList->addItem(item);
    }
}

void SplitView::selectionItemChanged(QListWidgetItem *item) {
    const int id = item->data(CutIdRole).toInt();
    m_split->cutIds.removeAll(id);
    if(item->checkState() == Qt::Checked) {
        m_split->cutIds.append(id);
    }
    refresh();
    emit modified();
}

QString SplitView::referenceText() const {
    const Cut *reference = m_measurement->referenceCut();
    return reference ? cutLabel(*reference) : QStringLiteral("all events");
}

void SplitView::refresh() {
    if(!m_plot) {
        return;
    }
    // Selections may have been added, removed or recolored
    updateSelectionList();
    if(splitInputs(*m_measurement, *m_split) != m_calculatedInputs || m_result.selections.empty()) {
        recalculate();
    } else {
        updatePlot(SpectrumPlotWidget::ViewChange::Keep);
        updateTable();
    }
}

void SplitView::recalculate() {
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_calculatedInputs = splitInputs(*m_measurement, *m_split);
    m_result = calculateSplit(m_events, m_calculatedInputs);
    QApplication::restoreOverrideCursor();
    m_errorLabel->setText(m_result.errorMessage);
    m_errorLabel->setVisible(!m_result.isValid());
    QString text = QString("Divided by %1: %2 counts, %3 per part.")
                           .arg(referenceText())
                           .arg(m_result.referenceCounts)
                           .arg(static_cast<double>(m_result.referenceCounts) / m_split->parts, 0, 'f', 1);
    if(!m_measurement->referenceCut()) {
        text += QStringLiteral(" Choose the selection that divides the measurement in its context menu in the "
                               "measurement tab.");
    }
    m_referenceLabel->setText(text);
    updatePlot(SpectrumPlotWidget::ViewChange::ShowAll);
    updateTable();
}

void SplitView::updatePlot(SpectrumPlotWidget::ViewChange viewChange) {
    SpectrumPlotWidget::YAxis yAxis;
    yAxis.title = m_split->relative ? QStringLiteral("Counts / mean") : QStringLiteral("Counts per part");
    yAxis.maxUnit = m_split->relative ? QString() : QStringLiteral("counts");
    yAxis.logFloor = m_split->relative ? 0.01 : 0.5;
    m_plot->setYAxis(yAxis);
    if(!m_result.isValid()) {
        m_plot->setBinEdges({});
        m_plot->setCurves(1, {}, viewChange);
        m_plot->setInfoText({});
        return;
    }
    std::vector<double> edges;
    for(int i = 0; i <= m_result.parts; i++) {
        edges.push_back(100.0 * i / m_result.parts);
    }
    m_plot->setBinEdges(edges);
    QList<SpectrumPlotWidget::Curve> curves;
    for(const SplitCounts &counts : m_result.selections) {
        const Cut *cut = m_measurement->cut(counts.cutId);
        if(!cut) {
            continue;
        }
        const double scale = m_split->relative && counts.fit.mean > 0 ? 1 / counts.fit.mean : 1;
        SpectrumPlotWidget::Curve curve{cutLabel(*cut), cut->color, counts.total, {}};
        for(const size_t count : counts.counts) {
            curve.values.push_back(static_cast<double>(count) * scale);
            curve.errors.push_back(std::sqrt(static_cast<double>(count)) * scale);
        }
        curve.line = {{0, counts.fit.intercept * scale}, {100, (counts.fit.intercept + 100 * counts.fit.slope) * scale}};
        curve.legendSuffix = QString(" %1 %").arg(valueWithUncertainty(counts.fit.relativeChange(),
                                                                       counts.fit.relativeChangeUncertainty()));
        curves.append(curve);
    }
    m_plot->setCurves(100.0 / m_result.parts, curves, viewChange);
    m_plot->setInfoText(QString("%1 parts of %2 counts of %3, lines fitted to the counts")
                                .arg(m_result.parts)
                                .arg(static_cast<double>(m_result.referenceCounts) / m_result.parts, 0, 'f', 1)
                                .arg(referenceText()));
}

void SplitView::updateTable() {
    m_table->clear();
    for(const SplitCounts &counts : m_result.selections) {
        const Cut *cut = m_measurement->cut(counts.cutId);
        if(!cut) {
            continue;
        }
        auto *item = new QTreeWidgetItem(m_table);
        item->setIcon(SelectionColumn, colorSwatch(cut->color));
        item->setText(SelectionColumn, cutLabel(*cut));
        item->setText(CountsColumn, QString::number(counts.total));
        item->setText(MeanColumn, QString::number(counts.fit.mean, 'f', 1));
        item->setText(ChangeColumn, valueWithUncertainty(counts.fit.relativeChange(), counts.fit.relativeChangeUncertainty()));
        item->setText(StartColumn, counts.fit.mean > 0 ? QString::number(counts.fit.intercept / counts.fit.mean, 'f', 3)
                                                       : QString());
        for(int column = CountsColumn; column < ColumnCount; column++) {
            item->setTextAlignment(column, Qt::AlignRight | Qt::AlignVCenter);
        }
    }
}

void SplitView::exportSplit() {
    if(!m_plot) {
        return;
    }
    if(!m_result.isValid() || m_result.selections.empty()) {
        QMessageBox::warning(this, "Export Split Analysis", "There is nothing to export.");
        return;
    }
    const QString fileName = QFileDialog::getSaveFileName(this, "Export Split Analysis",
                                                          QString("%1 %2.txt").arg(m_measurement->name, m_split->name),
                                                          "Text files (*.txt);;All files (*)");
    if(fileName.isEmpty()) {
        return;
    }
    // Numbers are written without the locale, so that other programs can read them
    QString text;
    QTextStream out(&text);
    out << "# JaRA split analysis\n";
    out << "# Measurement: " << m_measurement->name << " (" << m_measurement->path << ")\n";
    out << "# Split analysis: " << m_split->name << "\n";
    out << "# Divided by " << referenceText() << ": " << m_result.referenceCounts << " counts in " << m_result.parts
        << " parts\n";
    out << "# Fits: counts = intercept + slope * reference counts (%), change = 100 * (100 % * slope) / mean in %\n";
    for(const SplitCounts &counts : m_result.selections) {
        const Cut *cut = m_measurement->cut(counts.cutId);
        out << "# " << (cut ? cutLabel(*cut, false) : QString()) << ": total " << counts.total << ", mean "
            << QString::number(counts.fit.mean, 'g', 10) << ", intercept " << QString::number(counts.fit.intercept, 'g', 10)
            << ", slope " << QString::number(counts.fit.slope, 'g', 10) << " ± "
            << QString::number(counts.fit.slopeUncertainty, 'g', 10) << " /%, change "
            << QString::number(counts.fit.relativeChange(), 'g', 6) << " ± "
            << QString::number(counts.fit.relativeChangeUncertainty(), 'g', 6) << " %\n";
    }
    out << "# Part\tReference counts (%, middle)";
    for(const SplitCounts &counts : m_result.selections) {
        const Cut *cut = m_measurement->cut(counts.cutId);
        out << "\t" << (cut ? cutLabel(*cut, false) : QString());
    }
    out << "\n";
    for(int i = 0; i < m_result.parts; i++) {
        out << i + 1 << "\t" << QString::number(m_result.partMiddle(i), 'g', 10);
        for(const SplitCounts &counts : m_result.selections) {
            out << "\t" << counts.counts[static_cast<size_t>(i)];
        }
        out << "\n";
    }
    QSaveFile file(fileName);
    if(!file.open(QIODevice::WriteOnly) || file.write(text.toUtf8()) < 0 || !file.commit()) {
        QMessageBox::critical(this, "Export Split Analysis", QString("Could not write %1:\n%2").arg(fileName, file.errorString()));
        return;
    }
    m_statusLabel->setText(QString("Exported %1").arg(fileName));
}
