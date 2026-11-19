#ifndef JARA_PROJECTINFOVIEW_H
#define JARA_PROJECTINFOVIEW_H

#include <QWidget>

class Project;
class QLabel;
class QLineEdit;
class QPlainTextEdit;

// Tab in the main work area with the project's information: its name, shown in reports, its file, when it was created
// and saved, and notes.
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
};

#endif // JARA_PROJECTINFOVIEW_H
