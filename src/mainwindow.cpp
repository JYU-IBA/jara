#include "mainwindow.h"
#include "defaults.h"
#include "depthprofileview.h"
#include "splitview.h"
#include "energyspectrumview.h"
#include "histogramwidget.h"
#include "measurementview.h"
#include "preferencesdialog.h"
#include "projectpanel.h"
#include "settingsview.h"
#include "tabwidget.h"
#include "tofcalibrationwindow.h"
#include "version.h"

#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <gsl/gsl_version.h>
extern "C" {
#include <jibal.h>
}

namespace {
const QString ProjectFileFilter = QStringLiteral("JaRA projects (*.json);;All files (*)");
const QString RecentFilesKey = QStringLiteral("recentFiles");
constexpr int MaxRecentFiles = 10;

QStringList recentFiles() {
    return QSettings().value(RecentFilesKey).toStringList();
}

void setRecentFiles(const QStringList &files) {
    QSettings().setValue(RecentFilesKey, files);
}

// With ~ for the home directory
QString displayPath(const QString &path) {
    const QString home = QDir::homePath();
    return QDir::toNativeSeparators(path.startsWith(home + "/") ? "~" + path.mid(home.size()) : path);
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    resize(1280, 800);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(createStartPage());
    m_stack->addWidget(createProjectView());
    setCentralWidget(m_stack);

    createActions();
    createMenus(menuBar());
    statusBar()->showMessage(QString("JIBAL %1, GSL %2").arg(jibal_version(), GSL_VERSION));
    updateWindowState();
}

QWidget *MainWindow::createStartPage() {
    auto *title = new QLabel("JaRA");
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 2.5);
    titleFont.setBold(true);
    title->setFont(titleFont);
    auto *subtitle = new QLabel(QString("Analysis of ToF-ERDA data, version %1").arg(Version::version()));
    subtitle->setForegroundRole(QPalette::PlaceholderText);
    // Development builds tell where they came from. A detached HEAD (e.g. a checked out tag) is not a branch.
    QStringList buildNotes;
    if(Version::gitPopulated()) {
        if(Version::gitBranch() != "main" && Version::gitBranch() != "HEAD") {
            buildNotes << QString("branch %1").arg(Version::gitBranch());
        }
        if(Version::gitDirty()) {
            buildNotes << "uncommitted changes";
        }
    }
    auto *buildNote = new QLabel(QString("Development build from git: %1").arg(buildNotes.join(", ")));
    buildNote->setForegroundRole(QPalette::PlaceholderText);
    buildNote->setVisible(!buildNotes.isEmpty());

    auto *newButton = new QPushButton("New Project");
    auto *openButton = new QPushButton("Open Project...");
    connect(newButton, &QPushButton::clicked, this, &MainWindow::newProject);
    connect(openButton, &QPushButton::clicked, this, &MainWindow::openProject);
    auto *buttonRow = new QHBoxLayout;
    buttonRow->addWidget(newButton);
    buttonRow->addWidget(openButton);
    buttonRow->addStretch();

    auto *recentTitle = new QLabel("Recent projects");
    QFont recentFont = recentTitle->font();
    recentFont.setBold(true);
    recentTitle->setFont(recentFont);
    m_recentList = new QTreeWidget;
    m_recentList->setHeaderLabels({"Project", "Folder"});
    m_recentList->setRootIsDecorated(false);
    m_recentList->setToolTip("Double-click a project to open it");
    connect(m_recentList, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        if(const QString file = item->data(0, Qt::UserRole).toString(); !file.isEmpty()) {
            openRecentFile(file);
        }
    });

    // A column of limited width in the middle of the window
    auto *column = new QWidget;
    column->setMaximumWidth(720);
    auto *columnLayout = new QVBoxLayout(column);
    columnLayout->addWidget(title);
    columnLayout->addWidget(subtitle);
    columnLayout->addWidget(buildNote);
    columnLayout->addSpacing(20);
    columnLayout->addLayout(buttonRow);
    columnLayout->addSpacing(20);
    columnLayout->addWidget(recentTitle);
    columnLayout->addWidget(m_recentList, 1);

    auto *page = new QWidget;
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(20, 40, 20, 40);
    layout->addStretch();
    layout->addWidget(column, 3);
    layout->addStretch();
    return page;
}

