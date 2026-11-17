#include "projectpanel.h"
#include "defaults.h"
#include "project.h"
#include "settingseditors.h"

#include <algorithm>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTableWidget>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {
// Experimental settings are the first top-level nodes, then detector settings and samples. Under a sample node,
// measurements come first, followed by attached files.
enum NodeType { SampleNode, MeasurementNode, FileNode, SettingsNode, DetectorNode };
enum Page { EmptyPage, SamplePage, MeasurementPage, FilePage, SettingsPage, DetectorPage };
constexpr int TypeRole = Qt::UserRole;
constexpr int IndexRole = Qt::UserRole + 1; // Index in the sample's measurements or files
constexpr int MeasurementRole = Qt::UserRole; // In the list of measurements assigned to experimental settings

QString displayName(const QString &name) {
    return name.isEmpty() ? QStringLiteral("(unnamed)") : name;
}

NodeType nodeType(const QTreeWidgetItem *item) {
    return static_cast<NodeType>(item->data(0, TypeRole).toInt());
}

int nodeIndex(const QTreeWidgetItem *item) {
    return item->data(0, IndexRole).toInt();
}

QTreeWidgetItem *topLevelOf(QTreeWidgetItem *item) {
    while(item->parent()) {
        item = item->parent();
    }
    return item;
}

QTreeWidgetItem *createItem(const QString &text, NodeType type, int index, const QString &path, QStyle::StandardPixmap icon) {
    auto *item = new QTreeWidgetItem({text});
    item->setData(0, TypeRole, type);
    item->setData(0, IndexRole, index);
    item->setIcon(0, QApplication::style()->standardIcon(icon));
    item->setToolTip(0, path);
    if(!QFileInfo::exists(path)) {
        item->setForeground(0, Qt::red);
        item->setToolTip(0, path + " (missing)");
    }
    return item;
}

QTreeWidgetItem *createMeasurementItem(const Measurement &measurement, int index) {
    return createItem(measurement.name, MeasurementNode, index, measurement.path, QStyle::SP_FileDialogContentsView);
}

QTreeWidgetItem *createFileItem(const AttachedFile &file, int index) {
    return createItem(QFileInfo(file.path).fileName(), FileNode, index, file.path, QStyle::SP_FileIcon);
}

// Renumbers the nodes of one type after one of them has been removed
void renumberChildren(QTreeWidgetItem *parent, NodeType type) {
    int index = 0;
    for(int i = 0; i < parent->childCount(); i++) {
        if(nodeType(parent->child(i)) == type) {
            parent->child(i)->setData(0, IndexRole, index++);
        }
    }
}

QWidget *createPage(QLayout *layout) {
    auto *page = new QWidget;
    page->setLayout(layout);
    return page;
}

QLabel *pathLabel() {
    auto *label = new QLabel;
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

constexpr auto FromSurface = "Measured from the sample surface";
const QString JsonMimeType = QStringLiteral("application/json");
const QString SettingsFileFilter = QStringLiteral("JSON files (*.json);;All files (*)");

// Lets the user pick which settings sets to import, removes the others from the collection
bool chooseSettings(QWidget *parent, SettingsCollection &collection) {
    QDialog dialog(parent);
    dialog.setWindowTitle("Import Settings");
    auto *list = new QListWidget;
    auto addItems = [list](const QString &kind, const QStringList &names) {
        for(const QString &name : names) {
            auto *item = new QListWidgetItem(QString("%1: %2").arg(kind, displayName(name)));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Checked);
            list->addItem(item);
        }
    };
    QStringList experimentalNames, detectorNames;
    for(const auto &settings : collection.experimental) {
        experimentalNames.append(settings.name);
    }
    for(const auto &detector : collection.detectors) {
        detectorNames.append(detector.name);
    }
    addItems("Experimental settings", experimentalNames);
    addItems("Detector settings", detectorNames);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel("Settings to import:"));
    layout->addWidget(list);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if(dialog.exec() != QDialog::Accepted) {
        return false;
    }
    SettingsCollection chosen;
    for(int i = 0; i < list->count(); i++) {
        if(list->item(i)->checkState() != Qt::Checked) {
            continue;
        }
        if(i < collection.experimental.size()) {
            chosen.experimental.append(collection.experimental.at(i));
        } else {
            chosen.detectors.append(collection.detectors.at(i - collection.experimental.size()));
        }
    }
    collection = chosen;
    return true;
}
}

