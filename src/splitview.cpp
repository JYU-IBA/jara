#include "splitview.h"
#include "colorbutton.h"
#include "project.h"
#include "tableexport.h"

#include <cmath>

#include <QActionGroup>
#include <QApplication>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
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
    m_plot->setLegendCorner(legendCorner(m_split->legendPosition));
    connect(m_plot, &SpectrumPlotWidget::legendCornerChanged, this, [this](Qt::Corner corner) {
        m_split->legendPosition = legendPosition(corner);
        emit modified();
    });
    m_statusLabel = new QLabel;
    m_statusLabel->setMinimumHeight(m_statusLabel->fontMetrics().height()); // Cursor info, or the hint of the mode
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
    QAction *zoomMode = addMode("Zoom Box", SpectrumPlotWidget::Mode::Zoom, {});
    zoomMode->setChecked(true);
    zoomMode->setToolTip("Drag to zoom, double-click to show everything");
    addMode("Move", SpectrumPlotWidget::Mode::Move, {})->setToolTip("Drag to move the view");
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
    // Not in the handler of the list: after the project has been modified, the list is rebuilt, which deletes the item
    // being changed
    QTimer::singleShot(0, this, [this]() {
        recalculate(); // Shows everything, the counts axis is scaled to the selections shown
        emit modified();
    });
}

QString SplitView::referenceText() const {
    const Cut *reference = m_measurement->referenceCut();
    return reference ? cutLabel(*reference) : QStringLiteral("all events");
}

// "12345 reference counts of ¹H in 10 parts, 1234–1235 counts per part"
QString SplitView::partsText() const {
    const QString perPart = m_result.minPartReferenceCounts == m_result.maxPartReferenceCounts
            ? QString::number(m_result.maxPartReferenceCounts)
            : QString("%1–%2").arg(m_result.minPartReferenceCounts).arg(m_result.maxPartReferenceCounts);
    return QString("%1 reference counts of %2 in %3 parts, %4 counts per part")
            .arg(m_result.referenceCounts)
            .arg(referenceText())
            .arg(m_result.parts)
            .arg(perPart);
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
    // Only when there is something to do about it
    m_referenceLabel->setText(QStringLiteral("All events divide the measurement. Choose a selection for it in the "
                                             "measurement tab."));
    m_referenceLabel->setVisible(!m_measurement->referenceCut());
    updatePlot(SpectrumPlotWidget::ViewChange::ShowAll);
    updateTable();
}

void SplitView::updatePlot(SpectrumPlotWidget::ViewChange viewChange) {
    SpectrumPlotWidget::YAxis yAxis;
    yAxis.title = m_split->relative ? QStringLiteral("Counts / mean") : QStringLiteral("Counts per part");
    yAxis.maxUnit = m_split->relative ? QString() : QStringLiteral("counts");
    yAxis.logFloor = m_split->relative ? 0.01 : 0.5;
    yAxis.legendEvents = false; // The counts and the fits are in the table
    yAxis.includeZero = false; // Changes between the parts are small compared with the counts
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
        curves.append(curve);
    }
    m_plot->setCurves(100.0 / m_result.parts, curves, viewChange);
    m_plot->setInfoText(partsText());
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
    auto number = [](double value, int precision = 10) { return QString::number(value, 'g', precision); };
    ExportTable table;
    table.comments.append(QString("JaRA split analysis: %1 / %2 (%3)").arg(m_measurement->name, m_split->name,
                                                                         m_measurement->path));
    table.comments.append(partsText() + QStringLiteral("; lines fitted to the counts against the reference counts (%), "
                                                       "change over the measurement relative to the mean"));
    QStringList changes;
    for(const SplitCounts &counts : m_result.selections) {
        const Cut *cut = m_measurement->cut(counts.cutId);
        changes.append(QString("%1 %2 +- %3").arg(cut ? cutLabel(*cut, false) : QString(),
                                                   number(counts.fit.relativeChange(), 3),
                                                   number(counts.fit.relativeChangeUncertainty(), 2)));
    }
    table.comments.append(QStringLiteral("Change (%): ") + changes.join(QStringLiteral(", ")));
    table.columns = QStringList{QStringLiteral("Part"), QStringLiteral("Reference counts (%)")};
    for(const SplitCounts &counts : m_result.selections) {
        const Cut *cut = m_measurement->cut(counts.cutId);
        table.columns.append(cut ? cutLabel(*cut, false) : QString());
    }
    for(int i = 0; i < m_result.parts; i++) {
        QStringList row{QString::number(i + 1), number(m_result.partMiddle(i))};
        for(const SplitCounts &counts : m_result.selections) {
            row.append(QString::number(counts.counts[static_cast<size_t>(i)]));
        }
        table.rows.append(row);
    }
    const QString fileName = exportTable(this, "Export Split Analysis",
                                         QString("%1 %2").arg(m_measurement->name, m_split->name), table);
    if(!fileName.isEmpty()) {
        m_statusLabel->setText(QString("Exported %1").arg(fileName));
    }
}
