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
// Experimental settings are the first top-level nodes, then detector settings, physics settings and samples. Under a sample node,
// measurements come first, followed by attached files. Energy spectra, depth profiles and then split analyses are
// under their measurement.
enum NodeType { SampleNode, MeasurementNode, FileNode, SettingsNode, DetectorNode, SpectrumNode, DepthProfileNode, PhysicsNode,
                SplitNode, ReportNode, ProjectNode, HistogramNode };
enum Page { EmptyPage, SamplePage, MeasurementPage, FilePage, SettingsPage, SpectrumPage, DepthProfilePage, SplitPage,
            ReportPage, ProjectPage, HistogramPage };
constexpr int FirstSettingsRow = 1; // After the project information
constexpr int TypeRole = Qt::UserRole;
constexpr int IndexRole = Qt::UserRole + 1; // Index in the sample's measurements or files, or the measurement's spectra,
                                            // depth profiles or split analyses
constexpr int ObjectRole = Qt::UserRole + 2; // The sample, measurement, spectrum, depth profile or split analysis, for
                                             // finding it again after sorting

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
        item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled); // Only measurements and depth profiles can be dragged
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
    auto *item = new QTreeWidgetItem({displayName(profile.name)}); // Can be dragged, a copy is dropped
    item->setData(0, TypeRole, DepthProfileNode);
    item->setData(0, IndexRole, index);
    item->setData(0, ObjectRole, QVariant::fromValue(reinterpret_cast<quintptr>(&profile)));
    item->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_FileDialogListView));
    return item;
}

QTreeWidgetItem *createSplitItem(const SplitAnalysis &split, int index) {
    auto *item = new QTreeWidgetItem({displayName(split.name)});
    item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled);
    item->setData(0, TypeRole, SplitNode);
    item->setData(0, IndexRole, index);
    item->setData(0, ObjectRole, QVariant::fromValue(reinterpret_cast<quintptr>(&split)));
    item->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_FileDialogInfoView));
    return item;
}

QTreeWidgetItem *createProjectItem(const Project &project) {
    auto *item = new QTreeWidgetItem({project.displayName()});
    item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled);
    item->setData(0, TypeRole, ProjectNode);
    item->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_FileDialogInfoView));
    item->setToolTip(0, "Project information");
    return item;
}

QTreeWidgetItem *createReportItem(const Report &report, int index) {
    auto *item = new QTreeWidgetItem({displayName(report.name)});
    item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled);
    item->setData(0, TypeRole, ReportNode);
    item->setData(0, IndexRole, index);
    item->setData(0, ObjectRole, QVariant::fromValue(reinterpret_cast<quintptr>(&report)));
    item->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_FileDialogContentsView));
    return item;
}

// The ToF-E histogram, spectra, depth profiles and split analyses of a measurement
bool isAnalysisNode(const QTreeWidgetItem *item) {
    return nodeType(item) == HistogramNode || nodeType(item) == SpectrumNode || nodeType(item) == DepthProfileNode
           || nodeType(item) == SplitNode;
}

// The first child of a measurement, there is always one
QTreeWidgetItem *createHistogramItem() {
    auto *item = new QTreeWidgetItem({QStringLiteral("ToF-E histogram")});
    item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled);
    item->setData(0, TypeRole, HistogramNode);
    item->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_DesktopIcon));
    return item;
}