ProjectPanel::ProjectPanel(QWidget *parent) : QWidget(parent) {
    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);

    auto *splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(m_tree);
    splitter->addWidget(createPropertyPages());
    splitter->setStretchFactor(0, 1);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);

    connect(m_tree, &QTreeWidget::currentItemChanged, this, &ProjectPanel::currentItemChanged);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &ProjectPanel::itemDoubleClicked);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &ProjectPanel::showContextMenu);

    m_copySettingsAction = new QAction("Copy", this);
    m_copySettingsAction->setShortcut(QKeySequence::Copy);
    m_pasteSettingsAction = new QAction("Paste Settings", this);
    m_pasteSettingsAction->setShortcut(QKeySequence::Paste);
    for(QAction *action : {m_copySettingsAction, m_pasteSettingsAction}) {
        action->setShortcutContext(Qt::WidgetShortcut);
        m_tree->addAction(action);
    }
    connect(m_copySettingsAction, &QAction::triggered, this, &ProjectPanel::copySettings);
    connect(m_pasteSettingsAction, &QAction::triggered, this, &ProjectPanel::pasteSettings);
}

QWidget *ProjectPanel::createPropertyPages() {
    m_pages = new QStackedWidget;

    auto *emptyLayout = new QVBoxLayout;
    emptyLayout->addStretch();
    m_pages->insertWidget(EmptyPage, createPage(emptyLayout));

    m_sampleNameEdit = new QLineEdit;
    m_sampleDescriptionEdit = new QPlainTextEdit;
    m_metadataTable = new QTableWidget(0, 2);
    m_metadataTable->setHorizontalHeaderLabels({"Key", "Value"});
    m_metadataTable->horizontalHeader()->setStretchLastSection(true);
    m_metadataTable->verticalHeader()->hide();
    m_metadataTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_metadataTable->setContextMenuPolicy(Qt::CustomContextMenu);
    auto *metadataGroup = new QGroupBox("Metadata");
    auto *metadataLayout = new QVBoxLayout(metadataGroup);
    metadataLayout->addWidget(m_metadataTable);
    auto *sampleLayout = new QFormLayout;
    sampleLayout->addRow("Name:", m_sampleNameEdit);
    sampleLayout->addRow("Description:", m_sampleDescriptionEdit);
    sampleLayout->addRow(metadataGroup);
    m_pages->insertWidget(SamplePage, createPage(sampleLayout));

    m_measurementNameEdit = new QLineEdit;
    m_measurementDescriptionEdit = new QLineEdit;
    m_measurementPathLabel = pathLabel();
    m_measurementSettingsCombo = new QComboBox;
    m_measurementDetectorCombo = new QComboBox;
    m_exitAngleLabel = new QLabel;
    m_exitAngleLabel->setToolTip(QString("%1. Scattering angle minus incident angle (IBM geometry).").arg(FromSurface));
    auto *measurementLayout = new QFormLayout;
    measurementLayout->addRow("Name:", m_measurementNameEdit);
    measurementLayout->addRow("Description:", m_measurementDescriptionEdit);
    measurementLayout->addRow("Data file:", m_measurementPathLabel);
    measurementLayout->addRow("Experimental settings:", m_measurementSettingsCombo);
    measurementLayout->addRow("Detector settings:", m_measurementDetectorCombo);
    measurementLayout->addRow("Exit angle:", m_exitAngleLabel);
    m_pages->insertWidget(MeasurementPage, createPage(measurementLayout));

    m_filePathLabel = pathLabel();
    m_fileDescriptionEdit = new QLineEdit;
    auto *fileLayout = new QFormLayout;
    fileLayout->addRow("Path:", m_filePathLabel);
    fileLayout->addRow("Description:", m_fileDescriptionEdit);
    m_pages->insertWidget(FilePage, createPage(fileLayout));

    m_pages->insertWidget(SettingsPage, createSettingsPage());
    m_pages->insertWidget(DetectorPage, createDetectorPage());

    connect(m_sampleNameEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        QTreeWidgetItem *item = topLevelOf(m_tree->currentItem());
        sampleOf(item)->name = text;
        item->setText(0, displayName(text));
        emit modified();
    });
    connect(m_sampleDescriptionEdit, &QPlainTextEdit::textChanged, this, [this]() {
        currentSample()->description = m_sampleDescriptionEdit->toPlainText();
        emit modified();
    });
    connect(m_metadataTable, &QTableWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu;
        menu.addAction("Add Entry", this, &ProjectPanel::addMetadata);
        if(m_metadataTable->selectionModel()->hasSelection()) {
            menu.addAction("Remove Entry", this, &ProjectPanel::removeMetadata);
        }
        menu.exec(m_metadataTable->viewport()->mapToGlobal(pos));
    });
    connect(m_metadataTable, &QTableWidget::itemChanged, this, &ProjectPanel::metadataItemChanged);
    connect(m_measurementNameEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        Measurement *measurement = currentMeasurement();
        measurement->name = text;
        m_tree->currentItem()->setText(0, text);
        emit measurementRenamed(measurement);
        emit modified();
    });
    connect(m_measurementDescriptionEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        currentMeasurement()->description = text;
        emit modified();
    });
    connect(m_measurementSettingsCombo, &QComboBox::activated, this, [this]() {
        const int index = m_measurementSettingsCombo->currentData().toInt();
        Measurement *measurement = currentMeasurement();
        measurement->settings = index >= 0 ? m_project->settings(index) : nullptr;
        showExitAngle(*measurement);
        emit modified();
    });
    connect(m_measurementDetectorCombo, &QComboBox::activated, this, [this]() {
        const int index = m_measurementDetectorCombo->currentData().toInt();
        Measurement *measurement = currentMeasurement();
        measurement->detector = index >= 0 ? m_project->detector(index) : nullptr;
        showExitAngle(*measurement);
        emit modified();
    });
    connect(m_fileDescriptionEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        currentFile().description = text;
        emit modified();
    });
    return m_pages;
}