QWidget *MainWindow::createProjectView() {
    m_projectPanel = new ProjectPanel;

    m_tabs = new DetachableTabWidget("Double-click a measurement, energy spectrum or settings in the project tree to open them here.\n"
                                     "Drag a tab out to show it in a window of its own, and back to a tab bar.");
    connectTabs(m_tabs);

    auto *splitter = new QSplitter;
    splitter->addWidget(m_projectPanel);
    splitter->addWidget(m_tabs);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({320, 960});

    connect(m_projectPanel, &ProjectPanel::modified, this, &MainWindow::projectModified);
    connect(m_projectPanel, &ProjectPanel::currentSampleChanged, this, &MainWindow::updateWindowState);
    connect(m_projectPanel, &ProjectPanel::currentSettingsChanged, this, &MainWindow::updateWindowState);
    connect(m_projectPanel, &ProjectPanel::currentDetectorChanged, this, &MainWindow::updateWindowState);
    connect(m_projectPanel, &ProjectPanel::currentPhysicsChanged, this, &MainWindow::updateWindowState);
    connect(m_projectPanel, &ProjectPanel::sampleAboutToBeRemoved, this, &MainWindow::closeSampleTabs);
    connect(m_projectPanel, &ProjectPanel::measurementAboutToBeRemoved, this, &MainWindow::closeMeasurementTab);
    connect(m_projectPanel, &ProjectPanel::measurementOpenRequested, this, &MainWindow::openMeasurement);
    connect(m_projectPanel, &ProjectPanel::measurementRenamed, this, &MainWindow::measurementRenamed);
    connect(m_projectPanel, &ProjectPanel::spectrumOpenRequested, this, &MainWindow::openSpectrum);
    connect(m_projectPanel, &ProjectPanel::spectrumAboutToBeRemoved, this, [this](EnergySpectrum *spectrum) {
        closeTab(findView<EnergySpectrumView>([spectrum](EnergySpectrumView *view) { return view->spectrum() == spectrum; }));
    });
    connect(m_projectPanel, &ProjectPanel::spectrumRenamed, this, [this](EnergySpectrum *spectrum) {
        updateSpectrumTitle(findView<EnergySpectrumView>([spectrum](EnergySpectrumView *view) { return view->spectrum() == spectrum; }));
    });
    connect(m_projectPanel, &ProjectPanel::depthProfileOpenRequested, this, &MainWindow::openDepthProfile);
    connect(m_projectPanel, &ProjectPanel::depthProfileAboutToBeRemoved, this, [this](DepthProfile *profile) {
        closeTab(findView<DepthProfileView>([profile](DepthProfileView *view) { return view->profile() == profile; }));
    });
    connect(m_projectPanel, &ProjectPanel::depthProfileRenamed, this, [this](DepthProfile *profile) {
        updateDepthProfileTitle(findView<DepthProfileView>([profile](DepthProfileView *view) { return view->profile() == profile; }));
    });
    connect(m_projectPanel, &ProjectPanel::splitOpenRequested, this, &MainWindow::openSplit);
    connect(m_projectPanel, &ProjectPanel::splitAboutToBeRemoved, this, [this](SplitAnalysis *split) {
        closeTab(findView<SplitView>([split](SplitView *view) { return view->split() == split; }));
    });
    connect(m_projectPanel, &ProjectPanel::splitRenamed, this, [this](SplitAnalysis *split) {
        updateSplitTitle(findView<SplitView>([split](SplitView *view) { return view->split() == split; }));
    });
    connect(m_projectPanel, &ProjectPanel::settingsOpenRequested, this, &MainWindow::openSettings);
    connect(m_projectPanel, &ProjectPanel::detectorOpenRequested, this, &MainWindow::openDetector);
    connect(m_projectPanel, &ProjectPanel::physicsOpenRequested, this, &MainWindow::openPhysics);
    connect(m_projectPanel, &ProjectPanel::physicsAboutToBeRemoved, this, [this](PhysicsSettings *physics) {
        closeTab(findView<SettingsView>([physics](SettingsView *view) { return view->physics() == physics; }));
    });
    connect(m_projectPanel, &ProjectPanel::settingsAboutToBeRemoved, this, [this](ExperimentalSettings *settings) {
        closeTab(findView<SettingsView>([settings](SettingsView *view) { return view->settings() == settings; }));
    });
    connect(m_projectPanel, &ProjectPanel::detectorAboutToBeRemoved, this, [this](DetectorSettings *detector) {
        closeTab(findView<SettingsView>([detector](SettingsView *view) { return view->detector() == detector; }));
        closeToFCalibration(detector);
    });
    return splitter;
}

void MainWindow::connectTabs(DetachableTabWidget *tabs) {
    connect(tabs, &QTabWidget::tabCloseRequested, this, [this, tabs](int index) { closeTab(tabs, index); });
    connect(tabs, &DetachableTabWidget::detachRequested, this, [this, tabs](int index, const QPoint &globalPos) {
        detachTab(tabs, index, globalPos);
    });
}

void MainWindow::detachTab(DetachableTabWidget *tabs, int index, const QPoint &globalPos) {
    auto *window = new TabWindow(this);
    createMenus(window->menuBar());
    connectTabs(window->tabs());
    window->resize(tabs->widget(index)->size() + QSize(0, tabs->tabBar()->height()));
    window->move(globalPos - QPoint(window->width() / 2, 10));
    DetachableTabWidget::moveTab(tabs, index, window->tabs(), 0);
}