QTreeWidgetItem *createMeasurementItem(const Measurement &measurement, int index) {
    auto *item = createItem(measurement.name, MeasurementNode, index, measurement.path, QStyle::SP_FileDialogContentsView);
    item->setData(0, ObjectRole, QVariant::fromValue(reinterpret_cast<quintptr>(&measurement)));
    item->addChild(createHistogramItem());
    for(size_t i = 0; i < measurement.spectra.size(); i++) {
        item->addChild(createSpectrumItem(*measurement.spectra.at(i), static_cast<int>(i)));
    }
    for(size_t i = 0; i < measurement.depthProfiles.size(); i++) {
        item->addChild(createDepthProfileItem(*measurement.depthProfiles.at(i), static_cast<int>(i)));
    }
    for(size_t i = 0; i < measurement.splitAnalyses.size(); i++) {
        item->addChild(createSplitItem(*measurement.splitAnalyses.at(i), static_cast<int>(i)));
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

// A dragged measurement or depth profile is identified by the process and its address. The process is checked so
// that a drag from another JaRA is never taken for one of this project's objects.
const QString MeasurementMimeType = QStringLiteral("application/x-jara-measurement");
const QString DepthProfileMimeType = QStringLiteral("application/x-jara-depth-profile");

QByteArray dragData(const void *object) {
    return QByteArray::number(QCoreApplication::applicationPid()) + ' ' + QByteArray::number(reinterpret_cast<quintptr>(object));
}

const void *draggedObject(const QMimeData *mimeData, const QString &mimeType) {
    const QList<QByteArray> parts = mimeData->data(mimeType).split(' ');
    if(parts.size() != 2 || parts[0].toLongLong() != QCoreApplication::applicationPid()) {
        return nullptr;
    }
    return reinterpret_cast<const void *>(parts[1].toULongLong());
}

// Measurements can be dragged to another sample, depth profiles to another measurement. The default drag of
// QTreeWidget would remove the dragged row itself, so the drag is started here and the drop is handled in
// ProjectPanel::eventFilter().
class ProjectTree : public QTreeWidget {
protected:
    void startDrag(Qt::DropActions) override {
        QTreeWidgetItem *item = currentItem();
        if(!item || (nodeType(item) != MeasurementNode && nodeType(item) != DepthProfileNode)) {
            return;
        }
        const bool profile = nodeType(item) == DepthProfileNode;
        auto *mimeData = new QMimeData;
        mimeData->setData(profile ? DepthProfileMimeType : MeasurementMimeType, dragData(objectOf(item)));
        auto *drag = new QDrag(this);
        drag->setMimeData(mimeData);
        drag->setPixmap(item->icon(0).pixmap(16));
        drag->exec(profile ? Qt::CopyAction : Qt::MoveAction);
    }
};

// Selections of a measurement that correspond to those of another, with the same element or isotope and type. Each
// selection corresponds to at most one.
QList<int> correspondingCutIds(const QList<int> &ids, const Measurement &source, const Measurement &target) {
    QList<int> result;
    for(const int id : ids) {
        const Cut *cut = source.cut(id);
        if(!cut) {
            continue;
        }
        for(const Cut &candidate : target.cuts) {
            if(candidate.z == cut->z && candidate.a == cut->a && candidate.type == cut->type && !result.contains(candidate.id)) {
                result.append(candidate.id);
                break;
            }
        }
    }
    return result;
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
    QStringList experimentalNames, detectorNames, physicsNames;
    for(const auto &settings : collection.experimental) {
        experimentalNames.append(settings.name);
    }
    for(const auto &detector : collection.detectors) {
        detectorNames.append(detector.name);
    }
    for(const auto &physics : collection.physics) {
        physicsNames.append(physics.name);
    }
    addItems("Experimental settings", experimentalNames);
    addItems("Detector settings", detectorNames);
    addItems("Physics settings", physicsNames);
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
        // The list has the experimental, detector and physics settings in this order
        const qsizetype detectorsStart = collection.experimental.size();
        const qsizetype physicsStart = detectorsStart + collection.detectors.size();
        if(i < detectorsStart) {
            chosen.experimental.append(collection.experimental.at(i));
        } else if(i < physicsStart) {
            chosen.detectors.append(collection.detectors.at(i - detectorsStart));
        } else {
            chosen.physics.append(collection.physics.at(i - physicsStart));
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

    auto *measurementHint = new QLabel("Double-click to open the measurement in a tab, its ToF-E histogram is under it.");
    measurementHint->setWordWrap(true);
    auto *measurementLayout = new QVBoxLayout;
    measurementLayout->addWidget(measurementHint);
    measurementLayout->addStretch();
    m_pages->insertWidget(MeasurementPage, createPage(measurementLayout));

    auto *histogramHint = new QLabel("Double-click to open the ToF-E histogram in a tab and work with its selections.");
    histogramHint->setWordWrap(true);
    auto *histogramLayout = new QVBoxLayout;
    histogramLayout->addWidget(histogramHint);
    histogramLayout->addStretch();
    m_pages->insertWidget(HistogramPage, createPage(histogramLayout));

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

    m_splitNameEdit = new QLineEdit;
    auto *splitHint = new QLabel("Double-click to open the split analysis in a tab.");
    splitHint->setWordWrap(true);
    auto *splitLayout = new QFormLayout;
    splitLayout->addRow("Name:", m_splitNameEdit);
    splitLayout->addRow(splitHint);
    m_pages->insertWidget(SplitPage, createPage(splitLayout));

    auto *projectHint = new QLabel("Double-click to open the project information in a tab.");
    projectHint->setWordWrap(true);
    auto *projectLayout = new QVBoxLayout;
    projectLayout->addWidget(projectHint);
    projectLayout->addStretch();
    m_pages->insertWidget(ProjectPage, createPage(projectLayout));

    m_reportNameEdit = new QLineEdit;
    auto *reportHint = new QLabel("Double-click to open the report in a tab.");
    reportHint->setWordWrap(true);
    auto *reportLayout = new QFormLayout;
    reportLayout->addRow("Name:", m_reportNameEdit);
    reportLayout->addRow(reportHint);
    m_pages->insertWidget(ReportPage, createPage(reportLayout));

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
    connect(m_reportNameEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        Report *report = currentReport();
        report->name = text;
        m_tree->currentItem()->setText(0, displayName(text));
        emit reportRenamed(report);
        emit modified();
    });
    connect(m_splitNameEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        SplitAnalysis *split = currentSplit();
        split->name = text;
        m_tree->currentItem()->setText(0, displayName(text));
        emit splitRenamed(split);
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
            m_tree->addTopLevelItem(createProjectItem(*project));
            for(size_t i = 0; i < project->settingsCount(); i++) {
                m_tree->addTopLevelItem(createSettingsItem(*project->settings(i)));
            }
            for(size_t i = 0; i < project->detectorCount(); i++) {
                m_tree->addTopLevelItem(createDetectorItem(*project->detector(i)));
            }
            for(size_t i = 0; i < project->physicsCount(); i++) {
                m_tree->addTopLevelItem(createPhysicsItem(*project->physics(i)));
            }
            for(size_t i = 0; i < project->sampleCount(); i++) {
                m_tree->addTopLevelItem(createSampleItem(*project->sample(i)));
            }
            for(size_t i = 0; i < project->reportCount(); i++) {
                m_tree->addTopLevelItem(createReportItem(*project->report(i), static_cast<int>(i)));
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

QTreeWidgetItem *ProjectPanel::createPhysicsItem(const PhysicsSettings &physics) {
    auto *item = new QTreeWidgetItem({displayName(physics.name)});
    item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled);
    item->setData(0, TypeRole, PhysicsNode);
    item->setIcon(0, style()->standardIcon(QStyle::SP_FileDialogDetailedView));
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
    return FirstSettingsRow + static_cast<int>(m_project->settingsCount() + m_project->detectorCount() + m_project->physicsCount());
}

void ProjectPanel::updateProjectName() {
    if(m_project) {
        m_tree->topLevelItem(0)->setText(0, m_project->displayName());
    }
}

int ProjectPanel::firstReportRow() const {
    return firstSampleRow() + static_cast<int>(m_project->sampleCount());
}

Report *ProjectPanel::currentReport() const {
    QTreeWidgetItem *item = m_tree->currentItem();
    if(!m_project || !item || nodeType(item) != ReportNode) {
        return nullptr;
    }
    return m_project->report(static_cast<size_t>(nodeIndex(item)));
}

Sample *ProjectPanel::sampleOf(QTreeWidgetItem *item) const {
    const int index = sampleIndexOf(item);
    return index >= 0 ? m_project->sample(index) : nullptr;
}

ExperimentalSettings *ProjectPanel::settingsOf(QTreeWidgetItem *item) const {
    if(!m_project || !item || nodeType(item) != SettingsNode) {
        return nullptr;
    }
    return m_project->settings(m_tree->indexOfTopLevelItem(item) - FirstSettingsRow);
}

DetectorSettings *ProjectPanel::detectorOf(QTreeWidgetItem *item) const {
    if(!m_project || !item || nodeType(item) != DetectorNode) {
        return nullptr;
    }
    return m_project->detector(m_tree->indexOfTopLevelItem(item) - FirstSettingsRow - m_project->settingsCount());
}

DetectorSettings *ProjectPanel::currentDetector() const {
    return detectorOf(m_tree->currentItem());
}

PhysicsSettings *ProjectPanel::physicsOf(QTreeWidgetItem *item) const {
    if(!m_project || !item || nodeType(item) != PhysicsNode) {
        return nullptr;
    }
    return m_project->physics(m_tree->indexOfTopLevelItem(item) - FirstSettingsRow - m_project->settingsCount()
                              - m_project->detectorCount());
}

PhysicsSettings *ProjectPanel::currentPhysics() const {
    return physicsOf(m_tree->currentItem());
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
    if(isAnalysisNode(item)) {
        item = item->parent();
    }
    return currentSample()->measurements.at(nodeIndex(item)).get();
}

SplitAnalysis *ProjectPanel::currentSplit() const {
    QTreeWidgetItem *item = m_tree->currentItem();
    if(!m_project || !item || nodeType(item) != SplitNode) {
        return nullptr;
    }
    return currentMeasurement()->splitAnalyses.at(nodeIndex(item)).get();
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
    emit currentPhysicsChanged(physicsOf(item));
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
        case MeasurementNode:
            m_pages->setCurrentIndex(MeasurementPage);
            break;
        case HistogramNode:
            m_pages->setCurrentIndex(HistogramPage);
            break;
        case FileNode: {
            const AttachedFile &file = currentFile();
            m_filePathLabel->setText(file.path);
            m_fileDescriptionEdit->setText(file.description);
            m_pages->setCurrentIndex(FilePage);
            break;
        }
        case SettingsNode:
        case DetectorNode:
        case PhysicsNode:
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
        case SplitNode:
            m_splitNameEdit->setText(currentSplit()->name);
            m_pages->setCurrentIndex(SplitPage);
            break;
        case ReportNode:
            m_reportNameEdit->setText(currentReport()->name);
            m_pages->setCurrentIndex(ReportPage);
            break;
        case ProjectNode:
            m_pages->setCurrentIndex(ProjectPage);
            break;
    }
}

void ProjectPanel::measurementRenamedElsewhere(Measurement *measurement) {
    if(QTreeWidgetItem *item = measurementItem(measurement)) {
        item->setText(0, measurement->name);
    }
}

void ProjectPanel::measurementRenameFinished() {
    sortSamples(); // By name, the selection is kept
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
        } else if(const PhysicsSettings *physics = physicsOf(item)) {
            item->setText(0, displayName(physics->name));
        }
    }
}

void ProjectPanel::itemDoubleClicked(QTreeWidgetItem *item) {
    m_tree->setCurrentItem(item); // The actions below work on the current item
    switch(nodeType(item)) {
        case MeasurementNode:
            emit measurementInfoOpenRequested(currentMeasurement());
            break;
        case HistogramNode:
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
        case PhysicsNode:
            emit physicsOpenRequested(currentPhysics());
            break;
        case SpectrumNode:
            emit spectrumOpenRequested(currentMeasurement(), currentSpectrum());
            break;
        case DepthProfileNode:
            emit depthProfileOpenRequested(currentMeasurement(), currentDepthProfile());
            break;
        case SplitNode:
            emit splitOpenRequested(currentMeasurement(), currentSplit());
            break;
        case ReportNode:
            emit reportOpenRequested(currentReport());
            break;
        case ProjectNode:
            emit projectInfoOpenRequested();
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
        if(nodeType(item) == SettingsNode || nodeType(item) == DetectorNode || nodeType(item) == PhysicsNode) {
            menu.addAction("Edit", this, [this, item]() { itemDoubleClicked(item); });
            menu.addSeparator();
            menu.addAction(m_copySettingsAction);
            menu.addAction("Export...", this, &ProjectPanel::exportSettings);
            menu.addSeparator();
            if(nodeType(item) == SettingsNode) {
                menu.addAction("Remove Experimental Settings", this, &ProjectPanel::removeSettings);
            } else if(nodeType(item) == DetectorNode) {
                menu.addAction("Remove Detector Settings", this, &ProjectPanel::removeDetector);
            } else {
                menu.addAction("Remove Physics Settings", this, &ProjectPanel::removePhysics);
            }
        } else if(nodeType(item) == ProjectNode) {
            menu.addAction("Open Project Information", this, &ProjectPanel::projectInfoOpenRequested);
        } else if(nodeType(item) == ReportNode) {
            menu.addAction("Open Report", this, [this]() { emit reportOpenRequested(currentReport()); });
            menu.addAction("Remove Report", this, &ProjectPanel::removeCurrentReport);
        } else {
            if(nodeType(item) == MeasurementNode) {
                menu.addAction("Open Measurement", this, [this]() { emit measurementInfoOpenRequested(currentMeasurement()); });
                menu.addAction("Open ToF-E Histogram", this, [this]() { emit measurementOpenRequested(currentMeasurement()); });
                menu.addAction("Add Energy Spectrum", this, &ProjectPanel::addSpectrum);
                menu.addAction("Add Depth Profile", this, &ProjectPanel::addDepthProfile);
                menu.addAction("Add Split Analysis", this, &ProjectPanel::addSplit);
                menu.addAction("Remove Measurement", this, &ProjectPanel::removeCurrentMeasurement);
                menu.addSeparator();
            } else if(nodeType(item) == HistogramNode) {
                menu.addAction("Open ToF-E Histogram", this, [this]() { emit measurementOpenRequested(currentMeasurement()); });
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
            } else if(nodeType(item) == SplitNode) {
                menu.addAction("Open Split Analysis", this, [this]() {
                    emit splitOpenRequested(currentMeasurement(), currentSplit());
                });
                menu.addAction("Remove Split Analysis", this, &ProjectPanel::removeCurrentSplit);
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
    menu.addAction("Add Physics Settings", this, &ProjectPanel::addPhysics);
    menu.addAction("Add Report", this, &ProjectPanel::addReport);
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
        m_project->removeSettings(m_tree->indexOfTopLevelItem(item) - FirstSettingsRow);
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

void ProjectPanel::addPhysics() {
    if(!m_project) {
        return;
    }
    SettingsCollection collection;
    collection.physics.append(Defaults::physicsSettings());
    addSettingsCollection(collection);
    emit physicsOpenRequested(currentPhysics());
}

void ProjectPanel::addSettingsCollection(const SettingsCollection &collection) {
    // Names are made unique, the last added set becomes current
    QTreeWidgetItem *last = nullptr;
    for(ExperimentalSettings settings : collection.experimental) {
        settings.name = m_project->uniqueSettingsName(settings.name);
        last = createSettingsItem(*m_project->addSettings(settings));
        m_tree->insertTopLevelItem(FirstSettingsRow + static_cast<int>(m_project->settingsCount()) - 1, last); // After the other settings
    }
    for(DetectorSettings detector : collection.detectors) {
        detector.name = m_project->uniqueDetectorName(detector.name);
        last = createDetectorItem(*m_project->addDetector(detector));
        // After the other detectors, before the physics settings
        m_tree->insertTopLevelItem(FirstSettingsRow + static_cast<int>(m_project->settingsCount() + m_project->detectorCount()) - 1, last);
    }
    for(PhysicsSettings physics : collection.physics) {
        physics.name = m_project->uniquePhysicsName(physics.name);
        last = createPhysicsItem(*m_project->addPhysics(physics));
        m_tree->insertTopLevelItem(firstSampleRow() - 1, last); // After the other physics settings
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
    if(const PhysicsSettings *physics = currentPhysics()) {
        collection.physics.append(*physics);
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
    const QString name = !collection.experimental.isEmpty() ? collection.experimental.first().name
                         : !collection.detectors.isEmpty()  ? collection.detectors.first().name
                                                            : collection.physics.first().name;
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
                             errorMessage.isEmpty() ? QString("%1 has no experimental, detector or physics settings.").arg(fileName)
                                                    : QString("Could not import %1:\n%2").arg(fileName, errorMessage));
        return;
    }
    // Choose which settings to import from a file with several sets
    if(collection.size() > 1 && !chooseSettings(this, collection, fileName)) {
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
    QHash<const PhysicsSettings *, PhysicsSettings *> physicsCopies;
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
    for(size_t i = 0; i < source.physicsCount(); i++) {
        if(dialog.isSelected(source.physics(i))) {
            collection.physics.append(*source.physics(i));
        }
    }
    const size_t settingsBefore = m_project->settingsCount();
    const size_t detectorsBefore = m_project->detectorCount();
    const size_t physicsBefore = m_project->physicsCount();
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
    for(size_t i = 0, added = 0; i < source.physicsCount(); i++) {
        if(dialog.isSelected(source.physics(i))) {
            physicsCopies.insert(source.physics(i), m_project->physics(physicsBefore + added++));
        }
    }
    auto importMeasurement = [&](const Measurement &measurement) {
        auto copy = copyMeasurement(measurement);
        copy->settings = settingsCopies.value(measurement.settings);
        copy->detector = detectorCopies.value(measurement.detector);
        copy->physics = physicsCopies.value(measurement.physics);
        // As with new measurements, settings are assigned when there is no choice to make
        if(!copy->settings && m_project->settingsCount() == 1) {
            copy->settings = m_project->settings(0);
        }
        if(!copy->detector && m_project->detectorCount() == 1) {
            copy->detector = m_project->detector(0);
        }
        if(!copy->physics && m_project->physicsCount() == 1) {
            copy->physics = m_project->physics(0);
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
        if(dropEvent->mimeData()->hasFormat(DepthProfileMimeType)) {
            handleDepthProfileDrag(dropEvent, m_tree->itemAt(dropEvent->position().toPoint()));
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
    const void *dragged = draggedObject(event->mimeData(), MeasurementMimeType);
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

void ProjectPanel::handleDepthProfileDrag(QDropEvent *event, QTreeWidgetItem *target) {
    // Only depth profiles of this project, dropped on another measurement or the items under it
    const void *dragged = draggedObject(event->mimeData(), DepthProfileMimeType);
    QTreeWidgetItem *item = nullptr;
    for(QTreeWidgetItemIterator it(m_tree); *it && dragged && !item; ++it) {
        if(nodeType(*it) == DepthProfileNode && objectOf(*it) == dragged) {
            item = *it;
        }
    }
    if(target && isAnalysisNode(target)) {
        target = target->parent();
    }
    const bool onMeasurement = target && nodeType(target) == MeasurementNode && target != (item ? item->parent() : nullptr);
    // The drag is accepted when it enters, where it can be dropped is decided as it moves
    if(!item || (event->type() != QEvent::DragEnter && !onMeasurement)) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::CopyAction);
    event->accept();
    if(event->type() == QEvent::Drop) {
        const auto *profile = static_cast<const DepthProfile *>(dragged);
        auto *measurement = const_cast<Measurement *>(static_cast<const Measurement *>(objectOf(target)));
        // The tree is changed after the drag has finished
        QTimer::singleShot(0, this, [this, profile, measurement]() { copyDepthProfile(profile, measurement); });
    }
}

void ProjectPanel::copyDepthProfile(const DepthProfile *profile, Measurement *target) {
    // The source measurement, the profile may have been removed meanwhile
    const Measurement *source = nullptr;
    m_project->forEachMeasurement([&](const Measurement *measurement) {
        for(const auto &candidate : measurement->depthProfiles) {
            if(candidate.get() == profile) {
                source = measurement;
            }
        }
    });
    QTreeWidgetItem *targetItem = measurementItem(target);
    if(!source || !targetItem) {
        return;
    }
    // The settings are copied, the selections are those of the target with the same element or isotope and type
    auto copy = std::make_unique<DepthProfile>(*profile);
    copy->name = target->uniqueDepthProfileName(profile->name);
    copy->cutIds = correspondingCutIds(profile->cutIds, *source, *target);
    if(copy->cutIds.isEmpty()) {
        for(const Cut &cut : target->cuts) {
            copy->cutIds.append(cut.id); // None correspond, all are included as in a new profile
        }
    }
    copy->cutIdsExcludedFromAverage = correspondingCutIds(profile->cutIdsExcludedFromAverage, *source, *target);
    copy->cutIdsHiddenFromPlot = correspondingCutIds(profile->cutIdsHiddenFromPlot, *source, *target);
    copy->cutIdsWithoutLabel = correspondingCutIds(profile->cutIdsWithoutLabel, *source, *target);
    // Entries by cut id go to the corresponding cuts, the others are dropped
    auto corresponding = [source, target](int id) {
        const QList<int> ids = correspondingCutIds({id}, *source, *target);
        return ids.isEmpty() ? 0 : ids.first();
    };
    copy->labelPositions.clear();
    for(auto i = profile->labelPositions.begin(); i != profile->labelPositions.end(); ++i) {
        if(const int id = corresponding(i.key()); id > 0) {
            copy->labelPositions.insert(id, i.value());
        }
    }
    copy->cutRanges.clear();
    for(auto i = profile->cutRanges.begin(); i != profile->cutRanges.end(); ++i) {
        if(const int id = corresponding(i.key()); id > 0) {
            copy->cutRanges.insert(id, i.value());
        }
    }
    target->depthProfiles.push_back(std::move(copy));
    QTreeWidgetItem *profileItem = createDepthProfileItem(*target->depthProfiles.back(),
                                                          static_cast<int>(target->depthProfiles.size() - 1));
    // Depth profiles before split analyses
    targetItem->insertChild(static_cast<int>(target->spectra.size() + target->depthProfiles.size()), profileItem); // After the histogram
    targetItem->setExpanded(true);
    m_tree->setCurrentItem(profileItem);
    emit modified();
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
        m_project->removeDetector(m_tree->indexOfTopLevelItem(item) - FirstSettingsRow - m_project->settingsCount());
        delete item;
    }
    currentItemChanged(m_tree->currentItem());
    emit modified();
}

void ProjectPanel::removePhysics() {
    const PhysicsSettings *physics = currentPhysics();
    if(!physics) {
        return;
    }
    const qsizetype users = m_project->measurementsUsing(physics).size();
    QString question = QString("Remove physics settings \"%1\"?").arg(displayName(physics->name));
    if(users > 0) {
        question += QString(" %1 measurement(s) use them and will use the default physics settings.").arg(users);
    }
    if(QMessageBox::question(this, "Remove Physics Settings", question) != QMessageBox::Yes) {
        return;
    }
    emit physicsAboutToBeRemoved(currentPhysics());
    {
        const QSignalBlocker blocker(m_tree);
        QTreeWidgetItem *item = m_tree->currentItem();
        m_project->removePhysics(m_tree->indexOfTopLevelItem(item) - FirstSettingsRow - m_project->settingsCount()
                                 - m_project->detectorCount());
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
        // Only the sample items, the reports after them stay
        for(int row = m_tree->topLevelItemCount() - 1; row >= firstSampleRow(); row--) {
            QTreeWidgetItem *item = m_tree->topLevelItem(row);
            if(nodeType(item) != SampleNode) {
                continue;
            }
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
            m_tree->insertTopLevelItem(firstSampleRow() + static_cast<int>(i), item);
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
    QApplication::setOverrideCursor(Qt::WaitCursor); // Checksums of large files take a while
    for(const QString &fileName : fileNames) {
        const QFileInfo fileInfo(fileName);
        auto measurement = std::make_unique<Measurement>(fileInfo.fileName(), QString(), fileInfo.absoluteFilePath());
        measurement->sha256 = fileChecksum(measurement->path);
        // When there is no choice to make, assign the settings right away
        if(m_project->settingsCount() == 1) {
            measurement->settings = m_project->settings(0);
        }
        if(m_project->detectorCount() == 1) {
            measurement->detector = m_project->detector(0);
        }
        if(m_project->physicsCount() == 1) {
            measurement->physics = m_project->physics(0);
        }
        last = measurement.get();
        sample->measurements.push_back(std::move(measurement));
    }
    QApplication::restoreOverrideCursor();
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
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for(const QString &fileName : fileNames) {
        const QString path = QFileInfo(fileName).absoluteFilePath();
        sample->files.append(AttachedFile{path, {}, fileChecksum(path)});
        lastItem = createFileItem(sample->files.last(), static_cast<int>(sample->files.size() - 1));
        item->addChild(lastItem);
    }
    QApplication::restoreOverrideCursor();
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
    m_project->removeFromReports(measurements.at(index).get());
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
        if(isAnalyzed(cut)) {
            spectrum->cutIds.append(cut.id);
        }
    }
    measurement->spectra.push_back(std::move(spectrum));
    QTreeWidgetItem *spectrumItem = createSpectrumItem(*measurement->spectra.back(), static_cast<int>(measurement->spectra.size() - 1));
    item->insertChild(static_cast<int>(measurement->spectra.size()), spectrumItem); // After the histogram, before depth profiles
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
    if(isAnalysisNode(item)) {
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
        if(isAnalyzed(cut)) {
            profile->cutIds.append(cut.id);
        }
    }
    measurement->depthProfiles.push_back(std::move(profile));
    QTreeWidgetItem *profileItem = createDepthProfileItem(*measurement->depthProfiles.back(),
                                                          static_cast<int>(measurement->depthProfiles.size() - 1));
    // Depth profiles before split analyses
    item->insertChild(static_cast<int>(measurement->spectra.size() + measurement->depthProfiles.size()), profileItem); // After the histogram
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
    m_project->removeFromReports(profiles.at(index).get());
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

void ProjectPanel::addSplit() {
    QTreeWidgetItem *item = m_tree->currentItem();
    if(!m_project || !item) {
        return;
    }
    if(isAnalysisNode(item)) {
        item = item->parent();
    }
    if(nodeType(item) != MeasurementNode) {
        return;
    }
    m_tree->setCurrentItem(item);
    Measurement *measurement = currentMeasurement();
    auto split = std::make_unique<SplitAnalysis>();
    split->name = measurement->uniqueSplitAnalysisName("Split analysis");
    for(const Cut &cut : measurement->cuts) {
        split->cutIds.append(cut.id);
    }
    measurement->splitAnalyses.push_back(std::move(split));
    QTreeWidgetItem *splitItem = createSplitItem(*measurement->splitAnalyses.back(),
                                                 static_cast<int>(measurement->splitAnalyses.size() - 1));
    item->addChild(splitItem);
    item->setExpanded(true);
    m_tree->setCurrentItem(splitItem);
    emit modified();
    emit splitOpenRequested(measurement, currentSplit());
}

void ProjectPanel::removeCurrentSplit() {
    QTreeWidgetItem *item = m_tree->currentItem();
    if(!item || nodeType(item) != SplitNode) {
        return;
    }
    QTreeWidgetItem *parent = item->parent();
    auto &splits = currentMeasurement()->splitAnalyses;
    const int index = nodeIndex(item);
    emit splitAboutToBeRemoved(splits.at(index).get());
    m_project->removeFromReports(splits.at(index).get());
    {
        const QSignalBlocker blocker(m_tree);
        splits.erase(splits.begin() + index);
        delete item;
        renumberChildren(parent, SplitNode);
        m_tree->setCurrentItem(parent);
    }
    currentItemChanged(parent);
    emit modified();
}

void ProjectPanel::addReport() {
    if(!m_project) {
        return;
    }
    Report *report = m_project->addReport("Report");
    QTreeWidgetItem *item = createReportItem(*report, static_cast<int>(m_project->reportCount() - 1));
    m_tree->addTopLevelItem(item);
    m_tree->setCurrentItem(item);
    emit modified();
    emit reportOpenRequested(report);
}

void ProjectPanel::removeCurrentReport() {
    QTreeWidgetItem *item = m_tree->currentItem();
    if(!item || nodeType(item) != ReportNode) {
        return;
    }
    const int index = nodeIndex(item);
    emit reportAboutToBeRemoved(m_project->report(static_cast<size_t>(index)));
    {
        const QSignalBlocker blocker(m_tree);
        m_project->removeReport(static_cast<size_t>(index));
        delete item;
        // Renumbered, the reports are the last top-level items
        for(int row = firstReportRow(); row < m_tree->topLevelItemCount(); row++) {
            m_tree->topLevelItem(row)->setData(0, IndexRole, row - firstReportRow());
        }
        m_tree->setCurrentItem(nullptr);
    }
    currentItemChanged(nullptr);
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