QWidget *ProjectPanel::createSettingsPage() {
    m_settingsEditor = new ExperimentalSettingsEditor;
    m_assignedList = createAssignedList();
    auto *layout = new QVBoxLayout;
    layout->addWidget(m_settingsEditor);
    layout->addWidget(new QLabel("Assigned measurements:"));
    layout->addWidget(m_assignedList);
    connect(m_settingsEditor, &ExperimentalSettingsEditor::changed, this, &ProjectPanel::modified);
    connect(m_settingsEditor, &ExperimentalSettingsEditor::nameChanged, this, [this](const QString &name) {
        m_tree->currentItem()->setText(0, displayName(name));
    });
    return createPage(layout);
}

QWidget *ProjectPanel::createDetectorPage() {
    m_detectorEditor = new DetectorSettingsEditor;
    m_detectorAssignedList = createAssignedList();
    auto *layout = new QVBoxLayout;
    layout->addWidget(m_detectorEditor);
    layout->addWidget(new QLabel("Assigned measurements:"));
    layout->addWidget(m_detectorAssignedList);
    connect(m_detectorEditor, &DetectorSettingsEditor::changed, this, &ProjectPanel::modified);
    connect(m_detectorEditor, &DetectorSettingsEditor::nameChanged, this, [this](const QString &name) {
        m_tree->currentItem()->setText(0, displayName(name));
    });
    return createPage(layout);
}

QListWidget *ProjectPanel::createAssignedList() {
    auto *list = new QListWidget;
    connect(list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        const auto *measurement = reinterpret_cast<const Measurement *>(item->data(MeasurementRole).value<quintptr>());
        if(QTreeWidgetItem *treeItem = measurementItem(measurement)) {
            m_tree->setCurrentItem(treeItem);
        }
    });
    return list;
}

void ProjectPanel::setProject(Project *project) {
    m_project = project;
    {
        const QSignalBlocker blocker(m_tree);
        m_tree->clear();
        if(project) {
            for(size_t i = 0; i < project->settingsCount(); i++) {
                m_tree->addTopLevelItem(createSettingsItem(*project->settings(i)));
            }
            for(size_t i = 0; i < project->detectorCount(); i++) {
                m_tree->addTopLevelItem(createDetectorItem(*project->detector(i)));
            }
            for(size_t i = 0; i < project->sampleCount(); i++) {
                m_tree->addTopLevelItem(createSampleItem(*project->sample(i)));
            }
            m_tree->expandAll();
            m_tree->setCurrentItem(m_tree->topLevelItem(0));
        }
    }
    currentItemChanged(m_tree->currentItem());
}

QTreeWidgetItem *ProjectPanel::createSettingsItem(const ExperimentalSettings &settings) {
    auto *item = new QTreeWidgetItem({displayName(settings.name)});
    item->setData(0, TypeRole, SettingsNode);
    item->setIcon(0, style()->standardIcon(QStyle::SP_FileDialogInfoView));
    return item;
}

