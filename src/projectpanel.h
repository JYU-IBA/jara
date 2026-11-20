#ifndef JARA_PROJECTPANEL_H
#define JARA_PROJECTPANEL_H

#include <QWidget>

class Project;
class QAction;
struct SettingsCollection;
struct AttachedFile;
class QMenu;
struct DetectorSettings;
struct PhysicsSettings;
struct DepthProfile;
struct SplitAnalysis;
struct Report;
struct EnergySpectrum;
struct ExperimentalSettings;
struct Measurement;
struct Sample;
class QComboBox;
class QDropEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QTableWidget;
class QTableWidgetItem;
class QTreeWidget;
class QTreeWidgetItem;

// Project tree and a properties editor for the selected node. The first item is the project's information. Experimental, detector and physics settings are edited
// in tabs of the work area, opened with settingsOpenRequested(), detectorOpenRequested() and physicsOpenRequested().
// The tree has experimental settings as the first top-level items, followed by detector settings, physics settings and
// then the samples with their measurements and files, and the reports. Energy spectra, depth profiles and split
// analyses are under their measurements.
class ProjectPanel : public QWidget {
    Q_OBJECT

public:
    explicit ProjectPanel(QWidget *parent = nullptr);
    void setProject(Project *project); // Not owned, nullptr clears the panel
    Sample *currentSample() const;
    ExperimentalSettings *currentSettings() const;
    DetectorSettings *currentDetector() const;
    PhysicsSettings *currentPhysics() const;
    void selectMeasurement(const Measurement *measurement);
    void settingsEdited(); // Updates the shown names and values after settings were edited elsewhere

public slots:
    void updateProjectName(); // In the tree, after it was changed or the project saved (the file name may be used)
    void measurementRenamedElsewhere(Measurement *measurement); // The text of its item
    void sampleRenamedElsewhere(Sample *sample); // Same as above
    void renameFinished(); // Sorts samples and measurements by name, after one was renamed in a tab
    void addSample();
    void removeSample();
    void addFiles();
    void addMeasurements();
    void addMeasurementFiles(int sampleIndex, const QStringList &fileNames);
    void convertCoincidenceFiles(); // Converts *.evnt files to measurements of existing or new samples
    void addSpectrum(); // To the current measurement
    void addDepthProfile(); // To the current measurement
    void addSplit(); // To the current measurement
    void addReport();
    void addSettings();
    void removeSettings();
    void addDetector();
    void removeDetector();
    void addPhysics();
    void removePhysics();
    void copySettings(); // The current experimental or detector settings
    void pasteSettings();
    void exportSettings();
    void importSettings(); // Asks for a settings or project file
    void importFile(const QString &fileName, int targetSample = -1); // Settings or project file

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void modified();
    void currentSampleChanged(Sample *sample);
    void currentSettingsChanged(ExperimentalSettings *settings);
    void currentDetectorChanged(DetectorSettings *detector);
    void currentPhysicsChanged(PhysicsSettings *physics);
    void sampleAboutToBeRemoved(Sample *sample);
    void measurementAboutToBeRemoved(Measurement *measurement);
    void measurementOpenRequested(Measurement *measurement); // Its ToF-E histogram
    void measurementInfoOpenRequested(Measurement *measurement);
    void sampleOpenRequested(Sample *sample);
    // An attached file, see isImageFile(). The sample is nullptr for files of the project.
    void imageOpenRequested(Sample *sample, const QString &path);
    void fileAboutToBeRemoved(Sample *sample, const QString &path);
    void aboutToOpen(bool newTab); // Before one of the open requests: where the item is shown
    void settingsRenamed(); // In the tree, settings tabs show the name
    void projectRenamed(); // Same as above
    void settingsOpenRequested(ExperimentalSettings *settings);
    void detectorOpenRequested(DetectorSettings *detector);
    void physicsOpenRequested(PhysicsSettings *physics);
    void settingsAboutToBeRemoved(ExperimentalSettings *settings);
    void detectorAboutToBeRemoved(DetectorSettings *detector);
    void physicsAboutToBeRemoved(PhysicsSettings *physics);
    void measurementRenamed(Measurement *measurement);
    void spectrumOpenRequested(Measurement *measurement, EnergySpectrum *spectrum);
    void spectrumAboutToBeRemoved(EnergySpectrum *spectrum);
    void spectrumRenamed(EnergySpectrum *spectrum);
    void depthProfileOpenRequested(Measurement *measurement, DepthProfile *profile);
    void depthProfileAboutToBeRemoved(DepthProfile *profile);
    void depthProfileRenamed(DepthProfile *profile);
    void splitOpenRequested(Measurement *measurement, SplitAnalysis *split);
    void splitAboutToBeRemoved(SplitAnalysis *split);
    void splitRenamed(SplitAnalysis *split);
    void reportOpenRequested(Report *report);
    void projectInfoOpenRequested();
    void reportAboutToBeRemoved(Report *report);
    void reportRenamed(Report *report);

private slots:
    void currentItemChanged(QTreeWidgetItem *item);
    void itemRenamed(QTreeWidgetItem *item); // In place
    void openItem(QTreeWidgetItem *item, bool newTab); // In the active tab or a new one
    void showContextMenu(const QPoint &pos);
    void addFileActions(QMenu &menu); // Of the current file, to its context menu
    void openCurrentFile(); // Outside JaRA
    void removeCurrentFile();
    void removeCurrentMeasurement();
    void removeCurrentSpectrum();
    void removeCurrentDepthProfile();
    void removeCurrentSplit();
    void duplicateCurrentAnalysis(); // An energy spectrum, depth profile or split analysis
    void duplicateCurrentSettings(); // Experimental, detector or physics settings
    void removeCurrentReport();

private:
    QTreeWidgetItem *createSettingsItem(const ExperimentalSettings &settings);
    QTreeWidgetItem *createDetectorItem(const DetectorSettings &detector);
    QTreeWidgetItem *createPhysicsItem(const PhysicsSettings &physics);
    QTreeWidgetItem *createSampleItem(const Sample &sample);
    int sampleIndexOf(QTreeWidgetItem *item) const; // -1 when the item is not part of a sample
    Sample *sampleOf(QTreeWidgetItem *item) const;
    ExperimentalSettings *settingsOf(QTreeWidgetItem *item) const;
    DetectorSettings *detectorOf(QTreeWidgetItem *item) const;
    PhysicsSettings *physicsOf(QTreeWidgetItem *item) const;
    int firstSampleRow() const; // Row of the first sample among the top-level items
    AttachedFile &currentFile() const;
    QList<AttachedFile> &filesOf(QTreeWidgetItem *fileItem) const; // Of the project or the sample the file is under
    Measurement *currentMeasurement() const; // Also of the current spectrum, depth profile or split analysis
    EnergySpectrum *currentSpectrum() const;
    DepthProfile *currentDepthProfile() const;
    SplitAnalysis *currentSplit() const;
    Report *currentReport() const;
    int firstReportRow() const; // After the samples
    void addSettingsCollection(const SettingsCollection &collection);
    SettingsCollection currentSettingsCollection() const;
    static SettingsCollection settingsInClipboard();
    QTreeWidgetItem *measurementItem(const Measurement *measurement) const;
    void sortSamples();
    // Adds measurements without updating the tree. A file already measured in the sample is not added again, the
    // checksums of all measurements of the file are updated. Returns the last measurement added or found.
    const Measurement *addMeasurementsTo(Sample *sample, const QStringList &fileNames, DetectorSettings *detector = nullptr);
    void rebuildSampleItems(const void *select); // Sorts and rebuilds the sample items, selects the given sample or measurement
    void importProject(Project &source, const QString &fileName, int targetSample);
    void handleMeasurementDrag(QDropEvent *event, int targetSample);
    void handleDepthProfileDrag(QDropEvent *event, QTreeWidgetItem *target);
    void copyDepthProfile(const DepthProfile *profile, Measurement *target);
    void moveMeasurement(Measurement *measurement, int targetSample);

    Project *m_project = nullptr;
    QTreeWidget *m_tree;
    QAction *m_copySettingsAction;
    QAction *m_pasteSettingsAction;
};

#endif // JARA_PROJECTPANEL_H