void MainWindow::createActions() {
    m_newAction = new QAction("&New Project", this);
    m_newAction->setShortcut(QKeySequence::New);
    connect(m_newAction, &QAction::triggered, this, &MainWindow::newProject);

    m_openAction = new QAction("&Open Project...", this);
    m_openAction->setShortcut(QKeySequence::Open);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openProject);

    m_saveAction = new QAction("&Save Project", this);
    m_saveAction->setShortcut(QKeySequence::Save);
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::saveProject);

    m_saveAsAction = new QAction("Save Project &As...", this);
    m_saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(m_saveAsAction, &QAction::triggered, this, &MainWindow::saveProjectAs);

    m_exportArchiveAction = new QAction("&Export Archive...", this);
    m_exportArchiveAction->setToolTip("The project with copies of its measurements and files, to continue elsewhere");
    connect(m_exportArchiveAction, &QAction::triggered, this, &MainWindow::exportArchive);

    m_closeAction = new QAction("&Close Project", this);
    m_closeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W));
    connect(m_closeAction, &QAction::triggered, this, &MainWindow::closeProject);

    // The current tab of the active window, the main window or a tab window
    m_closeTabAction = new QAction("Close &Tab", this);
    m_closeTabAction->setShortcut(QKeySequence::Close);
    connect(m_closeTabAction, &QAction::triggered, this, [this]() {
        QWidget *window = QApplication::activeWindow();
        auto *tabWindow = qobject_cast<TabWindow *>(window);
        DetachableTabWidget *tabs = tabWindow ? tabWindow->tabs() : window == this ? m_tabs : nullptr;
        if(tabs && tabs->currentIndex() >= 0) {
            closeTab(tabs, tabs->currentIndex());
        }
    });

    m_preferencesAction = new QAction("&Preferences...", this);
    m_preferencesAction->setShortcut(QKeySequence::Preferences);
    m_preferencesAction->setMenuRole(QAction::PreferencesRole); // In the application menu on macOS
    connect(m_preferencesAction, &QAction::triggered, this, [this]() {
        PreferencesDialog dialog(this);
        if(dialog.exec() == QDialog::Accepted) {
            const HistogramPreferences preferences = Defaults::histogramPreferences();
            for(HistogramWidget *histogram : findChildren<HistogramWidget *>()) { // Also in the tab windows
                histogram->applyPreferences(preferences);
            }
            for(DepthProfileView *view : findChildren<DepthProfileView *>()) {
                view->applyPreferences();
            }
        }
    });

    m_importSettingsAction = new QAction("&Import...", this);
    connect(m_importSettingsAction, &QAction::triggered, m_projectPanel, &ProjectPanel::importSettings);

    m_quitAction = new QAction("&Quit", this);
    m_quitAction->setShortcut(QKeySequence::Quit);
    connect(m_quitAction, &QAction::triggered, this, &QWidget::close);

    m_addSampleAction = new QAction("&Add Sample", this);
    connect(m_addSampleAction, &QAction::triggered, m_projectPanel, &ProjectPanel::addSample);

    m_removeSampleAction = new QAction("&Remove Sample", this);
    connect(m_removeSampleAction, &QAction::triggered, m_projectPanel, &ProjectPanel::removeSample);

    m_addFilesAction = new QAction("Add &Files...", this);
    connect(m_addFilesAction, &QAction::triggered, m_projectPanel, &ProjectPanel::addFiles);

    m_addMeasurementsAction = new QAction("Add &Measurements...", this);
    connect(m_addMeasurementsAction, &QAction::triggered, m_projectPanel, &ProjectPanel::addMeasurements);

    m_addSettingsAction = new QAction("&Add Experimental Settings", this);
    connect(m_addSettingsAction, &QAction::triggered, m_projectPanel, &ProjectPanel::addSettings);

    m_removeSettingsAction = new QAction("&Remove Experimental Settings", this);
    connect(m_removeSettingsAction, &QAction::triggered, m_projectPanel, &ProjectPanel::removeSettings);

    m_addDetectorAction = new QAction("Add &Detector Settings", this);
    connect(m_addDetectorAction, &QAction::triggered, m_projectPanel, &ProjectPanel::addDetector);

    m_removeDetectorAction = new QAction("Remove D&etector Settings", this);
    connect(m_removeDetectorAction, &QAction::triggered, m_projectPanel, &ProjectPanel::removeDetector);
    m_addPhysicsAction = new QAction("Add &Physics Settings", this);
    connect(m_addPhysicsAction, &QAction::triggered, m_projectPanel, &ProjectPanel::addPhysics);
    m_removePhysicsAction = new QAction("Remove P&hysics Settings", this);
    connect(m_removePhysicsAction, &QAction::triggered, m_projectPanel, &ProjectPanel::removePhysics);
}

