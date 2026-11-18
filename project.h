#ifndef JARA_PROJECT_H
#define JARA_PROJECT_H

#include <memory>
#include <optional>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QColor>
#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>

#include "units.h"

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

// Recoiled target atoms (ERD) or incident ions scattered from them (RBS). For both, the element of a cut is that of
// the target atom. The energy of scattered ions is calculated with the mass of the incident ion.
enum class CutType { Recoil, Scattered };

// Channel of the leading (short ToF) edge of a selection's ToF spectrum, accepted in the ToF calibration tool
struct ToFEdge {
    double channel = 0; // Centre of the edge, the mean of the fitted Gaussian
    double sigma = 0; // Width of the edge in channels, zero when the channel was set by hand
    bool manual = false; // Set by hand instead of fitted
};

// Polygon selection of events in a ToF-E histogram, assigned to an element
struct Cut {
    int id = 0; // Unique within the measurement, energy spectra refer to cuts by it
    int z = 0;
    int a = 0; // Mass number, zero when no specific isotope is selected
    QList<QPointF> polygon; // ToF and E in channels
    QColor color; // Drawn with this color
    CutType type = CutType::Recoil;
    std::optional<ToFEdge> tofEdge; // Calibration point of the detector, see tofcalibration.h
};

// The element or isotope, with an asterisk for scattered ions. With superscript, the mass number is in Unicode
// superscript digits.
QString cutLabel(int z, int a, CutType type, bool superscript = true);
inline QString cutLabel(const Cut &cut) { return cutLabel(cut.z, cut.a, cut.type); }

// Values are in SI units, see units.h. Angles are measured from the sample surface. IBM geometry
// is assumed, so the exit angle is the scattering angle minus the incident angle.
constexpr double MinIncidentAngle = 0;
constexpr double MaxIncidentAngle = 90 * Units::Degree;
constexpr double MinScatteringAngle = 1 * Units::Degree;
constexpr double MaxScatteringAngle = 180 * Units::Degree;

// Parameters of an experiment, shared by the measurements assigned to them. More parameters will be added.
struct ExperimentalSettings {
    QString name;
    double beamEnergy = 0; // J
    int beamZ = 0; // Incident ion, always a specific isotope
    int beamA = 0;
    double incidentAngle = MinIncidentAngle; // rad
};

// Element of a material, with its natural isotopic composition
struct MaterialElement {
    int z = 0;
    double fraction = 0; // Atomic fraction, from 0 to 1
    bool operator==(const MaterialElement &) const = default;
};

// Fractions of the elements sum up to one in a valid material
struct Material {
    QList<MaterialElement> elements;
    bool operator==(const Material &) const = default;
};

constexpr double MaterialFractionTolerance = 1e-9; // Allowed difference of the sum of the fractions from one
bool isValidMaterial(const Material &material); // Has elements, all known and not negative, and they sum up to one
double averageAtomMass(const Material &material); // kg, weighted by the atomic fractions
QString materialText(const Material &material); // "C" or "C 95 %, H 5 %"
Material carbon();

// Default foil of the first timing detector, a typical DLC foil
constexpr double DefaultFoilThickness = 3 * Units::MicrogramPerSquareCentimetre; // kg/m²

// Parameters of the detector, shared by the measurements assigned to them
struct DetectorSettings {
    QString name;
    double scatteringAngle = MinScatteringAngle; // rad, scattering/recoil angle
    double tofLength = 0; // m
    double tofSlope = 0; // s/ch, not negative. ToF calibration t = slope * channel + offset
    double tofOffset = 0; // s
    // Foil of the first timing detector. Particles lose energy in it before their time of flight is measured.
    Material foilMaterial = carbon();
    double foilThickness = DefaultFoilThickness; // kg/m²
};

// Default bin width of new energy spectra
constexpr double DefaultSpectrumBinWidth = 10 * Units::Kiloelectronvolt; // J

// 1D energy spectrum of some of a measurement's selections, energies from the ToF calibration of the detector
struct EnergySpectrum {
    QString name;
    double binWidth = DefaultSpectrumBinWidth; // J
    QList<int> cutIds; // Included selections
};

struct Measurement {
    QString name;
    QString description;
    QString path; // Data file (*.asc), only referenced, never copied or moved
    QList<Cut> cuts;
    ExperimentalSettings *settings = nullptr; // Not owned, belong to the project. nullptr when not assigned.
    DetectorSettings *detector = nullptr; // Same as above
    std::vector<std::unique_ptr<EnergySpectrum>> spectra; // Pointers stay valid, spectrum views refer to them

    // Exit angle from the surface, available when both experimental and detector settings are assigned
    bool hasExitAngle() const { return settings && detector; }
    double exitAngle() const { return detector->scatteringAngle - settings->incidentAngle; }
    int newCutId() const; // Not used by any cut of this measurement
    void assignCutIds(); // Gives cuts without an id, or with an id already used by an earlier cut, a new one
    const Cut *cut(int id) const; // nullptr when there is no such cut
    void removeCut(int index); // Also from the spectra
    QString uniqueSpectrumName(const QString &name) const;
};

// Copy of a measurement with copies of its spectra
std::unique_ptr<Measurement> copyMeasurement(const Measurement &measurement);

// "Name" when it is not taken, otherwise "Name (2)", "Name (3)" and so on
QString uniqueName(const QString &name, const QStringList &taken);

struct Sample {
    QString name;
    QString description;
    QList<MetadataEntry> metadata;
    QList<AttachedFile> files;
    std::vector<std::unique_ptr<Measurement>> measurements; // Pointers stay valid, measurement views refer to them
};

// Cuts as stored in project files, also used for copying cuts through the clipboard. Cuts without a color get the
// default color of their element.
QJsonArray cutsToJson(const QList<Cut> &cuts);
QList<Cut> cutsFromJson(const QJsonArray &json);

// Settings as stored in project files
QJsonObject settingsToJson(const ExperimentalSettings &settings);
ExperimentalSettings settingsFromJson(const QJsonObject &json);
QJsonObject detectorToJson(const DetectorSettings &detector);
DetectorSettings detectorFromJson(const QJsonObject &json);

// Settings copied through the clipboard or exported to a file:
// {"format": "jara-settings", "version": 4, "experimentalSettings": [...], "detectorSettings": [...]}.
// Project files have the same lists, so settings can be imported from them too.
struct SettingsCollection {
    QList<ExperimentalSettings> experimental;
    QList<DetectorSettings> detectors;
    bool fromProject = false; // Read from a project file, which has much more than these settings
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
    Sample *addSample(std::unique_ptr<Sample> sample);
    void removeSample(size_t index);
    QString uniqueSampleName(const QString &name) const;

    // Samples, and the measurements of each sample, are kept in natural order by name ("run2" before "run10").
    // Called after names change or items are added.
    void sortByName();

private:
    QString m_fileName;
    bool m_modified = false;
    std::vector<std::unique_ptr<ExperimentalSettings>> m_settings; // Pointers stay valid, measurements refer to them
    std::vector<std::unique_ptr<DetectorSettings>> m_detectors; // Same as above
    std::vector<std::unique_ptr<Sample>> m_samples; // Pointers stay valid when samples are added or removed
};

#endif // JARA_PROJECT_H
