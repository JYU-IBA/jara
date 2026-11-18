#include "projectpanel.h"
#include "defaults.h"
#include "elements.h"
#include "projectimportdialog.h"
#include "project.h"

#include <algorithm>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QDrag>
#include <QDropEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHash>
#include <QHeaderView>
#include <QJsonDocument>
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
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {
// Experimental settings are the first top-level nodes, then detector settings and samples. Under a sample node,
// measurements come first, followed by attached files. Energy spectra and then depth profiles are under their
// measurement.
enum NodeType { SampleNode, MeasurementNode, FileNode, SettingsNode, DetectorNode, SpectrumNode, DepthProfileNode };
enum Page { EmptyPage, SamplePage, MeasurementPage, FilePage, SettingsPage, SpectrumPage, DepthProfilePage };
constexpr int TypeRole = Qt::UserRole;
constexpr int IndexRole = Qt::UserRole + 1; // Index in the sample's measurements or files, or the measurement's spectra
                                            // or depth profiles
constexpr int ObjectRole = Qt::UserRole + 2; // The sample, measurement, spectrum or depth profile, for finding it again
                                             // after sorting

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
    if(type != MeasurementNode) {
        item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled); // Only measurements can be moved by dragging
    }
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

const void *objectOf(const QTreeWidgetItem *item) {
    return reinterpret_cast<const void *>(item->data(0, ObjectRole).value<quintptr>());
}

QTreeWidgetItem *createSpectrumItem(const EnergySpectrum &spectrum, int index) {
    auto *item = new QTreeWidgetItem({displayName(spectrum.name)});
    item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled);
    item->setData(0, TypeRole, SpectrumNode);
    item->setData(0, IndexRole, index);
    item->setData(0, ObjectRole, QVariant::fromValue(reinterpret_cast<quintptr>(&spectrum)));
    item->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    return item;
}

QTreeWidgetItem *createDepthProfileItem(const DepthProfile &profile, int index) {
    auto *item = new QTreeWidgetItem({displayName(profile.name)});
    item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled);
    item->setData(0, TypeRole, DepthProfileNode);
    item->setData(0, IndexRole, index);
    item->setData(0, ObjectRole, QVariant::fromValue(reinterpret_cast<quintptr>(&profile)));
    item->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_FileDialogListView));
    return item;
}