QTreeWidgetItem *ProjectPanel::createDetectorItem(const DetectorSettings &detector) {
    auto *item = new QTreeWidgetItem({displayName(detector.name)});
    item->setData(0, TypeRole, DetectorNode);
    item->setIcon(0, style()->standardIcon(QStyle::SP_DriveHDIcon));
    return item;
}

QTreeWidgetItem *ProjectPanel::createSampleItem(const Sample &sample) {
    auto *item = new QTreeWidgetItem({displayName(sample.name)});
    item->setData(0, TypeRole, SampleNode);
    item->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
    for(size_t i = 0; i < sample.measurements.size(); i++) {
        item->addChild(createMeasurementItem(*sample.measurements.at(i), static_cast<int>(i)));
    }
    for(int i = 0; i < sample.files.size(); i++) {
        item->addChild(createFileItem(sample.files.at(i), i));
    }
    return item;
}

int ProjectPanel::sampleIndexOf(QTreeWidgetItem *item) const {
    if(!m_project || !item) {
        return -1;
    }
    QTreeWidgetItem *top = topLevelOf(item);
    if(nodeType(top) != SampleNode) {
        return -1;
    }
    return m_tree->indexOfTopLevelItem(top) - firstSampleRow();
}

int ProjectPanel::firstSampleRow() const {
    return static_cast<int>(m_project->settingsCount() + m_project->detectorCount());
}

Sample *ProjectPanel::sampleOf(QTreeWidgetItem *item) const {
    const int index = sampleIndexOf(item);
    return index >= 0 ? m_project->sample(index) : nullptr;
}

ExperimentalSettings *ProjectPanel::settingsOf(QTreeWidgetItem *item) const {
    if(!m_project || !item || nodeType(item) != SettingsNode) {
        return nullptr;
    }
    return m_project->settings(m_tree->indexOfTopLevelItem(item));
}

DetectorSettings *ProjectPanel::detectorOf(QTreeWidgetItem *item) const {
    if(!m_project || !item || nodeType(item) != DetectorNode) {
        return nullptr;
    }
    return m_project->detector(m_tree->indexOfTopLevelItem(item) - m_project->settingsCount());
}

DetectorSettings *ProjectPanel::currentDetector() const {
    return detectorOf(m_tree->currentItem());
}

Sample *ProjectPanel::currentSample() const {
    return sampleOf(m_tree->currentItem());
}

ExperimentalSettings *ProjectPanel::currentSettings() const {
    return settingsOf(m_tree->currentItem());
}

AttachedFile &ProjectPanel::currentFile() const {
    return currentSample()->files[nodeIndex(m_tree->currentItem())];
}

Measurement *ProjectPanel::currentMeasurement() const {
    return currentSample()->measurements.at(nodeIndex(m_tree->currentItem())).get();
}

QTreeWidgetItem *ProjectPanel::measurementItem(const Measurement *measurement) const {
    if(!m_project) {
        return nullptr;
    }
    for(size_t i = 0; i < m_project->sampleCount(); i++) {
        const auto &measurements = m_project->sample(i)->measurements;
        for(size_t j = 0; j < measurements.size(); j++) {
            if(measurements[j].get() == measurement) {
                return m_tree->topLevelItem(firstSampleRow() + static_cast<int>(i))->child(static_cast<int>(j));
            }
        }
    }
    return nullptr;
}

void ProjectPanel::currentItemChanged(QTreeWidgetItem *item) {
    showProperties(item);
    emit currentSampleChanged(sampleOf(item));
    emit currentSettingsChanged(settingsOf(item));
    emit currentDetectorChanged(detectorOf(item));
}

