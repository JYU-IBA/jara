#include "measurementview.h"
#include "colorbutton.h"
#include "defaults.h"
#include "elementdialog.h"
#include "elements.h"
#include "histogramwidget.h"
#include "project.h"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPolygonF>
#include <QSplitter>
#include <QToolBar>
#include <QVBoxLayout>

namespace {
const QString ZoomHint = QStringLiteral(
        "Drag to zoom, double-click to show everything. Scroll the mouse wheel or pinch to zoom, "
        "scroll with two fingers to move.");
const QString MoveHint = QStringLiteral("Drag to move the view. Scroll the mouse wheel or pinch to zoom.");
const QString SelectHint = QStringLiteral(
        "Click to add polygon vertices. Click the first vertex, double-click or press Enter to complete. "
        "Backspace removes the last vertex, Esc cancels.");
const QString EditHint = QStringLiteral(
        "Click a selection to edit it. Drag vertices or the whole selection, double-click an edge to add a vertex, "
        "right-click a vertex to remove it.");

// Cuts in the clipboard are JSON: {"format": "jara-cuts", "version": 1, "cuts": [...]}, cuts as in project files
const QString ClipboardFormat = QStringLiteral("jara-cuts");
const QString JsonMimeType = QStringLiteral("application/json");

QList<Cut> cutsFromClipboard() {
    const QMimeData *mimeData = QGuiApplication::clipboard()->mimeData();
    if(!mimeData) {
        return {};
    }
    const QByteArray data = mimeData->hasFormat(JsonMimeType) ? mimeData->data(JsonMimeType) : mimeData->text().toUtf8();
    const QJsonObject root = QJsonDocument::fromJson(data).object();
    if(root["format"].toString() != ClipboardFormat) {
        return {};
    }
    QList<Cut> cuts = cutsFromJson(root["cuts"].toArray());
    cuts.removeIf([](const Cut &cut) { return !Elements::isKnown(cut.z) || cut.polygon.size() < 3; });
    return cuts;
}

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
    m_cutList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_cutList->setContextMenuPolicy(Qt::CustomContextMenu);

    m_copyAction = new QAction("Copy", this);
    m_copyAction->setShortcut(QKeySequence::Copy);
    m_pasteAction = new QAction("Paste", this);
    m_pasteAction->setShortcut(QKeySequence::Paste);
    for(QAction *action : {m_copyAction, m_pasteAction}) {
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut); // Several measurements can be open
        addAction(action);
    }

    auto *panel = new QWidget;
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new QLabel("Selections"));
    layout->addWidget(m_cutList);

    connect(m_copyAction, &QAction::triggered, this, &MeasurementView::copyCuts);
    connect(m_pasteAction, &QAction::triggered, this, &MeasurementView::pasteCuts);
    connect(m_cutList, &QListWidget::currentRowChanged, m_histogram, &HistogramWidget::setHighlightedCut);
    connect(m_cutList, &QListWidget::itemSelectionChanged, this, [this]() {
        m_histogram->setSelectedCuts(selectedCutRows());
    });
    connect(m_cutList, &QListWidget::itemDoubleClicked, this, &MeasurementView::changeCutElement);
    connect(m_cutList, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        // Right-clicking an unselected selection selects only it
        QListWidgetItem *item = m_cutList->itemAt(pos);
        if(item && !item->isSelected()) {
            m_cutList->setCurrentItem(item);
        }
        const qsizetype count = selectedCutRows().size();
        m_copyAction->setEnabled(count > 0);
        m_pasteAction->setEnabled(!cutsFromClipboard().isEmpty());
        QMenu menu;
        menu.addAction("Change Element...", this, &MeasurementView::changeCutElement)->setEnabled(count == 1);
        menu.addAction("Change Color...", this, &MeasurementView::changeCutColor)->setEnabled(count > 0);
        menu.addAction(count > 1 ? QString("Remove %1 Selections").arg(count) : QString("Remove"), this, &MeasurementView::removeCuts)
                ->setEnabled(count > 0);
        menu.addSeparator();
        menu.addAction(m_copyAction);
        menu.addAction(m_pasteAction);
        menu.exec(m_cutList->viewport()->mapToGlobal(pos));
        // The shortcuts stay available, they check what there is to copy or paste themselves
        m_copyAction->setEnabled(true);
        m_pasteAction->setEnabled(true);
    });
    return panel;
}

