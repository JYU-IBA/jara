/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#include "sampleview.h"
#include "attachedfiletable.h"
#include "formlayout.h"
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

    m_fileTable = new AttachedFileTable(&sample->files);
    auto *fileGroup = new QGroupBox("Attached files");
    auto *fileLayout = new QVBoxLayout(fileGroup);
    fileLayout->addWidget(m_fileTable);

    auto *form = createFormLayout();
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
    connect(m_fileTable, &AttachedFileTable::modified, this, &SampleView::modified);
    updateMetadata();
    refresh();
}

void SampleView::refresh() {
    // Not while the user types
    if(!m_nameEdit->hasFocus()) {
        m_nameEdit->setText(m_sample->name);
    }
    m_fileTable->refresh();
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

