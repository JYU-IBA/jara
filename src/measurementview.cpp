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
#include <QSplitter>
#include <QToolBar>
#include <QVBoxLayout>

namespace {
const QString SelectHint = QStringLiteral(
        "Click to add vertices. Enter or the first vertex completes, Backspace undoes, Esc cancels.");
const QString EditHint = QStringLiteral(
        "Drag vertices or selections. Double-click an edge to add a vertex, right-click one to remove it.");

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
    cuts.removeIf([](const Cut &cut) {
        return (!Elements::isKnown(cut.z) && cut.type != CutType::Reference) || cut.polygon.size() < 3;
    });
    return cuts;
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
    m_statusLabel = new QLabel;
    m_statusLabel->setMinimumHeight(m_statusLabel->fontMetrics().height()); // Cursor info, or the hint of the mode

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
    QAction *zoomMode = addMode("Zoom Box", HistogramWidget::Mode::Zoom, {});
    zoomMode->setChecked(true);
    zoomMode->setToolTip("Drag to zoom, double-click to show everything");
    addMode("Move", HistogramWidget::Mode::Move, {})->setToolTip("Drag to move the view");
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
    connect(m_cutList, &QListWidget::itemDoubleClicked, this, &MeasurementView::editCut);
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
        menu.setToolTipsVisible(true);
        menu.addAction("Properties...", this, &MeasurementView::editCut)->setEnabled(count == 1);
        menu.addAction("Change Color...", this, &MeasurementView::changeCutColor)->setEnabled(count > 0);
        const auto &cuts = m_measurement->cuts;
        const bool custom = std::any_of(cuts.begin(), cuts.end(), [](const Cut &cut) {
            return cut.color != Defaults::elementColor(cut.z);
        });
        menu.addAction("Reset All Colors", this, &MeasurementView::resetCutColors)->setEnabled(custom);
        // Checked when all selected selections have the type. Only references can be without an element.
        auto addTypeAction = [this, &menu, count](const QString &text, CutType type) {
            const QList<int> rows = selectedCutRows();
            QAction *action = menu.addAction(text, this, [this, type]() { setCutType(type); });
            action->setCheckable(true);
            action->setChecked(count > 0 && std::all_of(rows.begin(), rows.end(), [this, type](int row) {
                                   return m_measurement->cuts.at(row).type == type;
                               }));
            action->setEnabled(count > 0 && (type == CutType::Reference || std::all_of(rows.begin(), rows.end(), [this](int row) {
                                                 return m_measurement->cuts.at(row).z > 0;
                                             })));
        };
        menu.addSeparator();
        addTypeAction("Recoil (ERD)", CutType::Recoil);
        addTypeAction("Scattered (RBS)", CutType::Scattered);
        addTypeAction("Reference", CutType::Reference);
        QAction *reference = menu.addAction("Divides the Measurement", this, [this](bool checked) {
            setReferenceCut(checked ? m_cutList->currentRow() : -1);
        });
        reference->setCheckable(true);
        reference->setEnabled(count == 1);
        reference->setChecked(count == 1 && m_measurement->cuts.at(selectedCutRows().first()).id == m_measurement->referenceCutId);
        reference->setToolTip("The counts of this selection divide the measurement into parts in split analyses and the "
                              "ranges of energy spectra and depth profiles. Without one, all events count.");
        // Checked when all selected selections represent their element, only isotopes that occur naturally can
        const QList<int> rows = selectedCutRows();
        const bool canRepresent = count > 0 && std::all_of(rows.begin(), rows.end(), [this](int row) {
            const Cut &cut = m_measurement->cuts.at(row);
            return canRepresentElement(cut.z, cut.a);
        });
        QAction *represents = menu.addAction("Represents Natural Element", this, &MeasurementView::setRepresentsElement);
        represents->setCheckable(true);
        represents->setChecked(canRepresent && std::all_of(rows.begin(), rows.end(), [this](int row) {
            return m_measurement->cuts.at(row).representsElement;
        }));
        represents->setEnabled(canRepresent);
        represents->setToolTip("In depth profiles, the events of the isotope are weighted by 1 / its natural abundance "
                               "and the profile is labeled with the element");
        menu.addSeparator();
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
        const bool divides = cut.id == m_measurement->referenceCutId;
        auto *item = new QListWidgetItem(colorSwatch(cut.color), QString("%1 — %2 events%3")
                                                                      .arg(cutLabel(cut))
                                                                      .arg(countEventsIn(m_events, cut.polygon))
                                                                      .arg(divides ? QStringLiteral(" — divides") : QString()));
        if(divides) {
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
        }
        QStringList toolTip;
        if(divides) {
            toolTip.append("Divides the measurement into parts in split analyses and the ranges of energy spectra and "
                           "depth profiles");
        }
        if(cut.representsElement && isAnalyzed(cut)) {
            toolTip.append(QString("Represents natural %1 in depth profiles, events weighted by 1 / %2%3")
                                   .arg(Elements::symbol(cut.z))
                                   .arg(cutAbundance(cut), 0, 'g', 4)
                                   .arg(cut.abundance ? QString(" (set by the user)") : QString()));
        }
        if(cut.weight != 1 && isAnalyzed(cut)) {
            toolTip.append(QString("Additional weight %1 in depth profiles").arg(cut.weight));
        }
        item->setToolTip(toolTip.join(QLatin1Char('\n')));
        m_cutList->addItem(item);
    }
    m_cutList->setCurrentRow(std::min(row, static_cast<int>(m_cutList->count()) - 1));
    m_histogram->update();
}