void MainWindow::createMenus(QMenuBar *menuBar) {
    QMenu *fileMenu = menuBar->addMenu("&File");
    fileMenu->addAction(m_newAction);
    fileMenu->addAction(m_openAction);
    QMenu *recentFilesMenu = fileMenu->addMenu("Open &Recent");
    m_recentFilesMenus.append(recentFilesMenu);
    connect(recentFilesMenu, &QObject::destroyed, this, [this, recentFilesMenu]() { m_recentFilesMenus.removeOne(recentFilesMenu); });
    updateRecentFilesMenu();
    fileMenu->addAction(m_saveAction);
    fileMenu->addAction(m_saveAsAction);
    fileMenu->addAction(m_exportArchiveAction);
    fileMenu->addAction(m_closeTabAction);
    fileMenu->addAction(m_closeAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_preferencesAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_quitAction);

    QMenu *sampleMenu = menuBar->addMenu("&Sample");
    sampleMenu->addAction(m_addSampleAction);
    sampleMenu->addAction(m_removeSampleAction);
    sampleMenu->addSeparator();
    sampleMenu->addAction(m_addMeasurementsAction);
    sampleMenu->addAction(m_addFilesAction);

    QMenu *experimentMenu = menuBar->addMenu("&Experiment");
    experimentMenu->addAction(m_addSettingsAction);
    experimentMenu->addAction(m_removeSettingsAction);
    experimentMenu->addSeparator();
    experimentMenu->addAction(m_addDetectorAction);
    experimentMenu->addAction(m_removeDetectorAction);
    experimentMenu->addAction(m_addPhysicsAction);
    experimentMenu->addAction(m_removePhysicsAction);
    experimentMenu->addSeparator();
    experimentMenu->addAction(m_importSettingsAction);

    QMenu *helpMenu = menuBar->addMenu("&Help");
    helpMenu->addAction("&About JaRA", this, &MainWindow::about);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if(maybeSave()) {
        // Otherwise the tab windows would keep the application running
        for(TabWindow *window : findChildren<TabWindow *>(Qt::FindDirectChildrenOnly)) {
            window->close();
        }
        event->accept();
    } else {
        event->ignore();
    }
}

bool MainWindow::maybeSave() {
    if(!m_project || !m_project->isModified()) {
        return true;
    }
    const auto answer = QMessageBox::warning(this, "JaRA", "The project has unsaved changes. Do you want to save them?",
                                             QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                             QMessageBox::Save);
    if(answer == QMessageBox::Save) {
        return saveProject();
    }
    return answer == QMessageBox::Discard;
}

void MainWindow::setProject(std::unique_ptr<Project> project) {
    // Views refer to the old project, so they are closed before it is destroyed. Emptied tab windows close.
    for(QWidget *page : tabPages()) {
        closeTab(page);
    }
    closeToFCalibration(nullptr);
    m_projectPanel->setProject(project.get());
    m_project = std::move(project);
    m_stack->setCurrentIndex(m_project ? 1 : 0);
    updateWindowState();
}

void MainWindow::updateWindowState() {
    const bool open = m_project != nullptr;
    const bool hasSample = open && m_projectPanel->currentSample();
    m_saveAction->setEnabled(open);
    m_saveAsAction->setEnabled(open);
    m_exportArchiveAction->setEnabled(open);
    m_closeAction->setEnabled(open);
    m_addSampleAction->setEnabled(open);
    m_removeSampleAction->setEnabled(hasSample);
    m_addFilesAction->setEnabled(hasSample);
    m_addMeasurementsAction->setEnabled(hasSample);
    m_addSettingsAction->setEnabled(open);
    m_removeSettingsAction->setEnabled(open && m_projectPanel->currentSettings());
    m_addDetectorAction->setEnabled(open);
    m_removeDetectorAction->setEnabled(open && m_projectPanel->currentDetector());
    m_addPhysicsAction->setEnabled(open);
    m_removePhysicsAction->setEnabled(open && m_projectPanel->currentPhysics());
    m_importSettingsAction->setEnabled(open);
    if(!open) {
        setWindowTitle("JaRA");
        setWindowModified(false);
        return;
    }
    const QString name = m_project->fileName().isEmpty() ? QStringLiteral("Untitled")
                                                         : QFileInfo(m_project->fileName()).fileName();
    setWindowTitle(QString("%1[*] - JaRA").arg(name));
    setWindowModified(m_project->isModified());
}

void MainWindow::newProject() {
    if(!maybeSave()) {
        return;
    }
    auto project = std::make_unique<Project>();
    project->addSettings(Defaults::experimentalSettings());
    project->addDetector(Defaults::detectorSettings());
    project->addPhysics(Defaults::physicsSettings());
    project->setModified(false); // Nothing to save until something is changed
    setProject(std::move(project));
}

