/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#include "attachedfiletable.h"
#include "project.h"

#include <QFileInfo>
#include <QHeaderView>

namespace {
enum Column { NameColumn, DescriptionColumn };
}

AttachedFileTable::AttachedFileTable(QList<AttachedFile> *files, QWidget *parent) : QTableWidget(0, 2, parent), m_files(files) {
    setHorizontalHeaderLabels({"File", "Description"});
    horizontalHeader()->setStretchLastSection(true);
    verticalHeader()->hide();
    setToolTip("Files are added and removed in the project tree");
    connect(this, &QTableWidget::itemChanged, this, &AttachedFileTable::itemEdited);
    refresh();
}

void AttachedFileTable::refresh() {
    QStringList paths;
    for(const AttachedFile &file : *m_files) {
        paths.append(file.path);
    }
    if(paths == m_shownPaths) {
        return;
    }
    m_shownPaths = paths;
    const QSignalBlocker blocker(this);
    setRowCount(static_cast<int>(m_files->size()));
    for(int row = 0; row < m_files->size(); row++) {
        const AttachedFile &file = m_files->at(row);
        auto *name = new QTableWidgetItem(QFileInfo(file.path).fileName());
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        name->setToolTip(file.path);
        setItem(row, NameColumn, name);
        setItem(row, DescriptionColumn, new QTableWidgetItem(file.description));
    }
    resizeColumnToContents(NameColumn);
}

void AttachedFileTable::itemEdited(QTableWidgetItem *item) {
    if(item->column() != DescriptionColumn || item->row() >= m_files->size()) {
        return;
    }
    (*m_files)[item->row()].description = item->text();
    emit modified();
}