void ProjectPanel::showProperties(QTreeWidgetItem *item) {
    if(!item) {
        m_pages->setCurrentIndex(EmptyPage);
        return;
    }
    const Sample *sample = sampleOf(item);
    switch(nodeType(item)) {
        case SampleNode: {
            m_sampleNameEdit->setText(sample->name);
            {
                const QSignalBlocker blocker(m_sampleDescriptionEdit);
                m_sampleDescriptionEdit->setPlainText(sample->description);
            }
            m_metadataTable->setRowCount(0);
            for(const auto &entry : sample->metadata) {
                appendMetadataRow(entry);
            }
            m_pages->setCurrentIndex(SamplePage);
            break;
        }
        case MeasurementNode: {
            const Measurement *measurement = currentMeasurement();
            m_measurementNameEdit->setText(measurement->name);
            m_measurementDescriptionEdit->setText(measurement->description);
            m_measurementPathLabel->setText(measurement->path);
            m_measurementSettingsCombo->clear();
            m_measurementSettingsCombo->addItem("None", -1);
            for(size_t i = 0; i < m_project->settingsCount(); i++) {
                m_measurementSettingsCombo->addItem(displayName(m_project->settings(i)->name), static_cast<int>(i));
            }
            m_measurementSettingsCombo->setCurrentIndex(
                    m_measurementSettingsCombo->findData(m_project->settingsIndex(measurement->settings)));
            m_measurementDetectorCombo->clear();
            m_measurementDetectorCombo->addItem("None", -1);
            for(size_t i = 0; i < m_project->detectorCount(); i++) {
                m_measurementDetectorCombo->addItem(displayName(m_project->detector(i)->name), static_cast<int>(i));
            }
            m_measurementDetectorCombo->setCurrentIndex(
                    m_measurementDetectorCombo->findData(m_project->detectorIndex(measurement->detector)));
            showExitAngle(*measurement);
            m_pages->setCurrentIndex(MeasurementPage);
            break;
        }
        case FileNode: {
            const AttachedFile &file = currentFile();
            m_filePathLabel->setText(file.path);
            m_fileDescriptionEdit->setText(file.description);
            m_pages->setCurrentIndex(FilePage);
            break;
        }
        case SettingsNode:
            showSettings(*settingsOf(item));
            m_pages->setCurrentIndex(SettingsPage);
            break;
        case DetectorNode:
            showDetector(*detectorOf(item));
            m_pages->setCurrentIndex(DetectorPage);
            break;
    }
}

void ProjectPanel::showExitAngle(const Measurement &measurement) {
    if(!measurement.hasExitAngle()) {
        m_exitAngleLabel->setText("Needs experimental and detector settings");
        m_exitAngleLabel->setStyleSheet({});
        return;
    }
    const double angle = measurement.exitAngle();
    const QString text = locale().toString(angle, 'f', 2) + "°"; // Same decimal separator as in the spin boxes
    m_exitAngleLabel->setText(angle > 0 ? text : text + " (incident angle exceeds scattering angle)");
    m_exitAngleLabel->setStyleSheet(angle > 0 ? QString() : QStringLiteral("color: red"));
}

void ProjectPanel::showDetector(DetectorSettings &detector) {
    m_detectorEditor->setDetector(&detector);
    showAssigned(m_detectorAssignedList, m_project->measurementsUsing(&detector));
}

void ProjectPanel::showAssigned(QListWidget *list, const QList<Measurement *> &measurements) {
    list->clear();
    for(size_t i = 0; i < m_project->sampleCount(); i++) {
        const Sample *sample = m_project->sample(i);
        for(const auto &measurement : sample->measurements) {
            if(measurements.contains(measurement.get())) {
                auto *item = new QListWidgetItem(QString("%1 / %2").arg(displayName(sample->name), measurement->name));
                item->setData(MeasurementRole, QVariant::fromValue(reinterpret_cast<quintptr>(measurement.get())));
                list->addItem(item);
            }
        }
    }
}

void ProjectPanel::showSettings(ExperimentalSettings &settings) {
    m_settingsEditor->setSettings(&settings);
    showAssigned(m_assignedList, m_project->measurementsUsing(&settings));
}

void ProjectPanel::itemDoubleClicked(QTreeWidgetItem *item) {
    switch(nodeType(item)) {
        case MeasurementNode:
            emit measurementOpenRequested(currentMeasurement());
            break;
        case FileNode:
            openCurrentFile();
            break;
        default:
            break;
    }
}