QTreeWidgetItem *createMeasurementItem(const Measurement &measurement, int index) {
    auto *item = createItem(measurement.name, MeasurementNode, index, measurement.path, QStyle::SP_FileDialogContentsView);
    item->setData(0, ObjectRole, QVariant::fromValue(reinterpret_cast<quintptr>(&measurement)));
    for(size_t i = 0; i < measurement.spectra.size(); i++) {
        item->addChild(createSpectrumItem(*measurement.spectra.at(i), static_cast<int>(i)));
    }
    for(size_t i = 0; i < measurement.depthProfiles.size(); i++) {
        item->addChild(createDepthProfileItem(*measurement.depthProfiles.at(i), static_cast<int>(i)));
    }
    return item;
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

// A dragged measurement is identified by the process and its address. The process is checked so that a drag from
// another JaRA is never taken for one of this project's measurements.
const QString MeasurementMimeType = QStringLiteral("application/x-jara-measurement");

QByteArray measurementDragData(const Measurement *measurement) {
    return QByteArray::number(QCoreApplication::applicationPid()) + ' ' + QByteArray::number(reinterpret_cast<quintptr>(measurement));
}

const void *draggedMeasurement(const QMimeData *mimeData) {
    const QList<QByteArray> parts = mimeData->data(MeasurementMimeType).split(' ');
    if(parts.size() != 2 || parts[0].toLongLong() != QCoreApplication::applicationPid()) {
        return nullptr;
    }
    return reinterpret_cast<const void *>(parts[1].toULongLong());
}

// Measurements can be dragged to another sample. The default drag of QTreeWidget would remove the dragged row
// itself, so the drag is started here and the drop is handled in ProjectPanel::eventFilter().
class ProjectTree : public QTreeWidget {
protected:
    void startDrag(Qt::DropActions) override {
        QTreeWidgetItem *item = currentItem();
        if(!item || nodeType(item) != MeasurementNode) {
            return;
        }
        auto *mimeData = new QMimeData;
        mimeData->setData(MeasurementMimeType, measurementDragData(static_cast<const Measurement *>(objectOf(item))));
        auto *drag = new QDrag(this);
        drag->setMimeData(mimeData);
        drag->setPixmap(item->icon(0).pixmap(16));
        drag->exec(Qt::MoveAction);
    }
};

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

QStringList localFiles(const QMimeData *mimeData, const QString &suffix) {
    QStringList files;
    for(const QUrl &url : mimeData->urls()) {
        if(url.isLocalFile() && url.toLocalFile().endsWith(suffix, Qt::CaseInsensitive)) {
            files.append(url.toLocalFile());
        }
    }
    return files;
}

// Lets the user pick which settings sets to import, removes the others from the collection
bool chooseSettings(QWidget *parent, SettingsCollection &collection, const QString &fileName) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QString("Import Settings from %1").arg(QFileInfo(fileName).fileName()));
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
    m_tree = new ProjectTree;
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
    m_tree->setAcceptDrops(true);
    m_tree->setDragEnabled(true); // Measurements can be dragged to another sample
    m_tree->viewport()->installEventFilter(this); // Drops are handled in eventFilter()

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
    // Read-only summary of the assigned settings
    m_beamIonLabel = new QLabel;
    m_beamEnergyLabel = new QLabel;
    m_incidentAngleLabel = new QLabel;
    m_incidentAngleLabel->setToolTip(FromSurface);
    m_scatteringAngleLabel = new QLabel;
    m_scatteringAngleLabel->setToolTip(FromSurface);
    m_exitAngleLabel = new QLabel;
    m_exitAngleLabel->setToolTip(QString("%1. Scattering angle minus incident angle (IBM geometry).").arg(FromSurface));
    auto *measurementLayout = new QFormLayout;
    measurementLayout->addRow("Name:", m_measurementNameEdit);
    measurementLayout->addRow("Description:", m_measurementDescriptionEdit);
    measurementLayout->addRow("Data file:", m_measurementPathLabel);
    measurementLayout->addRow("Experimental settings:", m_measurementSettingsCombo);
    measurementLayout->addRow("Detector settings:", m_measurementDetectorCombo);
    measurementLayout->addRow("Incident ion:", m_beamIonLabel);
    measurementLayout->addRow("Beam energy:", m_beamEnergyLabel);
    measurementLayout->addRow("Incident angle:", m_incidentAngleLabel);
    measurementLayout->addRow("Scattering angle:", m_scatteringAngleLabel);
    measurementLayout->addRow("Exit angle:", m_exitAngleLabel);
    m_pages->insertWidget(MeasurementPage, createPage(measurementLayout));

    m_filePathLabel = pathLabel();
    m_fileDescriptionEdit = new QLineEdit;
    auto *fileLayout = new QFormLayout;
    fileLayout->addRow("Path:", m_filePathLabel);
    fileLayout->addRow("Description:", m_fileDescriptionEdit);
    m_pages->insertWidget(FilePage, createPage(fileLayout));

    auto *settingsHint = new QLabel("Double-click to edit the settings in a tab.");
    settingsHint->setWordWrap(true);
    auto *settingsLayout = new QVBoxLayout;
    settingsLayout->addWidget(settingsHint);
    settingsLayout->addStretch();
    m_pages->insertWidget(SettingsPage, createPage(settingsLayout));

    m_spectrumNameEdit = new QLineEdit;
    auto *spectrumHint = new QLabel("Double-click to open the energy spectrum in a tab.");
    spectrumHint->setWordWrap(true);
    auto *spectrumLayout = new QFormLayout;
    spectrumLayout->addRow("Name:", m_spectrumNameEdit);
    spectrumLayout->addRow(spectrumHint);
    m_pages->insertWidget(SpectrumPage, createPage(spectrumLayout));

    m_depthProfileNameEdit = new QLineEdit;
    auto *depthProfileHint = new QLabel("Double-click to open the depth profile in a tab.");
    depthProfileHint->setWordWrap(true);
    auto *depthProfileLayout = new QFormLayout;
    depthProfileLayout->addRow("Name:", m_depthProfileNameEdit);
    depthProfileLayout->addRow(depthProfileHint);
    m_pages->insertWidget(DepthProfilePage, createPage(depthProfileLayout));

    connect(m_sampleNameEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        QTreeWidgetItem *item = topLevelOf(m_tree->currentItem());
        sampleOf(item)->name = text;
        item->setText(0, displayName(text));
        emit modified();
    });
    // Renamed samples and measurements are moved to their place when the name is finished, not while typing
    connect(m_sampleNameEdit, &QLineEdit::editingFinished, this, [this]() {
        if(m_sampleNameEdit->isModified()) {
            m_sampleNameEdit->setModified(false);
            sortSamples();
        }
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
    connect(m_measurementNameEdit, &QLineEdit::editingFinished, this, [this]() {
        if(m_measurementNameEdit->isModified()) {
            m_measurementNameEdit->setModified(false);
            sortSamples();
        }
    });
    connect(m_measurementDescriptionEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        currentMeasurement()->description = text;
        emit modified();
    });
    connect(m_measurementSettingsCombo, &QComboBox::activated, this, [this]() {
        const int index = m_measurementSettingsCombo->currentData().toInt();
        Measurement *measurement = currentMeasurement();
        measurement->settings = index >= 0 ? m_project->settings(index) : nullptr;
        showGeometry(*measurement);
        emit modified();
    });
    connect(m_measurementDetectorCombo, &QComboBox::activated, this, [this]() {
        const int index = m_measurementDetectorCombo->currentData().toInt();
        Measurement *measurement = currentMeasurement();
        measurement->detector = index >= 0 ? m_project->detector(index) : nullptr;
        showGeometry(*measurement);
        emit modified();
    });
    connect(m_fileDescriptionEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        currentFile().description = text;
        emit modified();
    });
    connect(m_spectrumNameEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        EnergySpectrum *spectrum = currentSpectrum();
        spectrum->name = text;
        m_tree->currentItem()->setText(0, displayName(text));
        emit spectrumRenamed(spectrum);
        emit modified();
    });
    connect(m_depthProfileNameEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        DepthProfile *profile = currentDepthProfile();
        profile->name = text;
        m_tree->currentItem()->setText(0, displayName(text));
        emit depthProfileRenamed(profile);
        emit modified();
    });
    return m_pages;
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
    item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled);
    item->setData(0, TypeRole, SettingsNode);
    item->setIcon(0, style()->standardIcon(QStyle::SP_FileDialogInfoView));
    return item;
}

