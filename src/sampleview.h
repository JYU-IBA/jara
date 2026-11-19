#ifndef JARA_SAMPLEVIEW_H
#define JARA_SAMPLEVIEW_H

#include <QStringList>
#include <QWidget>

class QLineEdit;
class QPlainTextEdit;
class QTableWidget;
class QTableWidgetItem;
struct Sample;

// Tab in the main work area with a sample's name, description, metadata and the descriptions of its attached files.
// Files and measurements are added and removed in the project tree.
class SampleView : public QWidget {
    Q_OBJECT

public:
    explicit SampleView(Sample *sample, QWidget *parent = nullptr);
    Sample *sample() const { return m_sample; }

public slots:
    void refresh(); // Call after the project has changed, e.g. the sample was renamed or files were added

signals:
    void modified();
    void renamed(Sample *sample); // While the name is typed
    void renameFinished(); // When the name has been entered, samples are sorted by it

private slots:
    void addMetadata();
    void removeMetadata();
    void metadataItemChanged(QTableWidgetItem *item);
    void fileItemChanged(QTableWidgetItem *item);

private:
    void updateMetadata();
    void updateFiles(); // Rebuilt only when the files change, not while their descriptions are edited

    Sample *m_sample;
    QLineEdit *m_nameEdit;
    QPlainTextEdit *m_descriptionEdit;
    QTableWidget *m_metadataTable;
    QTableWidget *m_fileTable;
    QStringList m_filesKey;
};

#endif // JARA_SAMPLEVIEW_H