void ProjectPanel::showContextMenu(const QPoint &pos) {
    if(!m_project) {
        return;
    }
    QMenu menu;
    QTreeWidgetItem *item = m_tree->itemAt(pos);
    if(item) {
        m_tree->setCurrentItem(item);
        if(nodeType(item) == SettingsNode || nodeType(item) == DetectorNode) {
            menu.addAction(m_copySettingsAction);
            menu.addAction("Export...", this, &ProjectPanel::exportSettings);
            menu.addSeparator();
            if(nodeType(item) == SettingsNode) {
                menu.addAction("Remove Experimental Settings", this, &ProjectPanel::removeSettings);
            } else {
                menu.addAction("Remove Detector Settings", this, &ProjectPanel::removeDetector);
            }
        } else {
            if(nodeType(item) == MeasurementNode) {
                menu.addAction("Open Measurement", this, [this]() { emit measurementOpenRequested(currentMeasurement()); });
                menu.addAction("Remove Measurement", this, &ProjectPanel::removeCurrentMeasurement);
                menu.addSeparator();
            } else if(nodeType(item) == FileNode) {
                menu.addAction("Open File", this, &ProjectPanel::openCurrentFile);
                menu.addAction("Remove File", this, &ProjectPanel::removeCurrentFile);
                menu.addSeparator();
            }
            menu.addAction("Add Measurements...", this, &ProjectPanel::addMeasurements);
            menu.addAction("Add Files...", this, &ProjectPanel::addFiles);
            menu.addAction("Remove Sample", this, &ProjectPanel::removeSample);
        }
        menu.addSeparator();
    }
    menu.addAction("Add Sample", this, &ProjectPanel::addSample);
    menu.addAction("Add Experimental Settings", this, &ProjectPanel::addSettings);
    menu.addAction("Add Detector Settings", this, &ProjectPanel::addDetector);
    menu.addSeparator();
    m_pasteSettingsAction->setEnabled(!settingsInClipboard().isEmpty());
    menu.addAction(m_pasteSettingsAction);
    menu.addAction("Import Settings...", this, &ProjectPanel::importSettings);
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
    m_pasteSettingsAction->setEnabled(true); // The shortcut checks the clipboard itself
}

void ProjectPanel::addSettings() {
    if(!m_project) {
        return;
    }
    SettingsCollection collection;
    collection.experimental.append(Defaults::experimentalSettings());
    addSettingsCollection(collection);
    m_settingsEditor->setFocus();
}

void ProjectPanel::removeSettings() {
    const ExperimentalSettings *settings = currentSettings();
    if(!settings) {
        return;
    }
    const qsizetype users = m_project->measurementsUsing(settings).size();
    QString question = QString("Remove experimental settings \"%1\"?").arg(displayName(settings->name));
    if(users > 0) {
        question += QString(" %1 measurement(s) use them and will have no experimental settings.").arg(users);
    }
    if(QMessageBox::question(this, "Remove Experimental Settings", question) != QMessageBox::Yes) {
        return;
    }
    {
        const QSignalBlocker blocker(m_tree);
        QTreeWidgetItem *item = m_tree->currentItem();
        m_project->removeSettings(m_tree->indexOfTopLevelItem(item));
        delete item;
    }
    currentItemChanged(m_tree->currentItem());
    emit modified();
}

void ProjectPanel::addDetector() {
    if(!m_project) {
        return;
    }
    SettingsCollection collection;
    collection.detectors.append(Defaults::detectorSettings());
    addSettingsCollection(collection);
    m_detectorEditor->setFocus();
}

void ProjectPanel::addSettingsCollection(const SettingsCollection &collection) {
    // Names are made unique, the last added set becomes current
    QTreeWidgetItem *last = nullptr;
    for(ExperimentalSettings settings : collection.experimental) {
        settings.name = m_project->uniqueSettingsName(settings.name);
        last = createSettingsItem(*m_project->addSettings(settings));
        m_tree->insertTopLevelItem(static_cast<int>(m_project->settingsCount()) - 1, last); // After the other settings
    }
    for(DetectorSettings detector : collection.detectors) {
        detector.name = m_project->uniqueDetectorName(detector.name);
        last = createDetectorItem(*m_project->addDetector(detector));
        m_tree->insertTopLevelItem(firstSampleRow() - 1, last); // After the other detectors
    }
    if(last) {
        m_tree->setCurrentItem(last);
        emit modified();
    }
}

SettingsCollection ProjectPanel::currentSettingsCollection() const {
    SettingsCollection collection;
    if(const ExperimentalSettings *settings = currentSettings()) {
        collection.experimental.append(*settings);
    }
    if(const DetectorSettings *detector = currentDetector()) {
        collection.detectors.append(*detector);
    }
    return collection;
}

void ProjectPanel::copySettings() {
    const SettingsCollection collection = currentSettingsCollection();
    if(collection.isEmpty()) {
        return;
    }
    const QByteArray json = settingsCollectionToJson(collection);
    auto *mimeData = new QMimeData;
    mimeData->setData(JsonMimeType, json);
    mimeData->setText(QString::fromUtf8(json));
    QGuiApplication::clipboard()->setMimeData(mimeData);
}