void MeasurementView::polygonCompleted(const QList<QPointF> &polygon) {
    ElementDialog dialog(0, 0, this);
    dialog.setWindowTitle("New Selection");
    dialog.setColorSelectable(QColor(), true);
    dialog.setTypeSelectable(CutType::Recoil);
    dialog.setCutPropertiesSelectable(false, std::nullopt, 1);
    if(dialog.exec() != QDialog::Accepted) {
        return; // The selection is discarded
    }
    Cut cut{m_measurement->newCutId(), dialog.z(), dialog.a(), polygon, dialog.color(), dialog.type()};
    cut.representsElement = dialog.representsElement();
    cut.abundance = dialog.abundance();
    cut.weight = dialog.weight();
    m_measurement->cuts.append(cut);
    // The first reference divides the measurement
    if(cut.type == CutType::Reference && !m_measurement->referenceCut()) {
        m_measurement->referenceCutId = cut.id;
    }
    updateCutList();
    m_cutList->setCurrentRow(static_cast<int>(m_measurement->cuts.size()) - 1);
    emit modified();
}

void MeasurementView::editCut() {
    const int row = m_cutList->currentRow();
    if(row < 0) {
        return;
    }
    Cut &cut = m_measurement->cuts[row];
    ElementDialog dialog(cut.z, cut.a, this);
    dialog.setWindowTitle(QString("%1 Selection").arg(cutLabel(cut)));
    // A selection with the default color of its element gets the default color of the new element
    dialog.setColorSelectable(cut.color, cut.color == Defaults::elementColor(cut.z));
    dialog.setTypeSelectable(cut.type);
    dialog.setCutPropertiesSelectable(cut.representsElement, cut.abundance, cut.weight);
    if(dialog.exec() != QDialog::Accepted) {
        return;
    }
    cut.z = dialog.z();
    cut.a = dialog.a();
    cut.color = dialog.color();
    cut.type = dialog.type();
    cut.representsElement = dialog.representsElement();
    cut.abundance = dialog.abundance();
    cut.weight = dialog.weight();
    if(cut.type == CutType::Reference && !m_measurement->referenceCut()) {
        m_measurement->referenceCutId = cut.id;
    }
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

void MeasurementView::resetCutColors() {
    const QList<int> rows = selectedCutRows();
    for(Cut &cut : m_measurement->cuts) {
        cut.color = Defaults::elementColor(cut.z);
    }
    updateCutList();
    for(int row : rows) {
        m_cutList->item(row)->setSelected(true);
    }
    emit modified();
}

void MeasurementView::setRepresentsElement(bool represents) {
    const QList<int> rows = selectedCutRows();
    for(int row : rows) {
        Cut &cut = m_measurement->cuts[row];
        cut.representsElement = represents && canRepresentElement(cut.z, cut.a);
    }
    updateCutList();
    for(int row : rows) {
        m_cutList->item(row)->setSelected(true);
    }
    emit modified();
}

void MeasurementView::setCutType(CutType type) {
    const QList<int> rows = selectedCutRows();
    for(int row : rows) {
        Cut &cut = m_measurement->cuts[row];
        if(cut.z > 0 || type == CutType::Reference) { // Only references can be without an element
            cut.type = type;
        }
    }
    if(type == CutType::Reference && !m_measurement->referenceCut() && !rows.isEmpty()) {
        m_measurement->referenceCutId = m_measurement->cuts.at(rows.first()).id;
    }
    updateCutList();
    for(int row : rows) {
        m_cutList->item(row)->setSelected(true);
    }
    emit modified();
}

void MeasurementView::setReferenceCut(int row) {
    m_measurement->referenceCutId = row >= 0 ? m_measurement->cuts.at(row).id : 0;
    updateCutList();
    emit modified();
}

void MeasurementView::removeCuts() {
    const QList<int> rows = selectedCutRows();
    if(rows.isEmpty()) {
        return;
    }
    const Cut &first = m_measurement->cuts.at(rows.first());
    const QString question = rows.size() > 1 ? QString("Remove %1 selections?").arg(rows.size())
                                             : QString("Remove the %1 selection?").arg(cutLabel(first));
    if(QMessageBox::question(this, "Remove Selections", question) != QMessageBox::Yes) {
        return;
    }
    for(auto row = rows.crbegin(); row != rows.crend(); ++row) {
        m_measurement->removeCut(*row);
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
    for(Cut cut : cuts) {
        cut.id = m_measurement->newCutId(); // The ids of the copied cuts belong to their measurement
        cut.tofEdge.reset(); // Found in the data of the measurement the cut was copied from
        m_measurement->cuts.append(cut);
    }
    updateCutList();
    // Select the pasted selections
    m_cutList->setCurrentRow(first);
    for(int row = first; row < m_cutList->count(); row++) {
        m_cutList->item(row)->setSelected(true);
    }
    m_statusLabel->setText(QString("Pasted %1 selection(s).").arg(cuts.size()));
    emit modified();
}
