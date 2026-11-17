#ifndef JARA_PROJECT_H
#define JARA_PROJECT_H

#include <memory>
#include <vector>

#include <QList>
#include <QPointF>
#include <QString>

struct MetadataEntry {
    QString key;
    QString value;
};

// Paths are absolute in memory. On disk they are stored relative to the project file when the file is in the
// project directory or below it, otherwise absolute.

struct AttachedFile {
    QString path;
    QString description;
};

// Polygon selection of events in a ToF-E histogram, assigned to an element
struct Cut {
    int z = 0;
    int a = 0; // Mass number, zero when no specific isotope is selected
    QList<QPointF> polygon; // ToF and E in channels
};

struct Measurement {
    QString name;
    QString description;
    QString path; // Data file (*.asc), only referenced, never copied or moved
    QList<Cut> cuts;
};

struct Sample {
    QString name;
    QString description;
    QList<MetadataEntry> metadata;
    QList<AttachedFile> files;
    std::vector<std::unique_ptr<Measurement>> measurements; // Pointers stay valid, measurement views refer to them
};

class Project {
public:
    static std::unique_ptr<Project> load(const QString &fileName, QString &errorMessage);
    bool save(const QString &fileName, QString &errorMessage);

    const QString &fileName() const { return m_fileName; }
    bool isModified() const { return m_modified; }
    void setModified(bool modified) { m_modified = modified; }

    size_t sampleCount() const { return m_samples.size(); }
    Sample *sample(size_t index) { return m_samples.at(index).get(); }
    Sample *addSample(const QString &name);
    void removeSample(size_t index);

private:
    QString m_fileName;
    bool m_modified = false;
    std::vector<std::unique_ptr<Sample>> m_samples; // Pointers stay valid when samples are added or removed
};

#endif // JARA_PROJECT_H