SettingsCollection ProjectPanel::settingsInClipboard() {
    const QMimeData *mimeData = QGuiApplication::clipboard()->mimeData();
    SettingsCollection collection;
    QString errorMessage;
    if(mimeData) {
        const QByteArray data = mimeData->hasFormat(JsonMimeType) ? mimeData->data(JsonMimeType) : mimeData->text().toUtf8();
        settingsCollectionFromJson(data, collection, errorMessage);
    }
    return collection;
}

void ProjectPanel::pasteSettings() {
    if(m_project) {
        addSettingsCollection(settingsInClipboard());
    }
}

void ProjectPanel::exportSettings() {
    const SettingsCollection collection = currentSettingsCollection();
    if(collection.isEmpty()) {
        return;
    }
    const QString name = collection.experimental.isEmpty() ? collection.detectors.first().name : collection.experimental.first().name;
    const QString fileName = QFileDialog::getSaveFileName(this, "Export Settings", name + ".json", SettingsFileFilter);
    if(fileName.isEmpty()) {
        return;
    }
    QSaveFile file(fileName);
    if(!file.open(QIODevice::WriteOnly) || file.write(settingsCollectionToJson(collection)) < 0 || !file.commit()) {
        QMessageBox::critical(this, "Export Settings", QString("Could not write %1:\n%2").arg(fileName, file.errorString()));
    }
}

void ProjectPanel::importSettings() {
    if(!m_project) {
        return;
    }
    const QString fileName = QFileDialog::getOpenFileName(this, "Import Settings", {}, SettingsFileFilter);
    if(fileName.isEmpty()) {
        return;
    }
    QFile file(fileName);
    SettingsCollection collection;
    QString errorMessage;
    if(!file.open(QIODevice::ReadOnly)) {
        errorMessage = file.errorString();
    } else {
        settingsCollectionFromJson(file.readAll(), collection, errorMessage);
    }
    if(!errorMessage.isEmpty() || collection.isEmpty()) {
        QMessageBox::warning(this, "Import Settings",
                             errorMessage.isEmpty() ? QString("%1 has no experimental or detector settings.").arg(fileName)
                                                    : QString("Could not import %1:\n%2").arg(fileName, errorMessage));
        return;
    }
    // A project file can have several settings sets, choose which ones to import
    if(collection.experimental.size() + collection.detectors.size() > 1 && !chooseSettings(this, collection)) {
        return;
    }
    addSettingsCollection(collection);
}

void ProjectPanel::removeDetector() {
    const DetectorSettings *detector = currentDetector();
    if(!detector) {
        return;
    }
    const qsizetype users = m_project->measurementsUsing(detector).size();
    QString question = QString("Remove detector settings \"%1\"?").arg(displayName(detector->name));
    if(users > 0) {
        question += QString(" %1 measurement(s) use them and will have no detector settings.").arg(users);
    }
    if(QMessageBox::question(this, "Remove Detector Settings", question) != QMessageBox::Yes) {
        return;
    }
    {
        const QSignalBlocker blocker(m_tree);
        QTreeWidgetItem *item = m_tree->currentItem();
        m_project->removeDetector(m_tree->indexOfTopLevelItem(item) - m_project->settingsCount());
        delete item;
    }
    currentItemChanged(m_tree->currentItem());
    emit modified();
}

void ProjectPanel::addSample() {
    if(!m_project) {
        return;
    }
    const Sample *sample = m_project->addSample(QString("Sample %1").arg(m_project->sampleCount() + 1));
    auto *item = createSampleItem(*sample);
    m_tree->addTopLevelItem(item);
    item->setExpanded(true);
    m_tree->setCurrentItem(item);
    m_sampleNameEdit->setFocus();
    m_sampleNameEdit->selectAll();
    emit modified();
}

void ProjectPanel::removeSample() {
    Sample *sample = currentSample();
    if(!sample) {
        return;
    }
    const auto answer = QMessageBox::question(this, "Remove Sample",
                                              QString("Remove sample \"%1\" and all its metadata, file references and measurements?")
                                                      .arg(displayName(sample->name)));
    if(answer != QMessageBox::Yes) {
        return;
    }
    emit sampleAboutToBeRemoved(sample);
    {
        // Tree and project are updated together before the properties are refreshed for the new current item
        const QSignalBlocker blocker(m_tree);
        QTreeWidgetItem *item = topLevelOf(m_tree->currentItem());
        m_project->removeSample(sampleIndexOf(item));
        delete item;
    }
    currentItemChanged(m_tree->currentItem());
    emit modified();
}