QTreeWidgetItem *ProjectPanel::createDetectorItem(const DetectorSettings &detector) {
    auto *item = new QTreeWidgetItem({displayName(detector.name)});
    item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled);
    item->setData(0, TypeRole, DetectorNode);
    item->setIcon(0, style()->standardIcon(QStyle::SP_DriveHDIcon));
    return item;
}

QTreeWidgetItem *ProjectPanel::createSampleItem(const Sample &sample) {
    auto *item = new QTreeWidgetItem({displayName(sample.name)});
    item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled);
    item->setData(0, TypeRole, SampleNode);
    item->setData(0, ObjectRole, QVariant::fromValue(reinterpret_cast<quintptr>(&sample)));
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
    QTreeWidgetItem *item = m_tree->currentItem();
    if(nodeType(item) == SpectrumNode || nodeType(item) == DepthProfileNode) {
        item = item->parent();
    }
    return currentSample()->measurements.at(nodeIndex(item)).get();
}

DepthProfile *ProjectPanel::currentDepthProfile() const {
    QTreeWidgetItem *item = m_tree->currentItem();
    if(!m_project || !item || nodeType(item) != DepthProfileNode) {
        return nullptr;
    }
    return currentMeasurement()->depthProfiles.at(nodeIndex(item)).get();
}

