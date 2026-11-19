#ifndef JARA_MAINWINDOW_H
#define JARA_MAINWINDOW_H

#include <memory>

#include <QMainWindow>

#include "project.h"

class DepthProfileView;
class DetachableTabWidget;
class EnergySpectrumView;
class ProjectPanel;
class QMenu;
class QMenuBar;
class QStackedWidget;
class SettingsView;
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
    void closeProject();
    void projectModified();
    void openMeasurement(Measurement *measurement);
    void openSpectrum(Measurement *measurement, EnergySpectrum *spectrum);
    void openDepthProfile(Measurement *measurement, DepthProfile *profile);
    void openSettings(ExperimentalSettings *settings);
    void openDetector(DetectorSettings *detector);
    void openPhysics(PhysicsSettings *physics);
    void openToFCalibration(DetectorSettings *detector);
    void measurementRenamed(Measurement *measurement);
    void closeSampleTabs(Sample *sample);
    void closeMeasurementTab(Measurement *measurement);
    void about();
    void openRecentFile(const QString &fileName);
    void clearRecentFiles();

private:
    void createActions();
    void createMenus(QMenuBar *menuBar); // Also for the tab windows, the actions are shared
    QWidget *createProjectView();
    void connectTabs(DetachableTabWidget *tabs);
    void detachTab(DetachableTabWidget *tabs, int index, const QPoint &globalPos);
    void closeTab(DetachableTabWidget *tabs, int index);
    void closeTab(QWidget *page); // Wherever it is
    void showTab(QWidget *page, const QString &title, const QString &toolTip); // Opened in the main window
    void connectSettingsView(SettingsView *view);
    void recalculateStaleSpectra(); // And depth profiles, after settings they depend on were edited
    void closeToFCalibration(const DetectorSettings *detector); // nullptr closes all
    void updateSpectrumTitle(EnergySpectrumView *view); // nullptr is ignored
    void updateDepthProfileTitle(DepthProfileView *view); // nullptr is ignored
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
    QAction *m_saveAction;
    QAction *m_saveAsAction;
    QAction *m_closeAction;
    QAction *m_closeTabAction;
    QAction *m_preferencesAction;
    QAction *m_importSettingsAction;
    QAction *m_quitAction;
    QAction *m_addSampleAction;
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
