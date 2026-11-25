/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#ifndef JARA_MAINWINDOW_H
#define JARA_MAINWINDOW_H

#include <memory>

#include <QMainWindow>

#include "project.h"

class DepthProfileView;
class SplitView;
class ReportView;
class DetachableTabWidget;
class EnergySpectrumView;
class ProjectPanel;
class QMenu;
class QMenuBar;
class QStackedWidget;
class QTreeWidget;
class SettingsView;
class AngleCalibrationWindow;
class ToFCalibrationWindow;

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
    void exportArchive();
    void showFileProblems(const QList<Project::FileProblem> &problems); // Of a project just opened
    void closeProject();
    void projectModified();
    void openMeasurement(Measurement *measurement); // Its ToF-E histogram
    void openMeasurementInfo(Measurement *measurement);
    void openSample(Sample *sample);
    void openImage(Sample *sample, const QString &path);
    void openSpectrum(Measurement *measurement, EnergySpectrum *spectrum);
    void openDepthProfile(Measurement *measurement, DepthProfile *profile);
    void openSplit(Measurement *measurement, SplitAnalysis *split);
    void openReport(Report *report);
    void openProjectInfo();
    void newTab(); // Empty, the next item opened from the project tree is shown in it
    void openSettings(ExperimentalSettings *settings);
    void openDetector(DetectorSettings *detector);
    void openPhysics(PhysicsSettings *physics);
    void openToFCalibration(DetectorSettings *detector);
    void openAngleCalibration(DetectorSettings *detector);
    void measurementRenamed(Measurement *measurement);
    void closeSampleTabs(Sample *sample);
    void closeMeasurementTab(Measurement *measurement);
    void about();
    void openRecentFile(const QString &fileName);
    void clearRecentFiles();

private:
    void createActions();
    void createMenus(QMenuBar *menuBar); // Also for the tab windows, the actions are shared
    QWidget *createStartPage(); // Shown when no project is open
    QWidget *createProjectView();
    void connectTabs(DetachableTabWidget *tabs);
    void detachTab(DetachableTabWidget *tabs, int index, const QPoint &globalPos);
    void closeTab(DetachableTabWidget *tabs, int index);
    void closeTab(QWidget *page); // Wherever it is
    void showTab(QWidget *page, const QString &title, const QString &toolTip); // Opened in the main window
    void connectSettingsView(SettingsView *view);
    // And depth profiles when their tabs are shown, after settings they depend on were edited
    void recalculateStaleSpectra();
    void closeCalibrations(const DetectorSettings *detector); // The ToF and angle calibrations, nullptr closes all
    void calibrationApplied(DetectorSettings *detector);
    void updateSpectrumTitle(EnergySpectrumView *view); // nullptr is ignored
    void updateDepthProfileTitle(DepthProfileView *view); // nullptr is ignored
    void updateSplitTitle(SplitView *view); // nullptr is ignored
    void updateReportTitle(ReportView *view); // nullptr is ignored
    void projectSaved(); // Updates what shows when it was saved
    static QString histogramTitle(const Measurement *measurement); // Of the tab
    QList<QWidget *> tabPages() const; // In all windows
    // Tab widget and index of the page, nullptr when it is not in a tab
    std::pair<DetachableTabWidget *, int> findTab(const QWidget *page) const;
    bool maybeSave();
    void setProject(std::unique_ptr<Project> project);
    void updateWindowState();
    template<typename View, typename Predicate> View *findView(Predicate predicate) const;
    void addRecentFile(const QString &fileName);
    void removeRecentFile(const QString &fileName);
    void updateRecentFilesMenu();

    std::unique_ptr<Project> m_project; // nullptr when no project is open
    QStackedWidget *m_stack;
    ProjectPanel *m_projectPanel;
    DetachableTabWidget *m_tabs; // In the main window, more can be in tab windows
    QAction *m_newAction;
    QAction *m_openAction;
    QList<QMenu *> m_recentFilesMenus; // One in each window
    QTreeWidget *m_recentList; // On the start page
    QAction *m_saveAction;
    QAction *m_saveAsAction;
    QAction *m_exportArchiveAction;
    QAction *m_convertCoincidencesAction;
    QAction *m_closeAction;
    QAction *m_closeTabAction;
    QAction *m_preferencesAction;
    QAction *m_importSettingsAction;
    QAction *m_quitAction;
    QAction *m_newTabAction;
    bool m_openInNewTab = false; // The next item opened from the project tree, see showTab()
    QAction *m_addSampleAction;
    QAction *m_addReportAction;
    QAction *m_removeSampleAction;
    QAction *m_addFilesAction;
    QAction *m_addMeasurementsAction;
    QAction *m_addSettingsAction;
    QAction *m_removeSettingsAction;
    QAction *m_addDetectorAction;
    QAction *m_removeDetectorAction;
    QAction *m_addPhysicsAction;
    QAction *m_removePhysicsAction;
};

#endif // JARA_MAINWINDOW_H