EnergySpectrum *ProjectPanel::currentSpectrum() const {
    QTreeWidgetItem *item = m_tree->currentItem();
    if(!m_project || !item || nodeType(item) != SpectrumNode) {
        return nullptr;
    }
    return currentMeasurement()->spectra.at(nodeIndex(item)).get();
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
            showGeometry(*measurement);
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
        case DetectorNode:
            m_pages->setCurrentIndex(SettingsPage);
            break;
        case SpectrumNode:
            m_spectrumNameEdit->setText(currentSpectrum()->name);
            m_pages->setCurrentIndex(SpectrumPage);
            break;
        case DepthProfileNode:
            m_depthProfileNameEdit->setText(currentDepthProfile()->name);
            m_pages->setCurrentIndex(DepthProfilePage);
            break;
    }
}

void ProjectPanel::showGeometry(const Measurement &measurement) {
    // Same number formatting as in the settings editors, with the locale's decimal separator
    auto angleText = [this](double angle) { return locale().toString(angle / Units::Degree, 'f', 2) + "°"; };
    const QString noExperimental = QStringLiteral("No experimental settings");
    const QString noDetector = QStringLiteral("No detector settings");
    if(const ExperimentalSettings *settings = measurement.settings) {
        m_beamIonLabel->setText(settings->beamZ > 0 ? Elements::displayLabel(settings->beamZ, settings->beamA) : QStringLiteral("Not set"));
        m_beamEnergyLabel->setText(locale().toString(settings->beamEnergy / Units::Megaelectronvolt, 'f', 3) + " MeV");
        m_incidentAngleLabel->setText(angleText(settings->incidentAngle));
    } else {
        m_beamIonLabel->setText(noExperimental);
        m_beamEnergyLabel->setText(noExperimental);
        m_incidentAngleLabel->setText(noExperimental);
    }
    m_scatteringAngleLabel->setText(measurement.detector ? angleText(measurement.detector->scatteringAngle) : noDetector);
    if(!measurement.hasExitAngle()) {
        m_exitAngleLabel->setText("Needs experimental and detector settings");
        m_exitAngleLabel->setStyleSheet({});
        return;
    }
    const double angle = measurement.exitAngle();
    m_exitAngleLabel->setText(angle > 0 ? angleText(angle) : angleText(angle) + " (incident angle exceeds scattering angle)");
    m_exitAngleLabel->setStyleSheet(angle > 0 ? QString() : QStringLiteral("color: red"));
}

void ProjectPanel::selectMeasurement(const Measurement *measurement) {
    if(QTreeWidgetItem *item = measurementItem(measurement)) {
        m_tree->setCurrentItem(item);
    }
}

void ProjectPanel::settingsEdited() {
    if(!m_project) {
        return;
    }
    for(int i = 0; i < m_tree->topLevelItemCount(); i++) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        if(const ExperimentalSettings *settings = settingsOf(item)) {
            item->setText(0, displayName(settings->name));
        } else if(const DetectorSettings *detector = detectorOf(item)) {
            item->setText(0, displayName(detector->name));
        }
    }
    // The measurement page shows the names and values of the assigned settings
    QTreeWidgetItem *current = m_tree->currentItem();
    if(current && nodeType(current) == MeasurementNode) {
        showProperties(current);
    }
}

