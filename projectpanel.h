#ifndef JARA_PROJECTPANEL_H
#define JARA_PROJECTPANEL_H

#include <QWidget>

class DetectorSettingsEditor;
class ExperimentalSettingsEditor;
class Project;
class QAction;
struct SettingsCollection;
struct AttachedFile;
struct DetectorSettings;
struct ExperimentalSettings;
struct Measurement;
struct MetadataEntry;
struct Sample;
class QComboBox;
class QDropEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QStackedWidget;
class QTableWidget;
class QTableWidgetItem;
class QTreeWidget;
class QTreeWidgetItem;

// Project tree and a properties editor for the selected node. Experimental settings are the first top-level
// items, followed by detector settings and then the samples with their measurements and files.
class ProjectPanel : public QWidget {
    Q_OBJECT

public:
    explicit ProjectPanel(QWidget *parent = nullptr);
    void setProject(Project *project); // Not owned, nullptr clears the panel
    Sample *currentSample() const;
    ExperimentalSettings *currentSettings() const;
    DetectorSettings *currentDetector() const;

public slots:
    void addSample();
    void removeSample();
    void addFiles();
    void addMeasurements();
    void addMeasurementFiles(int sampleIndex, const QStringList &fileNames);
    void addSettings();
    void removeSettings();
    void addDetector();
    void removeDetector();
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
    void sampleAboutToBeRemoved(Sample *sample);
    void measurementAboutToBeRemoved(Measurement *measurement);
    void measurementOpenRequested(Measurement *measurement);
    void measurementRenamed(Measurement *measurement);

private slots:
    void currentItemChanged(QTreeWidgetItem *item);
    void itemDoubleClicked(QTreeWidgetItem *item);
    void showContextMenu(const QPoint &pos);
    void addMetadata();
    void removeMetadata();
    void metadataItemChanged(QTableWidgetItem *item);
    void openCurrentFile();
    void removeCurrentFile();
    void removeCurrentMeasurement();

private:
    QWidget *createPropertyPages();
    QWidget *createSettingsPage();
    QWidget *createDetectorPage();
    QListWidget *createAssignedList();
    QTreeWidgetItem *createSettingsItem(const ExperimentalSettings &settings);
    QTreeWidgetItem *createDetectorItem(const DetectorSettings &detector);
    QTreeWidgetItem *createSampleItem(const Sample &sample);
    int sampleIndexOf(QTreeWidgetItem *item) const; // -1 when the item is not part of a sample
    Sample *sampleOf(QTreeWidgetItem *item) const;
    ExperimentalSettings *settingsOf(QTreeWidgetItem *item) const;
    DetectorSettings *detectorOf(QTreeWidgetItem *item) const;
    int firstSampleRow() const; // Row of the first sample among the top-level items
    AttachedFile &currentFile() const;
    Measurement *currentMeasurement() const;
    void showProperties(QTreeWidgetItem *item);
    void showSettings(ExperimentalSettings &settings);
    void showDetector(DetectorSettings &detector);
    void showAssigned(QListWidget *list, const QList<Measurement *> &measurements);
    void showGeometry(const Measurement &measurement); // Beam and angles from the assigned settings
    void addSettingsCollection(const SettingsCollection &collection);
    SettingsCollection currentSettingsCollection() const;
    static SettingsCollection settingsInClipboard();
    void appendMetadataRow(const MetadataEntry &entry);
    QTreeWidgetItem *measurementItem(const Measurement *measurement) const;
    void sortSamples();
    void rebuildSampleItems(const void *select); // Sorts and rebuilds the sample items, selects the given sample or measurement
    void importProject(Project &source, const QString &fileName, int targetSample);
    void handleMeasurementDrag(QDropEvent *event, int targetSample);
    void moveMeasurement(Measurement *measurement, int targetSample);

    Project *m_project = nullptr;
    QTreeWidget *m_tree;
    QStackedWidget *m_pages;
    QLineEdit *m_sampleNameEdit;
    QPlainTextEdit *m_sampleDescriptionEdit;
    QTableWidget *m_metadataTable;
    QLabel *m_filePathLabel;
    QLineEdit *m_fileDescriptionEdit;
    QLabel *m_measurementPathLabel;
    QLineEdit *m_measurementNameEdit;
    QLineEdit *m_measurementDescriptionEdit;
    QComboBox *m_measurementSettingsCombo;
    QComboBox *m_measurementDetectorCombo;
    QLabel *m_beamIonLabel;
    QLabel *m_beamEnergyLabel;
    QLabel *m_incidentAngleLabel;
    QLabel *m_scatteringAngleLabel;
    QLabel *m_exitAngleLabel;
    ExperimentalSettingsEditor *m_settingsEditor;
    QListWidget *m_assignedList;
    DetectorSettingsEditor *m_detectorEditor;
    QListWidget *m_detectorAssignedList;
    QAction *m_copySettingsAction;
    QAction *m_pasteSettingsAction;
};

#endif // JARA_PROJECTPANEL_H
