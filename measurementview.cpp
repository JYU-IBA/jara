#include "measurementview.h"
#include "elementdialog.h"
#include "elements.h"
#include "histogramwidget.h"
#include "project.h"

#include <QActionGroup>
#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPolygonF>
#include <QPushButton>
#include <QSplitter>
#include <QToolBar>
#include <QVBoxLayout>

namespace {
const QString ZoomHint = QStringLiteral("Drag to zoom, double-click to show everything.");
const QString MoveHint = QStringLiteral("Drag to move the view.");
const QString SelectHint = QStringLiteral(
        "Click to add polygon vertices. Click the first vertex, double-click or press Enter to complete. "
        "Backspace removes the last vertex, Esc cancels.");
const QString EditHint = QStringLiteral(
        "Click a selection to edit it. Drag vertices or the whole selection, double-click an edge to add a vertex, "
        "right-click a vertex to remove it.");

size_t countEvents(const std::vector<ToFEEvent> &events, const QList<QPointF> &points) {
    const QPolygonF polygon(points);
    const QRectF bounds = polygon.boundingRect();
    size_t count = 0;
    for(const auto &event : events) {
        const QPointF point(event.tof, event.energy);
        if(bounds.contains(point) && polygon.containsPoint(point, Qt::OddEvenFill)) {
            count++;
        }
    }
    return count;
}
}

MeasurementView::MeasurementView(Measurement *measurement, QWidget *parent) : QWidget(parent), m_measurement(measurement) {
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

    auto *infoLabel = new QLabel(QString("%1 — %2 events").arg(measurement->path).arg(m_events.size()));
    infoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_histogram = new HistogramWidget;
    m_histogram->setEvents(&m_events);
    m_histogram->setCuts(&measurement->cuts);
    m_hint = ZoomHint;
    m_statusLabel = new QLabel(m_hint);

    auto *splitter = new QSplitter;
    splitter->addWidget(m_histogram);
    splitter->addWidget(createCutPanel());
    splitter->setStretchFactor(0, 1);
    splitter->setSizes({800, 220});

    layout->addWidget(createToolBar());
    layout->addWidget(infoLabel);
    layout->addWidget(splitter, 1);
    layout->addWidget(m_statusLabel);

    connect(m_histogram, &HistogramWidget::cursorInfoChanged, this, [this](const QString &text) {
        m_statusLabel->setText(text.isEmpty() ? m_hint : text);
    });
    connect(m_histogram, &HistogramWidget::polygonCompleted, this, &MeasurementView::polygonCompleted);
    connect(m_histogram, &HistogramWidget::cutClicked, this, [this](int index) { m_cutList->setCurrentRow(index); });
    connect(m_histogram, &HistogramWidget::cutEdited, this, [this]() {
        updateCutList();
        emit modified();
    });
    updateCutList();
}

QWidget *MeasurementView::createToolBar() {
    auto *toolBar = new QToolBar;
    toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    QAction *zoomIn = toolBar->addAction("Zoom In", m_histogram, &HistogramWidget::zoomIn);
    zoomIn->setShortcut(QKeySequence::ZoomIn);
    QAction *zoomOut = toolBar->addAction("Zoom Out", m_histogram, &HistogramWidget::zoomOut);
    zoomOut->setShortcut(QKeySequence::ZoomOut);
    // Shortcuts apply only in this tab, several measurements can be open
    for(QAction *action : {zoomIn, zoomOut}) {
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        addAction(action);
    }
    toolBar->addAction("Show All", m_histogram, &HistogramWidget::resetZoom);
    toolBar->addSeparator();

    auto *modes = new QActionGroup(this);
    auto addMode = [this, toolBar, modes](const QString &text, HistogramWidget::Mode mode, const QString &hint) {
        QAction *action = toolBar->addAction(text);
        action->setCheckable(true);
        modes->addAction(action);
        connect(action, &QAction::triggered, this, [this, mode, hint]() {
            m_histogram->setMode(mode);
            m_hint = hint;
            m_statusLabel->setText(hint);
            m_histogram->setFocus();
        });
        return action;
    };
    addMode("Zoom Box", HistogramWidget::Mode::Zoom, ZoomHint)->setChecked(true);
    addMode("Move", HistogramWidget::Mode::Move, MoveHint);
    addMode("Select", HistogramWidget::Mode::Select, SelectHint);
    addMode("Edit", HistogramWidget::Mode::Edit, EditHint);
    return toolBar;
}

QWidget *MeasurementView::createCutPanel() {
    m_cutList = new QListWidget;
    m_changeCutButton = new QPushButton("Change Element...");
    m_removeCutButton = new QPushButton("Remove");
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_changeCutButton);
    buttons->addWidget(m_removeCutButton);
    buttons->addStretch();

    auto *panel = new QWidget;
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new QLabel("Selections"));
    layout->addWidget(m_cutList);
    layout->addLayout(buttons);

    connect(m_cutList, &QListWidget::currentRowChanged, this, [this](int row) {
        m_histogram->setHighlightedCut(row);
        m_changeCutButton->setEnabled(row >= 0);
        m_removeCutButton->setEnabled(row >= 0);
    });
    connect(m_cutList, &QListWidget::itemDoubleClicked, this, &MeasurementView::changeCutElement);
    connect(m_changeCutButton, &QPushButton::clicked, this, &MeasurementView::changeCutElement);
    connect(m_removeCutButton, &QPushButton::clicked, this, &MeasurementView::removeCut);
    return panel;
}

void MeasurementView::updateCutList() {
    const int row = m_cutList->currentRow();
    m_cutList->clear();
    for(const Cut &cut : m_measurement->cuts) {
        m_cutList->addItem(QString("%1 — %2 events").arg(Elements::displayLabel(cut.z, cut.a)).arg(countEvents(m_events, cut.polygon)));
    }
    m_cutList->setCurrentRow(std::min(row, static_cast<int>(m_cutList->count()) - 1));
    m_changeCutButton->setEnabled(m_cutList->currentRow() >= 0);
    m_removeCutButton->setEnabled(m_cutList->currentRow() >= 0);
    m_histogram->update();
}

void MeasurementView::polygonCompleted(const QList<QPointF> &polygon) {
    ElementDialog dialog(0, 0, this);
    if(dialog.exec() != QDialog::Accepted) {
        return; // The selection is discarded
    }
    m_measurement->cuts.append(Cut{dialog.z(), dialog.a(), polygon});
    updateCutList();
    m_cutList->setCurrentRow(static_cast<int>(m_measurement->cuts.size()) - 1);
    emit modified();
}

void MeasurementView::changeCutElement() {
    const int row = m_cutList->currentRow();
    if(row < 0) {
        return;
    }
    Cut &cut = m_measurement->cuts[row];
    ElementDialog dialog(cut.z, cut.a, this);
    if(dialog.exec() != QDialog::Accepted) {
        return;
    }
    cut.z = dialog.z();
    cut.a = dialog.a();
    updateCutList();
    emit modified();
}

void MeasurementView::removeCut() {
    const int row = m_cutList->currentRow();
    if(row < 0) {
        return;
    }
    const Cut &cut = m_measurement->cuts.at(row);
    if(QMessageBox::question(this, "Remove Selection", QString("Remove the %1 selection?").arg(Elements::displayLabel(cut.z, cut.a)))
       != QMessageBox::Yes) {
        return;
    }
    m_measurement->cuts.removeAt(row);
    updateCutList();
    emit modified();
}