void ProjectPanel::itemDoubleClicked(QTreeWidgetItem *item) {
    m_tree->setCurrentItem(item); // The actions below work on the current item
    switch(nodeType(item)) {
        case MeasurementNode:
            emit measurementOpenRequested(currentMeasurement());
            break;
        case FileNode:
            openCurrentFile();
            break;
        case SettingsNode:
            emit settingsOpenRequested(currentSettings());
            break;
        case DetectorNode:
            emit detectorOpenRequested(currentDetector());
            break;
        case SpectrumNode:
            emit spectrumOpenRequested(currentMeasurement(), currentSpectrum());
            break;
        case DepthProfileNode:
            emit depthProfileOpenRequested(currentMeasurement(), currentDepthProfile());
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
            menu.addAction("Edit", this, [this, item]() { itemDoubleClicked(item); });
            menu.addSeparator();
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
                menu.addAction("Add Energy Spectrum", this, &ProjectPanel::addSpectrum);
                menu.addAction("Add Depth Profile", this, &ProjectPanel::addDepthProfile);
                menu.addAction("Remove Measurement", this, &ProjectPanel::removeCurrentMeasurement);
                menu.addSeparator();
            } else if(nodeType(item) == SpectrumNode) {
                menu.addAction("Open Energy Spectrum", this, [this]() {
                    emit spectrumOpenRequested(currentMeasurement(), currentSpectrum());
                });
                menu.addAction("Remove Energy Spectrum", this, &ProjectPanel::removeCurrentSpectrum);
                menu.addSeparator();
            } else if(nodeType(item) == DepthProfileNode) {
                menu.addAction("Open Depth Profile", this, [this]() {
                    emit depthProfileOpenRequested(currentMeasurement(), currentDepthProfile());
                });
                menu.addAction("Remove Depth Profile", this, &ProjectPanel::removeCurrentDepthProfile);
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
    menu.addAction("Import...", this, &ProjectPanel::importSettings);
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
    emit settingsOpenRequested(currentSettings());
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
    emit settingsAboutToBeRemoved(currentSettings());
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
    emit detectorOpenRequested(currentDetector());
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
    const QString fileName = QFileDialog::getOpenFileName(this, "Import", {}, SettingsFileFilter);
    if(!fileName.isEmpty()) {
        importFile(fileName);
    }
}

void ProjectPanel::importFile(const QString &fileName, int targetSample) {
    QFile file(fileName);
    QByteArray data;
    if(file.open(QIODevice::ReadOnly)) {
        data = file.readAll();
    }
    // Projects are loaded in full, so that their paths are resolved relative to the project file
    if(QJsonDocument::fromJson(data).object()["format"].toString() == "jara-project") {
        QString errorMessage;
        std::unique_ptr<Project> source = Project::load(fileName, errorMessage);
        if(!source) {
            QMessageBox::warning(this, "Import", QString("Could not import %1:\n%2").arg(fileName, errorMessage));
            return;
        }
        importProject(*source, fileName, targetSample);
        return;
    }
    SettingsCollection collection;
    QString errorMessage = file.error() != QFile::NoError ? file.errorString() : QString();
    if(errorMessage.isEmpty()) {
        settingsCollectionFromJson(data, collection, errorMessage);
    }
    if(!errorMessage.isEmpty() || collection.isEmpty()) {
        QMessageBox::warning(this, "Import",
                             errorMessage.isEmpty() ? QString("%1 has no experimental or detector settings.").arg(fileName)
                                                    : QString("Could not import %1:\n%2").arg(fileName, errorMessage));
        return;
    }
    // Choose which settings to import from a file with several sets
    if(collection.experimental.size() + collection.detectors.size() > 1 && !chooseSettings(this, collection, fileName)) {
        return;
    }
    addSettingsCollection(collection);
}

void ProjectPanel::importProject(Project &source, const QString &fileName, int targetSample) {
    ProjectImportDialog dialog(source, fileName, targetSample >= 0 ? m_project->sample(targetSample) : nullptr, this);
    if(dialog.exec() != QDialog::Accepted) {
        return;
    }
    // Imported measurements keep their settings when the settings are imported too
    QHash<const ExperimentalSettings *, ExperimentalSettings *> settingsCopies;
    QHash<const DetectorSettings *, DetectorSettings *> detectorCopies;
    SettingsCollection collection;
    for(size_t i = 0; i < source.settingsCount(); i++) {
        if(dialog.isSelected(source.settings(i))) {
            collection.experimental.append(*source.settings(i));
        }
    }
    for(size_t i = 0; i < source.detectorCount(); i++) {
        if(dialog.isSelected(source.detector(i))) {
            collection.detectors.append(*source.detector(i));
        }
    }
    const size_t settingsBefore = m_project->settingsCount();
    const size_t detectorsBefore = m_project->detectorCount();
    addSettingsCollection(collection);
    for(size_t i = 0, added = 0; i < source.settingsCount(); i++) {
        if(dialog.isSelected(source.settings(i))) {
            settingsCopies.insert(source.settings(i), m_project->settings(settingsBefore + added++));
        }
    }
    for(size_t i = 0, added = 0; i < source.detectorCount(); i++) {
        if(dialog.isSelected(source.detector(i))) {
            detectorCopies.insert(source.detector(i), m_project->detector(detectorsBefore + added++));
        }
    }
    auto importMeasurement = [&](const Measurement &measurement) {
        auto copy = copyMeasurement(measurement);
        copy->settings = settingsCopies.value(measurement.settings);
        copy->detector = detectorCopies.value(measurement.detector);
        // As with new measurements, settings are assigned when there is no choice to make
        if(!copy->settings && m_project->settingsCount() == 1) {
            copy->settings = m_project->settings(0);
        }
        if(!copy->detector && m_project->detectorCount() == 1) {
            copy->detector = m_project->detector(0);
        }
        return copy;
    };

    const void *select = nullptr;
    for(size_t i = 0; i < source.sampleCount(); i++) {
        const Sample *sample = source.sample(i);
        if(!dialog.isSelected(sample)) {
            continue;
        }
        Sample *target = nullptr;
        if(targetSample >= 0) {
            target = m_project->sample(targetSample);
        } else {
            auto copy = std::make_unique<Sample>();
            copy->name = m_project->uniqueSampleName(sample->name);
            copy->description = sample->description;
            copy->metadata = sample->metadata;
            copy->files = sample->files;
            target = m_project->addSample(std::move(copy));
            select = target;
        }
        for(const auto &measurement : sample->measurements) {
            if(dialog.isSelected(measurement.get())) {
                target->measurements.push_back(importMeasurement(*measurement));
                if(targetSample >= 0) {
                    select = target->measurements.back().get();
                }
            }
        }
    }
    if(select) {
        rebuildSampleItems(select);
        emit modified();
    }
}

bool ProjectPanel::eventFilter(QObject *watched, QEvent *event) {
    // JSON files dropped on the tree are imported, ASC files dropped on a sample or its children become its measurements
    if(watched == m_tree->viewport()
       && (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove || event->type() == QEvent::Drop)) {
        auto *dropEvent = static_cast<QDropEvent *>(event);
        const int sampleIndex = sampleIndexOf(m_tree->itemAt(dropEvent->position().toPoint()));
        if(dropEvent->mimeData()->hasFormat(MeasurementMimeType)) {
            handleMeasurementDrag(dropEvent, sampleIndex);
            return true;
        }
        const QStringList jsonFiles = m_project ? localFiles(dropEvent->mimeData(), ".json") : QStringList();
        const QStringList allAscFiles = m_project ? localFiles(dropEvent->mimeData(), ".asc") : QStringList();
        const QStringList ascFiles = sampleIndex >= 0 ? allAscFiles : QStringList();
        // A drag that is refused when it enters gets no further events, so it is accepted whenever it has files
        // to drop somewhere. Where they can be dropped is decided as the drag moves.
        const bool accept = event->type() == QEvent::DragEnter ? !jsonFiles.isEmpty() || !allAscFiles.isEmpty()
                                                               : !jsonFiles.isEmpty() || !ascFiles.isEmpty();
        if(!accept) {
            dropEvent->ignore();
            return true;
        }
        dropEvent->acceptProposedAction();
        if(event->type() == QEvent::Drop) {
            // Importing may show dialogs, which should not be run while the drop is being handled
            QTimer::singleShot(0, this, [this, jsonFiles, ascFiles, sampleIndex]() {
                // Measurements from a project dropped on a sample are added to it, otherwise samples are added
                for(const QString &file : jsonFiles) {
                    importFile(file, sampleIndex);
                }
                if(!ascFiles.isEmpty()) {
                    addMeasurementFiles(sampleIndex, ascFiles);
                }
            });
        }
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void ProjectPanel::handleMeasurementDrag(QDropEvent *event, int targetSample) {
    // Only measurements of this project, dropped on another sample or the items under it
    const void *dragged = draggedMeasurement(event->mimeData());
    QTreeWidgetItem *item = nullptr;
    for(QTreeWidgetItemIterator it(m_tree); *it && dragged && !item; ++it) {
        if(nodeType(*it) == MeasurementNode && objectOf(*it) == dragged) {
            item = *it;
        }
    }
    // The drag is accepted when it enters, where it can be dropped is decided as it moves
    const bool valid = item && (event->type() == QEvent::DragEnter || (targetSample >= 0 && targetSample != sampleIndexOf(item)));
    if(!valid) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::MoveAction);
    event->accept();
    if(event->type() == QEvent::Drop) {
        auto *measurement = const_cast<Measurement *>(static_cast<const Measurement *>(dragged));
        // The tree is rebuilt after the drag has finished
        QTimer::singleShot(0, this, [this, measurement, targetSample]() {
            moveMeasurement(measurement, targetSample);
        });
    }
}

void ProjectPanel::moveMeasurement(Measurement *measurement, int targetSample) {
    for(size_t i = 0; i < m_project->sampleCount(); i++) {
        auto &measurements = m_project->sample(i)->measurements;
        const auto found = std::find_if(measurements.begin(), measurements.end(),
                                        [measurement](const auto &m) { return m.get() == measurement; });
        if(found != measurements.end()) {
            // The measurement object itself is moved, so open views still refer to it
            m_project->sample(targetSample)->measurements.push_back(std::move(*found));
            measurements.erase(found);
            rebuildSampleItems(measurement);
            emit modified();
            return;
        }
    }
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
    emit detectorAboutToBeRemoved(currentDetector());
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
    const Sample *sample = m_project->addSample(m_project->uniqueSampleName(QString("Sample %1").arg(m_project->sampleCount() + 1)));
    rebuildSampleItems(sample);
    m_sampleNameEdit->setFocus();
    m_sampleNameEdit->selectAll();
    emit modified();
}

void ProjectPanel::sortSamples() {
    if(!m_project) {
        return;
    }
    QTreeWidgetItem *current = m_tree->currentItem();
    const void *object = current ? objectOf(current) : nullptr;
    if(current && !object) {
        object = objectOf(topLevelOf(current)); // A file or settings, select its sample or keep the selection
    }
    rebuildSampleItems(object);
}

void ProjectPanel::rebuildSampleItems(const void *select) {
    // Samples and their measurements are sorted in the project, the sample items are rebuilt in that order
    m_project->sortByName();
    QSet<const void *> collapsed;
    QTreeWidgetItem *current = m_tree->currentItem();
    const bool keepCurrent = current && nodeType(topLevelOf(current)) != SampleNode && !select;
    {
        const QSignalBlocker blocker(m_tree);
        while(m_tree->topLevelItemCount() > firstSampleRow()) {
            QTreeWidgetItem *item = m_tree->topLevelItem(firstSampleRow());
            for(QTreeWidgetItemIterator it(item); *it && topLevelOf(*it) == item; ++it) {
                if((*it)->childCount() > 0 && !(*it)->isExpanded()) {
                    collapsed.insert(objectOf(*it));
                }
            }
            delete item;
        }
        QTreeWidgetItem *selected = keepCurrent ? current : nullptr;
        for(size_t i = 0; i < m_project->sampleCount(); i++) {
            const Sample *sample = m_project->sample(i);
            QTreeWidgetItem *item = createSampleItem(*sample);
            m_tree->addTopLevelItem(item);
            for(QTreeWidgetItemIterator it(item); *it && topLevelOf(*it) == item; ++it) {
                (*it)->setExpanded((*it)->childCount() > 0 && !collapsed.contains(objectOf(*it)));
                if(!selected && objectOf(*it) == select) {
                    selected = *it;
                }
            }
        }
        for(QTreeWidgetItem *parent = selected ? selected->parent() : nullptr; parent; parent = parent->parent()) {
            parent->setExpanded(true);
        }
        m_tree->setCurrentItem(selected);
    }
    // The properties are only refreshed when the selection changed, so that edits in progress are not disturbed
    if(!current || m_tree->currentItem() != current) {
        currentItemChanged(m_tree->currentItem());
    }
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
    const int sampleIndex = sampleIndexOf(m_tree->currentItem());
    if(sampleIndex < 0) {
        return;
    }
    const QStringList fileNames = QFileDialog::getOpenFileNames(this, "Add Measurements", {},
                                                                "ToF-E event files (*.asc);;All files (*)");
    if(!fileNames.isEmpty()) {
        addMeasurementFiles(sampleIndex, fileNames);
    }
}

void ProjectPanel::addMeasurementFiles(int sampleIndex, const QStringList &fileNames) {
    Sample *sample = m_project->sample(sampleIndex);
    const Measurement *last = nullptr;
    for(const QString &fileName : fileNames) {
        const QFileInfo fileInfo(fileName);
        auto measurement = std::make_unique<Measurement>(fileInfo.fileName(), QString(), fileInfo.absoluteFilePath());
        // When there is no choice to make, assign the settings right away
        if(m_project->settingsCount() == 1) {
            measurement->settings = m_project->settings(0);
        }
        if(m_project->detectorCount() == 1) {
            measurement->detector = m_project->detector(0);
        }
        last = measurement.get();
        sample->measurements.push_back(std::move(measurement));
    }
    rebuildSampleItems(last);
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

void ProjectPanel::addSpectrum() {
    QTreeWidgetItem *item = m_tree->currentItem();
    if(!m_project || !item || (nodeType(item) != MeasurementNode && nodeType(item) != SpectrumNode)) {
        return;
    }
    if(nodeType(item) == SpectrumNode) {
        item = item->parent();
    }
    Measurement *measurement = currentMeasurement();
    auto spectrum = std::make_unique<EnergySpectrum>();
    spectrum->name = measurement->uniqueSpectrumName("Energy spectrum");
    for(const Cut &cut : measurement->cuts) {
        spectrum->cutIds.append(cut.id);
    }
    measurement->spectra.push_back(std::move(spectrum));
    QTreeWidgetItem *spectrumItem = createSpectrumItem(*measurement->spectra.back(), static_cast<int>(measurement->spectra.size() - 1));
    item->insertChild(static_cast<int>(measurement->spectra.size() - 1), spectrumItem); // Spectra before depth profiles
    item->setExpanded(true);
    m_tree->setCurrentItem(spectrumItem);
    emit modified();
    emit spectrumOpenRequested(measurement, currentSpectrum());
}

void ProjectPanel::removeCurrentSpectrum() {
    QTreeWidgetItem *item = m_tree->currentItem();
    if(!item || nodeType(item) != SpectrumNode) {
        return;
    }
    QTreeWidgetItem *parent = item->parent();
    auto &spectra = currentMeasurement()->spectra;
    const int index = nodeIndex(item);
    emit spectrumAboutToBeRemoved(spectra.at(index).get());
    {
        const QSignalBlocker blocker(m_tree);
        spectra.erase(spectra.begin() + index);
        delete item;
        renumberChildren(parent, SpectrumNode);
        m_tree->setCurrentItem(parent);
    }
    currentItemChanged(parent);
    emit modified();
}

void ProjectPanel::addDepthProfile() {
    QTreeWidgetItem *item = m_tree->currentItem();
    if(!m_project || !item) {
        return;
    }
    if(nodeType(item) == SpectrumNode || nodeType(item) == DepthProfileNode) {
        item = item->parent();
    }
    if(nodeType(item) != MeasurementNode) {
        return;
    }
    m_tree->setCurrentItem(item);
    Measurement *measurement = currentMeasurement();
    auto profile = std::make_unique<DepthProfile>(Defaults::depthProfile());
    profile->name = measurement->uniqueDepthProfileName("Depth profile");
    for(const Cut &cut : measurement->cuts) {
        profile->cutIds.append(cut.id);
    }
    measurement->depthProfiles.push_back(std::move(profile));
    QTreeWidgetItem *profileItem = createDepthProfileItem(*measurement->depthProfiles.back(),
                                                          static_cast<int>(measurement->depthProfiles.size() - 1));
    item->addChild(profileItem);
    item->setExpanded(true);
    m_tree->setCurrentItem(profileItem);
    emit modified();
    emit depthProfileOpenRequested(measurement, currentDepthProfile());
}

void ProjectPanel::removeCurrentDepthProfile() {
    QTreeWidgetItem *item = m_tree->currentItem();
    if(!item || nodeType(item) != DepthProfileNode) {
        return;
    }
    QTreeWidgetItem *parent = item->parent();
    auto &profiles = currentMeasurement()->depthProfiles;
    const int index = nodeIndex(item);
    emit depthProfileAboutToBeRemoved(profiles.at(index).get());
    {
        const QSignalBlocker blocker(m_tree);
        profiles.erase(profiles.begin() + index);
        delete item;
        renumberChildren(parent, DepthProfileNode);
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
