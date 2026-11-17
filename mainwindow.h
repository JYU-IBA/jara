#ifndef JARA_MAINWINDOW_H
#define JARA_MAINWINDOW_H

#include <memory>

#include <QMainWindow>

#include "project.h"

class ProjectPanel;
class QMenu;
class QStackedWidget;
class QTabWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    void openProjectFile(const QString &fileName);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void newProject();
    void openProject();
    bool saveProject();
    bool saveProjectAs();
    void closeProject();
    void projectModified();
    void openMeasurement(Measurement *measurement);
    void measurementRenamed(Measurement *measurement);
    void closeSampleTabs(Sample *sample);
    void closeMeasurementTab(Measurement *measurement);
    void closeTab(int index);
    void about();
    void openRecentFile(const QString &fileName);
    void clearRecentFiles();

private:
    void createActions();
    void createMenus();
    QWidget *createProjectView();
    bool maybeSave();
    void setProject(std::unique_ptr<Project> project);
    void updateWindowState();
    int measurementTabIndex(const Measurement *measurement) const;
    void addRecentFile(const QString &fileName);
    void removeRecentFile(const QString &fileName);
    void updateRecentFilesMenu();

    std::unique_ptr<Project> m_project; // nullptr when no project is open
    QStackedWidget *m_stack;
    ProjectPanel *m_projectPanel;
    QStackedWidget *m_workArea; // Hint when no measurements are open, tabs otherwise
    QTabWidget *m_tabs;
    QAction *m_newAction;
    QAction *m_openAction;
    QMenu *m_recentFilesMenu;
    QAction *m_saveAction;
    QAction *m_saveAsAction;
    QAction *m_closeAction;
    QAction *m_quitAction;
    QAction *m_addSampleAction;
    QAction *m_removeSampleAction;
    QAction *m_addFilesAction;
    QAction *m_addMeasurementsAction;
};

#endif // JARA_MAINWINDOW_H
