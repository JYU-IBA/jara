/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#include "projectinfoview.h"
#include "attachedfiletable.h"
#include "formlayout.h"
#include "project.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QVBoxLayout>

ProjectInfoView::ProjectInfoView(Project *project, QWidget *parent) : QWidget(parent), m_project(project) {
    m_nameEdit = new QLineEdit(project->name());
    m_nameEdit->setToolTip("Shown in the project tree and in reports. The file name is used when empty.");
    m_fileLabel = new QLabel;
    m_fileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_fileLabel->setWordWrap(true);
    m_createdLabel = new QLabel;
    m_savedLabel = new QLabel;
    m_notesEdit = new QPlainTextEdit(project->notes());
    m_notesEdit->setPlaceholderText("Notes of the project");

    m_fileTable = new AttachedFileTable(&project->files());
    m_fileTable->setToolTip("Files of the project as a whole. They are added and removed in the project tree, from the "
                            "context menu of the project.");
    auto *fileGroup = new QGroupBox("Attached files");
    auto *fileLayout = new QVBoxLayout(fileGroup);
    fileLayout->addWidget(m_fileTable);

    auto *form = createFormLayout();
    form->addRow("Name:", m_nameEdit);
    form->addRow("File:", m_fileLabel);
    form->addRow("Created:", m_createdLabel);
    form->addRow("Saved:", m_savedLabel);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(new QLabel("Notes:"));
    layout->addWidget(m_notesEdit, 1);
    layout->addWidget(fileGroup, 1);
    connect(m_fileTable, &AttachedFileTable::modified, this, &ProjectInfoView::modified);

    connect(m_nameEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        m_project->setName(text.trimmed());
        emit nameChanged();
        emit modified();
    });
    connect(m_notesEdit, &QPlainTextEdit::textChanged, this, [this]() {
        if(m_notesEdit->toPlainText() != m_project->notes()) {
            m_project->setNotes(m_notesEdit->toPlainText());
            emit modified();
        }
    });
    refresh();
}

void ProjectInfoView::refresh() {
    const QString format = QStringLiteral("yyyy-MM-dd HH:mm:ss");
    if(!m_nameEdit->hasFocus()) {
        m_nameEdit->setText(m_project->name());
    }
    m_nameEdit->setPlaceholderText(m_project->displayName());
    m_fileLabel->setText(m_project->fileName().isEmpty() ? QStringLiteral("Not saved yet") : m_project->fileName());
    m_createdLabel->setText(m_project->created().isValid() ? m_project->created().toString(format) : QStringLiteral("Unknown"));
    m_savedLabel->setText(m_project->saved().isValid() ? m_project->saved().toString(format) : QStringLiteral("Not saved yet"));
    m_fileTable->refresh();
}