void ProjectPanel::addMeasurements() {
    Sample *sample = currentSample();
    if(!sample) {
        return;
    }
    QTreeWidgetItem *item = topLevelOf(m_tree->currentItem());
    const QStringList fileNames = QFileDialog::getOpenFileNames(this, "Add Measurements", {},
                                                                "ToF-E event files (*.asc);;All files (*)");
    if(fileNames.isEmpty()) {
        return;
    }
    QTreeWidgetItem *lastItem = nullptr;
    for(const QString &fileName : fileNames) {
        const QFileInfo fileInfo(fileName);
        sample->measurements.push_back(std::make_unique<Measurement>(fileInfo.fileName(), QString(), fileInfo.absoluteFilePath()));
        const int index = static_cast<int>(sample->measurements.size() - 1);
        lastItem = createMeasurementItem(*sample->measurements.back(), index);
        item->insertChild(index, lastItem); // Measurements are before files
    }
    item->setExpanded(true);
    m_tree->setCurrentItem(lastItem);
    emit modified();
}

void ProjectPanel::addFiles() {
    Sample *sample = currentSample();
    if(!sample) {
        return;
    }
    QTreeWidgetItem *item = topLevelOf(m_tree->currentItem());
    const QStringList fileNames = QFileDialog::getOpenFileNames(this, "Add Files");
    if(fileNames.isEmpty()) {
        return;
    }
    QTreeWidgetItem *lastItem = nullptr;
    for(const QString &fileName : fileNames) {
        sample->files.append(AttachedFile{QFileInfo(fileName).absoluteFilePath(), {}});
        lastItem = createFileItem(sample->files.last(), static_cast<int>(sample->files.size() - 1));
        item->addChild(lastItem);
    }
    item->setExpanded(true);
    m_tree->setCurrentItem(lastItem);
    emit modified();
}

void ProjectPanel::removeCurrentMeasurement() {
    QTreeWidgetItem *item = m_tree->currentItem();
    QTreeWidgetItem *parent = item->parent();
    auto &measurements = currentSample()->measurements;
    const int index = nodeIndex(item);
    emit measurementAboutToBeRemoved(measurements.at(index).get());
    {
        const QSignalBlocker blocker(m_tree);
        measurements.erase(measurements.begin() + index);
        delete item;
        renumberChildren(parent, MeasurementNode);
        m_tree->setCurrentItem(parent);
    }
    currentItemChanged(parent);
    emit modified();
}

void ProjectPanel::openCurrentFile() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(currentFile().path));
}

void ProjectPanel::removeCurrentFile() {
    QTreeWidgetItem *item = m_tree->currentItem();
    QTreeWidgetItem *parent = item->parent();
    {
        const QSignalBlocker blocker(m_tree);
        currentSample()->files.removeAt(nodeIndex(item));
        delete item;
        renumberChildren(parent, FileNode);
        m_tree->setCurrentItem(parent);
    }
    currentItemChanged(parent);
    emit modified();
}

void ProjectPanel::appendMetadataRow(const MetadataEntry &entry) {
    const QSignalBlocker blocker(m_metadataTable);
    const int row = m_metadataTable->rowCount();
    m_metadataTable->insertRow(row);
    m_metadataTable->setItem(row, 0, new QTableWidgetItem(entry.key));
    m_metadataTable->setItem(row, 1, new QTableWidgetItem(entry.value));
}

void ProjectPanel::addMetadata() {
    Sample *sample = currentSample();
    sample->metadata.append(MetadataEntry{});
    appendMetadataRow(sample->metadata.last());
    const int row = m_metadataTable->rowCount() - 1;
    m_metadataTable->setCurrentCell(row, 0);
    m_metadataTable->editItem(m_metadataTable->item(row, 0));
    emit modified();
}

void ProjectPanel::removeMetadata() {
    QList<int> rows;
    for(const QModelIndex &index : m_metadataTable->selectionModel()->selectedRows()) {
        rows.append(index.row());
    }
    std::sort(rows.begin(), rows.end(), std::greater<>());
    for(int row : rows) {
        currentSample()->metadata.removeAt(row);
        m_metadataTable->removeRow(row);
    }
    if(!rows.isEmpty()) {
        emit modified();
    }
}

void ProjectPanel::metadataItemChanged(QTableWidgetItem *item) {
    auto &entry = currentSample()->metadata[item->row()];
    (item->column() == 0 ? entry.key : entry.value) = item->text();
    emit modified();
}
