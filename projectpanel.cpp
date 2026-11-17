#include "projectpanel.h"
#include "project.h"

#include <algorithm>

#include <QApplication>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTableWidget>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {
// Under a sample node, measurements come first, followed by attached files
enum NodeType { SampleNode, MeasurementNode, FileNode };
enum Page { EmptyPage, SamplePage, MeasurementPage, FilePage };
constexpr int TypeRole = Qt::UserRole;
constexpr int IndexRole = Qt::UserRole + 1; // Index in the sample's measurements or files

QString sampleLabel(const QString &name) {
    return name.isEmpty() ? QStringLiteral("(unnamed)") : name;
}

NodeType nodeType(const QTreeWidgetItem *item) {
    return static_cast<NodeType>(item->data(0, TypeRole).toInt());
}

int nodeIndex(const QTreeWidgetItem *item) {
    return item->data(0, IndexRole).toInt();
}

QTreeWidgetItem *sampleItem(QTreeWidgetItem *item) {
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
    auto *measurementLayout = new QFormLayout;
    measurementLayout->addRow("Name:", m_measurementNameEdit);
    measurementLayout->addRow("Description:", m_measurementDescriptionEdit);
    measurementLayout->addRow("Data file:", m_measurementPathLabel);
    m_pages->insertWidget(MeasurementPage, createPage(measurementLayout));

    m_filePathLabel = pathLabel();
    m_fileDescriptionEdit = new QLineEdit;
    auto *fileLayout = new QFormLayout;
    fileLayout->addRow("Path:", m_filePathLabel);
    fileLayout->addRow("Description:", m_fileDescriptionEdit);
    m_pages->insertWidget(FilePage, createPage(fileLayout));

    connect(m_sampleNameEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        QTreeWidgetItem *item = sampleItem(m_tree->currentItem());
        sampleOf(item)->name = text;
        item->setText(0, sampleLabel(text));
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
    connect(m_fileDescriptionEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        currentFile().description = text;
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
            for(size_t i = 0; i < project->sampleCount(); i++) {
                m_tree->addTopLevelItem(createSampleItem(*project->sample(i)));
            }
            m_tree->expandAll();
            m_tree->setCurrentItem(m_tree->topLevelItem(0));
        }
    }
    currentItemChanged(m_tree->currentItem());
}

QTreeWidgetItem *ProjectPanel::createSampleItem(const Sample &sample) {
    auto *item = new QTreeWidgetItem({sampleLabel(sample.name)});
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

Sample *ProjectPanel::sampleOf(QTreeWidgetItem *item) const {
    if(!m_project || !item) {
        return nullptr;
    }
    return m_project->sample(m_tree->indexOfTopLevelItem(sampleItem(item)));
}

Sample *ProjectPanel::currentSample() const {
    return sampleOf(m_tree->currentItem());
}

AttachedFile &ProjectPanel::currentFile() const {
    return currentSample()->files[nodeIndex(m_tree->currentItem())];
}

Measurement *ProjectPanel::currentMeasurement() const {
    return currentSample()->measurements.at(nodeIndex(m_tree->currentItem())).get();
}

void ProjectPanel::currentItemChanged(QTreeWidgetItem *item) {
    showProperties(item);
    emit currentSampleChanged(sampleOf(item));
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
    }
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
        menu.addSeparator();
    }
    menu.addAction("Add Sample", this, &ProjectPanel::addSample);
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
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
                                                      .arg(sampleLabel(sample->name)));
    if(answer != QMessageBox::Yes) {
        return;
    }
    emit sampleAboutToBeRemoved(sample);
    {
        // Tree and project are updated together before the properties are refreshed for the new current item
        const QSignalBlocker blocker(m_tree);
        QTreeWidgetItem *item = sampleItem(m_tree->currentItem());
        m_project->removeSample(m_tree->indexOfTopLevelItem(item));
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
    QTreeWidgetItem *item = sampleItem(m_tree->currentItem());
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
    QTreeWidgetItem *item = sampleItem(m_tree->currentItem());
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
