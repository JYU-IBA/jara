#ifndef JARA_PROJECTPANEL_H
#define JARA_PROJECTPANEL_H

#include <QWidget>

class Project;
struct AttachedFile;
struct Measurement;
struct MetadataEntry;
struct Sample;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QStackedWidget;
class QTableWidget;
class QTableWidgetItem;
class QTreeWidget;
class QTreeWidgetItem;

// Project tree (samples with their metadata, files and measurements) and a properties editor for the selected node
class ProjectPanel : public QWidget {
    Q_OBJECT

public:
    explicit ProjectPanel(QWidget *parent = nullptr);
    void setProject(Project *project); // Not owned, nullptr clears the panel
    Sample *currentSample() const;

public slots:
    void addSample();
    void removeSample();
    void addFiles();
    void addMeasurements();

signals:
    void modified();
    void currentSampleChanged(Sample *sample);
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
    QTreeWidgetItem *createSampleItem(const Sample &sample);
    Sample *sampleOf(QTreeWidgetItem *item) const;
    AttachedFile &currentFile() const;
    Measurement *currentMeasurement() const;
    void showProperties(QTreeWidgetItem *item);
    void appendMetadataRow(const MetadataEntry &entry);

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
};

#endif // JARA_PROJECTPANEL_H