void MainWindow::openProject() {
    if(!maybeSave()) {
        return;
    }
    const QString fileName = QFileDialog::getOpenFileName(this, "Open Project", {}, ProjectFileFilter);
    if(fileName.isEmpty()) {
        return;
    }
    openProjectFile(fileName);
}

void MainWindow::openProjectFile(const QString &fileName) {
    QString errorMessage;
    auto project = Project::load(fileName, errorMessage);
    if(!project) {
        QMessageBox::critical(this, "JaRA", QString("Could not open project %1:\n%2").arg(fileName, errorMessage));
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QList<Project::FileProblem> problems = project->verifyFiles();
    QApplication::restoreOverrideCursor();
    setProject(std::move(project));
    addRecentFile(fileName);
    statusBar()->showMessage(QString("Opened %1").arg(fileName), 5000);
    if(!problems.isEmpty()) {
        showFileProblems(problems);
    }
}

void MainWindow::showFileProblems(const QList<Project::FileProblem> &problems) {
    QStringList changed;
    QStringList missing;
    for(const Project::FileProblem &problem : problems) {
        (problem.missing ? missing : changed).append(QDir::toNativeSeparators(problem.path));
    }
    QString text;
    if(!changed.isEmpty()) {
        text += QString("These files have changed since they were added to the project (the SHA-256 checksum is "
                        "different):\n\n%1").arg(changed.join('\n'));
    }
    if(!missing.isEmpty()) {
        text += QString("%1These files cannot be read:\n\n%2").arg(text.isEmpty() ? QString() : QStringLiteral("\n\n"),
                                                                   missing.join('\n'));
    }
    QMessageBox box(QMessageBox::Warning, "JaRA", text, QMessageBox::NoButton, this);
    if(changed.isEmpty()) {
        box.addButton(QMessageBox::Ok);
        box.exec();
        return;
    }
    box.setInformativeText("Accept the changed files as they are now? Otherwise the project keeps the old checksums "
                           "and the files are reported again the next time the project is opened.");
    QPushButton *accept = box.addButton("Accept Changes", QMessageBox::AcceptRole);
    box.addButton("Keep Old Checksums", QMessageBox::RejectRole);
    box.setDefaultButton(qobject_cast<QPushButton *>(box.buttons().last()));
    box.exec();
    if(box.clickedButton() == accept) {
        m_project->acceptChangedFiles(problems); // Saved with the project
        updateWindowState();
    }
}

bool MainWindow::saveProject() {
    if(!m_project) {
        return false;
    }
    if(m_project->fileName().isEmpty()) {
        return saveProjectAs();
    }
    QString errorMessage;
    if(!m_project->save(m_project->fileName(), errorMessage)) {
        QMessageBox::critical(this, "JaRA", QString("Could not save project:\n%1").arg(errorMessage));
        return false;
    }
    updateWindowState();
    statusBar()->showMessage(QString("Saved %1").arg(m_project->fileName()), 5000);
    return true;
}

bool MainWindow::saveProjectAs() {
    if(!m_project) {
        return false;
    }
    QString fileName = QFileDialog::getSaveFileName(this, "Save Project As", m_project->fileName(), ProjectFileFilter);
    if(fileName.isEmpty()) {
        return false;
    }
    if(QFileInfo(fileName).suffix().isEmpty()) {
        fileName += ".json";
    }
    QString errorMessage;
    if(!m_project->save(fileName, errorMessage)) {
        QMessageBox::critical(this, "JaRA", QString("Could not save project:\n%1").arg(errorMessage));
        return false;
    }
    updateWindowState();
    addRecentFile(fileName);
    statusBar()->showMessage(QString("Saved %1").arg(fileName), 5000);
    return true;
}

void MainWindow::exportArchive() {
    if(!m_project) {
        return;
    }
    QStringList missing;
    for(const QString &path : m_project->referencedFiles()) {
        if(!QFileInfo(path).isReadable()) {
            missing.append(QDir::toNativeSeparators(path));
        }
    }
    if(!missing.isEmpty()
       && QMessageBox::warning(this, "Export Archive",
                               QString("These files cannot be read and will be left out of the archive:\n\n%1\n\n"
                                       "Export the archive anyway?").arg(missing.join('\n')),
                               QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }
    const QFileInfo projectFile(m_project->fileName());
    const QString suggested = m_project->fileName().isEmpty()
                                      ? QStringLiteral("project.tar.gz")
                                      : projectFile.absoluteDir().filePath(projectFile.completeBaseName() + ".tar.gz");
    const QString zipFilter = QStringLiteral("Zip archives (*.zip)");
    QString filter;
    QString fileName = QFileDialog::getSaveFileName(this, "Export Archive", suggested,
                                                    "Compressed tar archives (*.tar.gz);;" + zipFilter, &filter);
    if(fileName.isEmpty()) {
        return;
    }
    if(!fileName.endsWith(".zip", Qt::CaseInsensitive) && !fileName.endsWith(".tar.gz", Qt::CaseInsensitive)
       && !fileName.endsWith(".tgz", Qt::CaseInsensitive)) {
        fileName += filter == zipFilter ? ".zip" : ".tar.gz";
    }
    QString errorMessage;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool exported = m_project->exportArchive(fileName, errorMessage);
    QApplication::restoreOverrideCursor();
    if(!exported) {
        QMessageBox::critical(this, "Export Archive", QString("Could not export the archive:\n%1").arg(errorMessage));
        return;
    }
    statusBar()->showMessage(QString("Exported %1").arg(fileName), 5000);
}

void MainWindow::closeProject() {
    if(maybeSave()) {
        setProject(nullptr);
    }
}

void MainWindow::projectModified() {
    m_project->setModified(true);
    updateWindowState();
    // Measurements may have been assigned to other settings, renamed, added or removed. Selections or the ToF
    // calibration of spectra may have changed.
    for(QWidget *page : tabPages()) {
        if(auto *view = qobject_cast<SettingsView *>(page)) {
            view->updateAssigned();
        } else if(auto *spectrumView = qobject_cast<EnergySpectrumView *>(page)) {
            spectrumView->checkStale();
        } else if(auto *profileView = qobject_cast<DepthProfileView *>(page)) {
            profileView->checkStale();
        } else if(auto *splitView = qobject_cast<SplitView *>(page)) {
            splitView->refresh();
        }
    }
    for(ToFCalibrationWindow *window : findChildren<ToFCalibrationWindow *>()) {
        window->refresh();
    }
}

QList<QWidget *> MainWindow::tabPages() const {
    QList<QWidget *> pages;
    for(const DetachableTabWidget *tabs : findChildren<DetachableTabWidget *>()) {
        for(int i = 0; i < tabs->count(); i++) {
            pages.append(tabs->widget(i));
        }
    }
    return pages;
}

std::pair<DetachableTabWidget *, int> MainWindow::findTab(const QWidget *page) const {
    for(DetachableTabWidget *tabs : findChildren<DetachableTabWidget *>()) {
        const int index = tabs->indexOf(page);
        if(index >= 0) {
            return {tabs, index};
        }
    }
    return {nullptr, -1};
}

template<typename View, typename Predicate> View *MainWindow::findView(Predicate predicate) const {
    for(QWidget *page : tabPages()) {
        auto *view = qobject_cast<View *>(page);
        if(view && predicate(view)) {
            return view;
        }
    }
    return nullptr;
}

void MainWindow::showTab(QWidget *page, const QString &title, const QString &toolTip) {
    auto [tabs, index] = findTab(page);
    if(!tabs) {
        tabs = m_tabs;
        index = m_tabs->addTab(page, title);
        m_tabs->setTabToolTip(index, toolTip);
    }
    tabs->activateTab(index);
}

void MainWindow::openMeasurement(Measurement *measurement) {
    auto *view = findView<MeasurementView>([measurement](MeasurementView *view) { return view->measurement() == measurement; });
    if(!view) {
        view = new MeasurementView(measurement);
        connect(view, &MeasurementView::modified, this, &MainWindow::projectModified);
    }
    showTab(view, measurement->name, measurement->path);
}

void MainWindow::openSpectrum(Measurement *measurement, EnergySpectrum *spectrum) {
    auto *view = findView<EnergySpectrumView>([spectrum](EnergySpectrumView *view) { return view->spectrum() == spectrum; });
    if(!view) {
        view = new EnergySpectrumView(measurement, spectrum);
        connect(view, &EnergySpectrumView::modified, this, &MainWindow::projectModified);
        connect(view, &EnergySpectrumView::titleChanged, this, [this, view]() { updateSpectrumTitle(view); });
    }
    showTab(view, view->title(), measurement->path);
}

void MainWindow::openDepthProfile(Measurement *measurement, DepthProfile *profile) {
    auto *view = findView<DepthProfileView>([profile](DepthProfileView *view) { return view->profile() == profile; });
    if(!view) {
        view = new DepthProfileView(measurement, profile);
        connect(view, &DepthProfileView::modified, this, &MainWindow::projectModified);
        connect(view, &DepthProfileView::titleChanged, this, [this, view]() { updateDepthProfileTitle(view); });
    }
    showTab(view, view->title(), measurement->path);
}

void MainWindow::openSplit(Measurement *measurement, SplitAnalysis *split) {
    auto *view = findView<SplitView>([split](SplitView *view) { return view->split() == split; });
    if(!view) {
        view = new SplitView(measurement, split);
        connect(view, &SplitView::modified, this, &MainWindow::projectModified);
    }
    showTab(view, view->title(), measurement->path);
}

void MainWindow::updateSplitTitle(SplitView *view) {
    if(const auto [tabs, index] = findTab(view); tabs) {
        tabs->setTabTitle(index, view->title());
    }
}

void MainWindow::updateDepthProfileTitle(DepthProfileView *view) {
    if(const auto [tabs, index] = findTab(view); tabs) {
        tabs->setTabTitle(index, view->title());
    }
}

void MainWindow::updateSpectrumTitle(EnergySpectrumView *view) {
    if(const auto [tabs, index] = findTab(view); tabs) {
        tabs->setTabTitle(index, view->title());
    }
}

void MainWindow::openSettings(ExperimentalSettings *settings) {
    auto *view = findView<SettingsView>([settings](SettingsView *view) { return view->settings() == settings; });
    if(!view) {
        view = new SettingsView(m_project.get(), settings);
        connectSettingsView(view);
    }
    showTab(view, view->name(), "Experimental settings");
}

void MainWindow::openDetector(DetectorSettings *detector) {
    auto *view = findView<SettingsView>([detector](SettingsView *view) { return view->detector() == detector; });
    if(!view) {
        view = new SettingsView(m_project.get(), detector);
        connectSettingsView(view);
    }
    showTab(view, view->name(), "Detector settings");
}

void MainWindow::connectSettingsView(SettingsView *view) {
    connect(view, &SettingsView::modified, this, [this]() {
        m_projectPanel->settingsEdited();
        projectModified();
    });
    connect(view, &SettingsView::nameChanged, this, [this, view]() {
        if(const auto [tabs, index] = findTab(view); tabs) {
            tabs->setTabTitle(index, view->name());
        }
        m_projectPanel->settingsEdited();
    });
    connect(view, &SettingsView::measurementActivated, this, [this](Measurement *measurement) {
        m_projectPanel->selectMeasurement(measurement);
        openMeasurement(measurement);
    });
    // Spectra and depth profiles follow the ToF calibration, the foil or the physics settings once an edit is finished
    connect(view, &SettingsView::editingFinished, this, &MainWindow::recalculateStaleSpectra);
    connect(view, &SettingsView::calibrationRequested, this, [this, view]() { openToFCalibration(view->detector()); });
}

void MainWindow::openPhysics(PhysicsSettings *physics) {
    auto *view = findView<SettingsView>([physics](SettingsView *view) { return view->physics() == physics; });
    if(!view) {
        view = new SettingsView(m_project.get(), physics);
        connectSettingsView(view);
    }
    showTab(view, view->name(), "Physics settings");
}

void MainWindow::recalculateStaleSpectra() {
    for(QWidget *page : tabPages()) {
        auto *spectrumView = qobject_cast<EnergySpectrumView *>(page);
        if(spectrumView && spectrumView->isStale()) {
            spectrumView->recalculate();
        }
        auto *profileView = qobject_cast<DepthProfileView *>(page);
        if(profileView && profileView->isStale()) {
            profileView->recalculate();
        }
    }
}

void MainWindow::openToFCalibration(DetectorSettings *detector) {
    ToFCalibrationWindow *window = nullptr;
    for(ToFCalibrationWindow *existing : findChildren<ToFCalibrationWindow *>()) {
        if(existing->detector() == detector) {
            window = existing;
        }
    }
    if(!window) {
        window = new ToFCalibrationWindow(m_project.get(), detector, this);
        window->setAttribute(Qt::WA_DeleteOnClose);
        connect(window, &ToFCalibrationWindow::modified, this, &MainWindow::projectModified);
        connect(window, &ToFCalibrationWindow::calibrationApplied, this, [this, detector]() {
            if(auto *view = findView<SettingsView>([detector](SettingsView *view) { return view->detector() == detector; })) {
                view->refresh();
            }
            m_projectPanel->settingsEdited();
            projectModified();
            recalculateStaleSpectra();
        });
    }
    window->show();
    window->raise();
    window->activateWindow();
}

void MainWindow::closeToFCalibration(const DetectorSettings *detector) {
    // Deleted right away, the windows refer to the project and the detector
    for(ToFCalibrationWindow *window : findChildren<ToFCalibrationWindow *>()) {
        if(!detector || window->detector() == detector) {
            delete window;
        }
    }
}

void MainWindow::measurementRenamed(Measurement *measurement) {
    auto *view = findView<MeasurementView>([measurement](MeasurementView *view) { return view->measurement() == measurement; });
    if(const auto [tabs, index] = findTab(view); tabs) {
        tabs->setTabTitle(index, measurement->name);
    }
    for(const auto &spectrum : measurement->spectra) {
        updateSpectrumTitle(findView<EnergySpectrumView>([&spectrum](EnergySpectrumView *view) { return view->spectrum() == spectrum.get(); }));
    }
    for(const auto &profile : measurement->depthProfiles) {
        updateDepthProfileTitle(findView<DepthProfileView>([&profile](DepthProfileView *view) { return view->profile() == profile.get(); }));
    }
    for(const auto &split : measurement->splitAnalyses) {
        updateSplitTitle(findView<SplitView>([&split](SplitView *view) { return view->split() == split.get(); }));
    }
}

void MainWindow::closeSampleTabs(Sample *sample) {
    for(const auto &measurement : sample->measurements) {
        closeMeasurementTab(measurement.get());
    }
}

void MainWindow::closeMeasurementTab(Measurement *measurement) {
    closeTab(findView<MeasurementView>([measurement](MeasurementView *view) { return view->measurement() == measurement; }));
    for(const auto &spectrum : measurement->spectra) {
        closeTab(findView<EnergySpectrumView>([&spectrum](EnergySpectrumView *view) { return view->spectrum() == spectrum.get(); }));
    }
    for(const auto &profile : measurement->depthProfiles) {
        closeTab(findView<DepthProfileView>([&profile](DepthProfileView *view) { return view->profile() == profile.get(); }));
    }
    for(const auto &split : measurement->splitAnalyses) {
        closeTab(findView<SplitView>([&split](SplitView *view) { return view->split() == split.get(); }));
    }
}

void MainWindow::closeTab(DetachableTabWidget *tabs, int index) {
    QWidget *page = tabs->widget(index);
    tabs->removeTab(index);
    delete page;
}

void MainWindow::closeTab(QWidget *page) {
    if(const auto [tabs, index] = findTab(page); tabs) {
        closeTab(tabs, index);
    }
}

void MainWindow::addRecentFile(const QString &fileName) {
    const QString path = QFileInfo(fileName).absoluteFilePath();
    QStringList files = recentFiles();
    files.removeAll(path);
    files.prepend(path);
    while(files.size() > MaxRecentFiles) {
        files.removeLast();
    }
    setRecentFiles(files);
    updateRecentFilesMenu();
}

void MainWindow::removeRecentFile(const QString &fileName) {
    QStringList files = recentFiles();
    files.removeAll(fileName);
    setRecentFiles(files);
    updateRecentFilesMenu();
}

void MainWindow::clearRecentFiles() {
    setRecentFiles({});
    updateRecentFilesMenu();
}

void MainWindow::updateRecentFilesMenu() {
    const QStringList files = recentFiles();
    for(QMenu *menu : m_recentFilesMenus) {
        menu->clear();
        for(int i = 0; i < files.size(); i++) {
            const QString &file = files.at(i);
            QString shown = displayPath(file);
            shown.replace("&", "&&"); // Not a mnemonic
            QAction *action = menu->addAction(QString("&%1 %2").arg(i + 1).arg(shown));
            connect(action, &QAction::triggered, this, [this, file]() { openRecentFile(file); });
        }
        if(files.isEmpty()) {
            menu->addAction("No Recent Projects")->setEnabled(false);
        } else {
            menu->addSeparator();
            menu->addAction("Clear Recent Projects", this, &MainWindow::clearRecentFiles);
        }
    }

    m_recentList->clear();
    for(const QString &file : files) {
        const QFileInfo info(file);
        auto *item = new QTreeWidgetItem(m_recentList, {info.completeBaseName(), displayPath(info.absolutePath())});
        item->setData(0, Qt::UserRole, file);
        item->setToolTip(0, displayPath(file));
        item->setToolTip(1, displayPath(file));
    }
    if(files.isEmpty()) {
        auto *item = new QTreeWidgetItem(m_recentList, {"No recent projects"});
        item->setFlags(Qt::NoItemFlags);
    }
    m_recentList->resizeColumnToContents(0);
}

void MainWindow::openRecentFile(const QString &fileName) {
    if(!maybeSave()) {
        return;
    }
    if(!QFileInfo::exists(fileName)) {
        QMessageBox::warning(this, "JaRA", QString("%1 no longer exists. It was removed from the recent projects.").arg(fileName));
        removeRecentFile(fileName);
        return;
    }
    openProjectFile(fileName);
}

void MainWindow::about() {
    QString gitInfo;
    if(Version::gitPopulated()) {
        gitInfo = QString("<p>Built from git commit %1 dated %2 on branch %3%4.</p>")
                          .arg(Version::gitCommitSha1(), Version::gitCommitDate(), Version::gitBranch(),
                               Version::gitDirty() ? " with uncommitted changes" : "");
    }
    QMessageBox::about(this, "About JaRA",
                       QString("<b>JaRA %1</b><p>Analysis of ToF-ERDA data.</p>"
                               "%2<p>JIBAL %3<br>GSL %4<br>Qt %5</p>")
                               .arg(Version::version(), gitInfo, jibal_version(), GSL_VERSION, qVersion()));
}
