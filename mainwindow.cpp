#include "mainwindow.h"
#include "defaults.h"
#include "measurementview.h"
#include "preferencesdialog.h"
#include "projectpanel.h"

#include <QCloseEvent>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>

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

QLabel *centeredLabel(const QString &text) {
    auto *label = new QLabel(text);
    label->setAlignment(Qt::AlignCenter);
    return label;
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    resize(1280, 800);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(centeredLabel("No project open.\nCreate a new project or open an existing one from the File menu."));
    m_stack->addWidget(createProjectView());
    setCentralWidget(m_stack);

    createActions();
    createMenus();
    statusBar()->showMessage(QString("JIBAL %1, GSL %2").arg(jibal_version(), GSL_VERSION));
    updateWindowState();
}

QWidget *MainWindow::createProjectView() {
    m_projectPanel = new ProjectPanel;

    m_tabs = new QTabWidget;
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    m_workArea = new QStackedWidget;
    m_workArea->addWidget(centeredLabel("Double-click a measurement in the project tree to open it here."));
    m_workArea->addWidget(m_tabs);

    auto *splitter = new QSplitter;
    splitter->addWidget(m_projectPanel);
    splitter->addWidget(m_workArea);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({320, 960});

    connect(m_projectPanel, &ProjectPanel::modified, this, &MainWindow::projectModified);
    connect(m_projectPanel, &ProjectPanel::currentSampleChanged, this, &MainWindow::updateWindowState);
    connect(m_projectPanel, &ProjectPanel::currentSettingsChanged, this, &MainWindow::updateWindowState);
    connect(m_projectPanel, &ProjectPanel::currentDetectorChanged, this, &MainWindow::updateWindowState);
    connect(m_projectPanel, &ProjectPanel::sampleAboutToBeRemoved, this, &MainWindow::closeSampleTabs);
    connect(m_projectPanel, &ProjectPanel::measurementAboutToBeRemoved, this, &MainWindow::closeMeasurementTab);
    connect(m_projectPanel, &ProjectPanel::measurementOpenRequested, this, &MainWindow::openMeasurement);
    connect(m_projectPanel, &ProjectPanel::measurementRenamed, this, &MainWindow::measurementRenamed);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    return splitter;
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

    m_closeAction = new QAction("&Close Project", this);
    m_closeAction->setShortcut(QKeySequence::Close);
    connect(m_closeAction, &QAction::triggered, this, &MainWindow::closeProject);

    m_preferencesAction = new QAction("&Preferences...", this);
    m_preferencesAction->setShortcut(QKeySequence::Preferences);
    m_preferencesAction->setMenuRole(QAction::PreferencesRole); // In the application menu on macOS
    connect(m_preferencesAction, &QAction::triggered, this, [this]() {
        PreferencesDialog dialog(this);
        dialog.exec();
    });

    m_importSettingsAction = new QAction("&Import Settings...", this);
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
}

