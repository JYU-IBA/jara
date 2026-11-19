#include "sampleview.h"
#include "project.h"

#include <algorithm>

#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
enum FileColumn { FileNameColumn, FileDescriptionColumn };
}

SampleView::SampleView(Sample *sample, QWidget *parent) : QWidget(parent), m_sample(sample) {
    m_nameEdit = new QLineEdit(sample->name);
    m_descriptionEdit = new QPlainTextEdit(sample->description);
    m_descriptionEdit->setMaximumHeight(m_descriptionEdit->fontMetrics().height() * 6);

    m_metadataTable = new QTableWidget(0, 2);
    m_metadataTable->setHorizontalHeaderLabels({"Key", "Value"});
    m_metadataTable->horizontalHeader()->setStretchLastSection(true);
    m_metadataTable->verticalHeader()->hide();
    m_metadataTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_metadataTable->setContextMenuPolicy(Qt::CustomContextMenu);
    m_metadataTable->setToolTip("Right-click to add or remove entries");
    auto *metadataGroup = new QGroupBox("Metadata");
    auto *metadataLayout = new QVBoxLayout(metadataGroup);
    metadataLayout->addWidget(m_metadataTable);

    m_fileTable = new QTableWidget(0, 2);
    m_fileTable->setHorizontalHeaderLabels({"File", "Description"});
    m_fileTable->horizontalHeader()->setStretchLastSection(true);
    m_fileTable->verticalHeader()->hide();
    m_fileTable->setToolTip("Files are added and removed in the project tree");
    auto *fileGroup = new QGroupBox("Attached files");
    auto *fileLayout = new QVBoxLayout(fileGroup);
    fileLayout->addWidget(m_fileTable);

    auto *form = new QFormLayout;
    form->addRow("Name:", m_nameEdit);
    form->addRow("Description:", m_descriptionEdit);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(metadataGroup, 1);
    layout->addWidget(fileGroup, 1);

    connect(m_nameEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        m_sample->name = text;
        emit renamed(m_sample);
        emit modified();
    });
    // Renamed samples are moved to their place when the name is finished, not while typing
    connect(m_nameEdit, &QLineEdit::editingFinished, this, [this]() {
        if(m_nameEdit->isModified()) {
            m_nameEdit->setModified(false);
            emit renameFinished();
        }
    });
    connect(m_descriptionEdit, &QPlainTextEdit::textChanged, this, [this]() {
        if(m_descriptionEdit->toPlainText() != m_sample->description) {
            m_sample->description = m_descriptionEdit->toPlainText();
            emit modified();
        }
    });
    connect(m_metadataTable, &QTableWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu;
        menu.addAction("Add Entry", this, &SampleView::addMetadata);
        if(m_metadataTable->selectionModel()->hasSelection()) {
            menu.addAction("Remove Entry", this, &SampleView::removeMetadata);
        }
        menu.exec(m_metadataTable->viewport()->mapToGlobal(pos));
    });
    connect(m_metadataTable, &QTableWidget::itemChanged, this, &SampleView::metadataItemChanged);
    connect(m_fileTable, &QTableWidget::itemChanged, this, &SampleView::fileItemChanged);
    updateMetadata();
    refresh();
}

void SampleView::refresh() {
    // Not while the user types
    if(!m_nameEdit->hasFocus()) {
        m_nameEdit->setText(m_sample->name);
    }
    updateFiles();
}

void SampleView::updateMetadata() {
    const QSignalBlocker blocker(m_metadataTable);
    m_metadataTable->setRowCount(static_cast<int>(m_sample->metadata.size()));
    for(int row = 0; row < m_sample->metadata.size(); row++) {
        m_metadataTable->setItem(row, 0, new QTableWidgetItem(m_sample->metadata[row].key));
        m_metadataTable->setItem(row, 1, new QTableWidgetItem(m_sample->metadata[row].value));
    }
}

void SampleView::addMetadata() {
    m_sample->metadata.append(MetadataEntry{});
    updateMetadata();
    const int row = m_metadataTable->rowCount() - 1;
    m_metadataTable->setCurrentCell(row, 0);
    m_metadataTable->editItem(m_metadataTable->item(row, 0));
    emit modified();
}

void SampleView::removeMetadata() {
    QList<int> rows;
    for(const QModelIndex &index : m_metadataTable->selectionModel()->selectedRows()) {
        rows.append(index.row());
    }
    std::sort(rows.begin(), rows.end(), std::greater<>());
    for(const int row : rows) {
        m_sample->metadata.removeAt(row);
        m_metadataTable->removeRow(row);
    }
    if(!rows.isEmpty()) {
        emit modified();
    }
}

void SampleView::metadataItemChanged(QTableWidgetItem *item) {
    MetadataEntry &entry = m_sample->metadata[item->row()];
    (item->column() == 0 ? entry.key : entry.value) = item->text();
    emit modified();
}

void SampleView::updateFiles() {
    QStringList key;
    for(const AttachedFile &file : m_sample->files) {
        key.append(file.path);
    }
    if(key == m_filesKey) {
        return;
    }
    m_filesKey = key;
    const QSignalBlocker blocker(m_fileTable);
    m_fileTable->setRowCount(static_cast<int>(m_sample->files.size()));
    for(int row = 0; row < m_sample->files.size(); row++) {
        const AttachedFile &file = m_sample->files[row];
        auto *name = new QTableWidgetItem(QFileInfo(file.path).fileName());
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        name->setToolTip(file.path);
        m_fileTable->setItem(row, FileNameColumn, name);
        m_fileTable->setItem(row, FileDescriptionColumn, new QTableWidgetItem(file.description));
    }
    m_fileTable->resizeColumnToContents(FileNameColumn);
}

void SampleView::fileItemChanged(QTableWidgetItem *item) {
    if(item->column() != FileDescriptionColumn || item->row() >= m_sample->files.size()) {
        return;
    }
    m_sample->files[item->row()].description = item->text();
    emit modified();
}