QList<int> MeasurementView::selectedCutRows() const {
    QList<int> rows;
    for(const QModelIndex &index : m_cutList->selectionModel()->selectedRows()) {
        rows.append(index.row());
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

void MeasurementView::updateCutList() {
    const int row = m_cutList->currentRow();
    m_cutList->clear();
    for(const Cut &cut : m_measurement->cuts) {
        m_cutList->addItem(new QListWidgetItem(colorSwatch(cut.color), QString("%1 — %2 events")
                                                       .arg(Elements::displayLabel(cut.z, cut.a))
                                                       .arg(countEvents(m_events, cut.polygon))));
    }
    m_cutList->setCurrentRow(std::min(row, static_cast<int>(m_cutList->count()) - 1));
    m_histogram->update();
}

void MeasurementView::polygonCompleted(const QList<QPointF> &polygon) {
    ElementDialog dialog(0, 0, this);
    dialog.setColorSelectable(QColor(), true);
    if(dialog.exec() != QDialog::Accepted) {
        return; // The selection is discarded
    }
    m_measurement->cuts.append(Cut{dialog.z(), dialog.a(), polygon, dialog.color()});
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
    // A selection with the default color of its element gets the default color of the new element
    dialog.setColorSelectable(cut.color, cut.color == Defaults::elementColor(cut.z));
    if(dialog.exec() != QDialog::Accepted) {
        return;
    }
    cut.z = dialog.z();
    cut.a = dialog.a();
    cut.color = dialog.color();
    updateCutList();
    emit modified();
}

void MeasurementView::changeCutColor() {
    const QList<int> rows = selectedCutRows();
    if(rows.isEmpty()) {
        return;
    }
    const QColor color = QColorDialog::getColor(m_measurement->cuts.at(rows.first()).color, this, "Selection Color");
    if(!color.isValid()) {
        return;
    }
    for(int row : rows) {
        m_measurement->cuts[row].color = color;
    }
    updateCutList();
    for(int row : rows) {
        m_cutList->item(row)->setSelected(true);
    }
    emit modified();
}

void MeasurementView::removeCuts() {
    const QList<int> rows = selectedCutRows();
    if(rows.isEmpty()) {
        return;
    }
    const Cut &first = m_measurement->cuts.at(rows.first());
    const QString question = rows.size() > 1 ? QString("Remove %1 selections?").arg(rows.size())
                                             : QString("Remove the %1 selection?").arg(Elements::displayLabel(first.z, first.a));
    if(QMessageBox::question(this, "Remove Selections", question) != QMessageBox::Yes) {
        return;
    }
    for(auto row = rows.crbegin(); row != rows.crend(); ++row) {
        m_measurement->cuts.removeAt(*row);
    }
    updateCutList();
    emit modified();
}

void MeasurementView::copyCuts() {
    QList<Cut> cuts;
    for(int row : selectedCutRows()) {
        cuts.append(m_measurement->cuts.at(row));
    }
    if(cuts.isEmpty()) {
        return;
    }
    const QJsonObject root{{"format", ClipboardFormat}, {"version", 1}, {"cuts", cutsToJson(cuts)}};
    const QByteArray json = QJsonDocument(root).toJson();
    auto *mimeData = new QMimeData;
    mimeData->setData(JsonMimeType, json);
    mimeData->setText(QString::fromUtf8(json));
    QGuiApplication::clipboard()->setMimeData(mimeData);
    m_statusLabel->setText(QString("Copied %1 selection(s).").arg(cuts.size()));
}

void MeasurementView::pasteCuts() {
    const QList<Cut> cuts = cutsFromClipboard();
    if(cuts.isEmpty()) {
        return;
    }
    const auto first = static_cast<int>(m_measurement->cuts.size());
    m_measurement->cuts.append(cuts);
    updateCutList();
    // Select the pasted selections
    m_cutList->setCurrentRow(first);
    for(int row = first; row < m_cutList->count(); row++) {
        m_cutList->item(row)->setSelected(true);
    }
    m_statusLabel->setText(QString("Pasted %1 selection(s).").arg(cuts.size()));
    emit modified();
}