void MainWindow::createMenus() {
    QMenu *fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction(m_newAction);
    fileMenu->addAction(m_openAction);
    m_recentFilesMenu = fileMenu->addMenu("Open &Recent");
    updateRecentFilesMenu();
    fileMenu->addAction(m_saveAction);
    fileMenu->addAction(m_saveAsAction);
    fileMenu->addAction(m_closeAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_preferencesAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_quitAction);

    QMenu *sampleMenu = menuBar()->addMenu("&Sample");
    sampleMenu->addAction(m_addSampleAction);
    sampleMenu->addAction(m_removeSampleAction);
    sampleMenu->addSeparator();
    sampleMenu->addAction(m_addMeasurementsAction);
    sampleMenu->addAction(m_addFilesAction);

    QMenu *experimentMenu = menuBar()->addMenu("&Experiment");
    experimentMenu->addAction(m_addSettingsAction);
    experimentMenu->addAction(m_removeSettingsAction);
    experimentMenu->addSeparator();
    experimentMenu->addAction(m_addDetectorAction);
    experimentMenu->addAction(m_removeDetectorAction);
    experimentMenu->addSeparator();
    experimentMenu->addAction(m_importSettingsAction);

    QMenu *helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("&About JaRA", this, &MainWindow::about);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if(maybeSave()) {
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
    // Views refer to the old project, so they are closed before it is destroyed
    while(m_tabs->count() > 0) {
        closeTab(0);
    }
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
    m_closeAction->setEnabled(open);
    m_addSampleAction->setEnabled(open);
    m_removeSampleAction->setEnabled(hasSample);
    m_addFilesAction->setEnabled(hasSample);
    m_addMeasurementsAction->setEnabled(hasSample);
    m_addSettingsAction->setEnabled(open);
    m_removeSettingsAction->setEnabled(open && m_projectPanel->currentSettings());
    m_addDetectorAction->setEnabled(open);
    m_removeDetectorAction->setEnabled(open && m_projectPanel->currentDetector());
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
    setProject(std::move(project));
    addRecentFile(fileName);
    statusBar()->showMessage(QString("Opened %1").arg(fileName), 5000);
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

void MainWindow::closeProject() {
    if(maybeSave()) {
        setProject(nullptr);
    }
}

void MainWindow::projectModified() {
    m_project->setModified(true);
    updateWindowState();
}

int MainWindow::measurementTabIndex(const Measurement *measurement) const {
    for(int i = 0; i < m_tabs->count(); i++) {
        const auto *view = qobject_cast<MeasurementView *>(m_tabs->widget(i));
        if(view && view->measurement() == measurement) {
            return i;
        }
    }
    return -1;
}

void MainWindow::openMeasurement(Measurement *measurement) {
    int index = measurementTabIndex(measurement);
    if(index < 0) {
        auto *view = new MeasurementView(measurement);
        connect(view, &MeasurementView::modified, this, &MainWindow::projectModified);
        index = m_tabs->addTab(view, measurement->name);
    }
    m_tabs->setCurrentIndex(index);
    m_workArea->setCurrentWidget(m_tabs);
}

void MainWindow::measurementRenamed(Measurement *measurement) {
    const int index = measurementTabIndex(measurement);
    if(index >= 0) {
        m_tabs->setTabText(index, measurement->name);
    }
}

void MainWindow::closeSampleTabs(Sample *sample) {
    for(const auto &measurement : sample->measurements) {
        closeMeasurementTab(measurement.get());
    }
}

void MainWindow::closeMeasurementTab(Measurement *measurement) {
    const int index = measurementTabIndex(measurement);
    if(index >= 0) {
        closeTab(index);
    }
}

void MainWindow::closeTab(int index) {
    QWidget *widget = m_tabs->widget(index);
    m_tabs->removeTab(index);
    delete widget;
    if(m_tabs->count() == 0) {
        m_workArea->setCurrentIndex(0);
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
    m_recentFilesMenu->clear();
    const QStringList files = recentFiles();
    const QString home = QDir::homePath();
    for(int i = 0; i < files.size(); i++) {
        const QString &file = files.at(i);
        QString shown = QDir::toNativeSeparators(file.startsWith(home + "/") ? "~" + file.mid(home.size()) : file);
        shown.replace("&", "&&"); // Not a mnemonic
        QAction *action = m_recentFilesMenu->addAction(QString("&%1 %2").arg(i + 1).arg(shown));
        connect(action, &QAction::triggered, this, [this, file]() { openRecentFile(file); });
    }
    if(files.isEmpty()) {
        m_recentFilesMenu->addAction("No Recent Projects")->setEnabled(false);
    } else {
        m_recentFilesMenu->addSeparator();
        m_recentFilesMenu->addAction("Clear Recent Projects", this, &MainWindow::clearRecentFiles);
    }
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
    QMessageBox::about(this, "About JaRA",
                       QString("<b>JaRA %1</b><p>Analysis of ToF-ERDA data.</p>"
                               "<p>JIBAL %2<br>GSL %3<br>Qt %4</p>")
                               .arg(JARA_VERSION, jibal_version(), GSL_VERSION, qVersion()));
}
