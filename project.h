#ifndef JARA_PROJECT_H
#define JARA_PROJECT_H

#include <memory>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>
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

// Angles are in degrees and measured from the sample surface. IBM geometry is assumed, so the exit angle is the
// scattering angle minus the incident angle.
constexpr double MinIncidentAngle = 0;
constexpr double MaxIncidentAngle = 90;
constexpr double MinScatteringAngle = 1;
constexpr double MaxScatteringAngle = 180;

// Parameters of an experiment, shared by the measurements assigned to them. More parameters will be added.
struct ExperimentalSettings {
    QString name;
    double beamEnergy = 0; // MeV
    int beamZ = 0; // Incident ion, always a specific isotope
    int beamA = 0;
    double incidentAngle = MinIncidentAngle;
};

// Parameters of the detector, shared by the measurements assigned to them
struct DetectorSettings {
    QString name;
    double scatteringAngle = MinScatteringAngle; // Scattering/recoil angle
    double tofLength = 0; // m
    double tofSlope = 0; // ns/ch, ToF calibration t = slope * channel + offset
    double tofOffset = 0; // ns
};

struct Measurement {
    QString name;
    QString description;
    QString path; // Data file (*.asc), only referenced, never copied or moved
    QList<Cut> cuts;
    ExperimentalSettings *settings = nullptr; // Not owned, belong to the project. nullptr when not assigned.
    DetectorSettings *detector = nullptr; // Same as above

    // Exit angle from the surface, available when both experimental and detector settings are assigned
    bool hasExitAngle() const { return settings && detector; }
    double exitAngle() const { return detector->scatteringAngle - settings->incidentAngle; }
};

struct Sample {
    QString name;
    QString description;
    QList<MetadataEntry> metadata;
    QList<AttachedFile> files;
    std::vector<std::unique_ptr<Measurement>> measurements; // Pointers stay valid, measurement views refer to them
};

// Cuts as stored in project files, also used for copying cuts through the clipboard
QJsonArray cutsToJson(const QList<Cut> &cuts);
QList<Cut> cutsFromJson(const QJsonArray &json);

// Settings as stored in project files
QJsonObject settingsToJson(const ExperimentalSettings &settings);
ExperimentalSettings settingsFromJson(const QJsonObject &json);
QJsonObject detectorToJson(const DetectorSettings &detector);
DetectorSettings detectorFromJson(const QJsonObject &json);

// Settings copied through the clipboard or exported to a file:
// {"format": "jara-settings", "version": 1, "experimentalSettings": [...], "detectorSettings": [...]}.
// Project files have the same lists, so settings can be imported from them too.
struct SettingsCollection {
    QList<ExperimentalSettings> experimental;
    QList<DetectorSettings> detectors;
    bool isEmpty() const { return experimental.isEmpty() && detectors.isEmpty(); }
};
QByteArray settingsCollectionToJson(const SettingsCollection &collection);
bool settingsCollectionFromJson(const QByteArray &data, SettingsCollection &collection, QString &errorMessage);

class Project {
public:
    // Calls f for every measurement in the project
    template<typename F> void forEachMeasurement(F f) const {
        for(const auto &sample : m_samples) {
            for(const auto &measurement : sample->measurements) {
                f(measurement.get());
            }
        }
    }

    static std::unique_ptr<Project> load(const QString &fileName, QString &errorMessage);
    bool save(const QString &fileName, QString &errorMessage);

    const QString &fileName() const { return m_fileName; }
    bool isModified() const { return m_modified; }
    void setModified(bool modified) { m_modified = modified; }

    size_t settingsCount() const { return m_settings.size(); }
    ExperimentalSettings *settings(size_t index) { return m_settings.at(index).get(); }
    ExperimentalSettings *addSettings(const ExperimentalSettings &settings); // Copied
    QString uniqueSettingsName(const QString &name) const; // name, or name with a number when it is taken
    void removeSettings(size_t index); // Measurements assigned to them are left without settings
    int settingsIndex(const ExperimentalSettings *settings) const; // -1 when not in this project
    QList<Measurement *> measurementsUsing(const ExperimentalSettings *settings) const;

    size_t detectorCount() const { return m_detectors.size(); }
    DetectorSettings *detector(size_t index) { return m_detectors.at(index).get(); }
    DetectorSettings *addDetector(const DetectorSettings &detector); // Copied
    QString uniqueDetectorName(const QString &name) const;
    void removeDetector(size_t index); // Measurements assigned to it are left without detector settings
    int detectorIndex(const DetectorSettings *detector) const; // -1 when not in this project
    QList<Measurement *> measurementsUsing(const DetectorSettings *detector) const;

    size_t sampleCount() const { return m_samples.size(); }
    Sample *sample(size_t index) { return m_samples.at(index).get(); }
    Sample *addSample(const QString &name);
    void removeSample(size_t index);

private:
    QString m_fileName;
    bool m_modified = false;
    std::vector<std::unique_ptr<ExperimentalSettings>> m_settings; // Pointers stay valid, measurements refer to them
    std::vector<std::unique_ptr<DetectorSettings>> m_detectors; // Same as above
    std::vector<std::unique_ptr<Sample>> m_samples; // Pointers stay valid when samples are added or removed
};

#endif // JARA_PROJECT_H
