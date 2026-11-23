/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#ifndef JARA_PROJECTINFOVIEW_H
#define JARA_PROJECTINFOVIEW_H

#include <QWidget>

class AttachedFileTable;
class Project;
class QLabel;
class QLineEdit;
class QPlainTextEdit;

// Tab in the main work area with the project's information: its name, shown in reports, its file, when it was created
// and saved, notes and the files attached to the project.
class ProjectInfoView : public QWidget {
    Q_OBJECT

public:
    explicit ProjectInfoView(Project *project, QWidget *parent = nullptr);

public slots:
    void refresh(); // Call after the project has been saved or changed elsewhere

signals:
    void modified();
    void nameChanged();

private:
    Project *m_project;
    QLineEdit *m_nameEdit;
    QLabel *m_fileLabel;
    QLabel *m_createdLabel;
    QLabel *m_savedLabel;
    QPlainTextEdit *m_notesEdit;
    AttachedFileTable *m_fileTable;
};

#endif // JARA_PROJECTINFOVIEW_H
