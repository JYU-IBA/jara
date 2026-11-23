/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#ifndef JARA_PROJECT_H
#define JARA_PROJECT_H

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QColor>
#include <QHash>
#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>

#include "coincidence.h"
#include "units.h"

struct MetadataEntry {
    QString key;
    QString value;
};

// Paths are absolute in memory. On disk they are stored relative to the project file when the file is in the
// project directory or below it, otherwise absolute.
//
// Referenced files have the SHA-256 checksums they had when they were added to the project, so that changed files can
// be noticed when the project is opened.

struct AttachedFile {
    QString path;
    QString description;
    QString sha256; // Hex, empty when not known yet
};

// SHA-256 of a file in hex, empty when it cannot be read
QString fileChecksum(const QString &path);

// Recoiled target atoms (ERD) or incident ions scattered from them (RBS). For both, the element of a cut is that of
// the target atom. The energy of scattered ions is calculated with the mass of the incident ion. Reference selections
// are not used in energy spectra or depth profiles, they divide the measurement into parts, see split.h. Their
// element is optional (z = 0).
enum class CutType { Recoil, Scattered, Reference };

// Channel of the leading (short ToF) edge of a selection's ToF spectrum, accepted in the ToF calibration tool
struct ToFEdge {
    double channel = 0; // Centre of the edge, the mean of the fitted Gaussian
    double sigma = 0; // Width of the edge in channels, zero when the channel was set by hand
    bool manual = false; // Set by hand instead of fitted
};

// Polygon selection of events in a ToF-E histogram, assigned to an element
struct Cut {
    int id = 0; // Unique within the measurement, energy spectra refer to cuts by it
    int z = 0; // Zero only for a reference selection without an element
    int a = 0; // Mass number, zero when no specific isotope is selected
    QList<QPointF> polygon; // ToF and E in channels
    QColor color; // Drawn with this color
    CutType type = CutType::Recoil;
    std::optional<ToFEdge> tofEdge; // Calibration point of the detector, see tofcalibration.h
    // The isotope stands for the natural element in depth profiles: its events are weighted by 1 / natural abundance
    // and the profile is labeled with the element. Only for isotopes that occur naturally.
    bool representsElement = false;
    // Natural abundance of the isotope set by the user, used instead of the one from JIBAL when the isotope represents
    // its element
    std::optional<double> abundance;
    double weight = 1; // Additional weight of the events in depth profiles, MinCutWeight...MaxCutWeight
};

constexpr double MinCutWeight = 0;
constexpr double MaxCutWeight = 100;

// The selection can represent its natural element, see Cut::representsElement
bool canRepresentElement(int z, int a);
// Natural abundance of the cut's isotope: the one set by the user, or the one from JIBAL
double cutAbundance(const Cut &cut);
// Weight of the cut's events for its isotope standing for the element: 1 / natural abundance when the isotope represents
// its element, otherwise 1
double abundanceCorrection(const Cut &cut);

// The element or isotope, with an asterisk for scattered ions and " (ref)" for reference selections, "Ref" for a
// reference selection without an element. With superscript, the mass number is in Unicode superscript digits.
QString cutLabel(int z, int a, CutType type, bool superscript = true);
// As above, a reference selection without an element is told apart by its id ("Ref 3")
QString cutLabel(const Cut &cut, bool superscript = true);
// Selections, and everything listed from them (spectra, depth profiles, legends, tables), are in this order: by Z, then
// the mass number (the element before its isotopes), recoils before scattered ions and references. References without
// an element come last. Stable, so selections of the same particle keep their order.
bool cutBefore(const Cut &a, const Cut &b);
void sortCuts(QList<Cut> &cuts);
// Used in energy spectra and depth profiles, i.e. not a reference selection
inline bool isAnalyzed(const Cut &cut) { return cut.type != CutType::Reference; }

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
void sortElements(Material &material); // By Z, as they are listed
double averageAtomMass(const Material &material); // kg, weighted by the atomic fractions
QString materialText(const Material &material); // "C" or "C 95 %, H 5 %"
Material carbon();

// Detection efficiency of an isotope against the energy of the particle as it leaves the sample, i.e. before the
// timing detector foil. Linearly interpolated between the points, zero outside them.
struct EfficiencyCurve {
    int z = 0;
    int a = 0; // Mass number, efficiencies are measured for isotopes
    QList<QPointF> points; // Energy in J, efficiency from 0 to 1, increasing energies
    bool operator==(const EfficiencyCurve &) const = default;
};

double efficiency(const EfficiencyCurve &curve, double energy);

// The curve of an isotope, nullptr when there is none. Without a mass number (a = 0), the curve of the most abundant
// isotope.
const EfficiencyCurve *findEfficiency(const QList<EfficiencyCurve> &curves, int z, int a);
void sortEfficiencies(QList<EfficiencyCurve> &curves); // By Z, then the mass number

// Default foil of the first timing detector, a typical DLC foil
constexpr double DefaultFoilThickness = 3 * Units::MicrogramPerSquareCentimetre; // kg/m²

// Timing coordinate of the events (third column of event files, ticks) to the detection angle, see anglecalibration.h.
// The detector window spans centre(tof) ± halfWidth, with centre(tof) = centre + centreSlope * ToF channel.
struct AngleCalibration {
    bool valid = false; // Fitted and applied in the angle calibration tool
    double centre = 0; // Ticks, at ToF channel 0
    double centreSlope = 0; // Ticks/ch
    double halfWidth = 0; // Ticks
    double sigma = 0; // Ticks, timing resolution, for information
    bool reversed = false; // Larger timing is a smaller angle
    double maxTof = 0; // ToF channel, the longest ToF used in the fit, remembered for the tool
    double timingSmoothing = 0; // Ticks, timing values are spread uniformly over ± this, in the fit and the correction
    bool operator==(const AngleCalibration &) const = default;
};

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
    QList<EfficiencyCurve> efficiencies; // At most one for each isotope, isotopes without one are detected fully
    CoincidenceSettings coincidence; // For converting list-mode (*.evnt) files
    double detectorDistance = 0; // m, from the sample to the window of the energy detector
    double windowWidth = 0; // m, of the detector window in the scattering plane
    AngleCalibration angleCalibration;
    // The calibration and the geometry give each event an angle, see anglecalibration.h
    bool hasAngleCalibration() const { return angleCalibration.valid && detectorDistance > 0 && windowWidth > 0; }
    const EfficiencyCurve *efficiency(int z, int a) const { return findEfficiency(efficiencies, z, a); }
};

// Corner of the plot with the legend. Default follows the preferences of depth profiles.
enum class LegendPosition { Default, TopRight, TopLeft, BottomRight, BottomLeft };
constexpr LegendPosition LegendCorners[] = {LegendPosition::TopRight, LegendPosition::TopLeft, LegendPosition::BottomRight,
                                            LegendPosition::BottomLeft}; // All but Default
QString legendPositionName(LegendPosition position); // As shown to the user
QString legendPositionId(LegendPosition position); // In files and the preferences
LegendPosition legendPositionFromId(const QString &id, LegendPosition fallback);
Qt::Corner legendCorner(LegendPosition position); // Default is the top right corner
LegendPosition legendPosition(Qt::Corner corner);

// Default bin width of new energy spectra
constexpr double DefaultSpectrumBinWidth = 10 * Units::Kiloelectronvolt; // J

// 1D energy spectrum of some of a measurement's selections, energies from the ToF calibration of the detector
struct EnergySpectrum {
    QString name;
    double binWidth = DefaultSpectrumBinWidth; // J
    QList<int> cutIds; // Included selections
    // Part of the measurement used, as fractions of the counts of the measurement's reference, see split.h
    double rangeMin = 0;
    double rangeMax = 1;
    LegendPosition legendPosition = LegendPosition::TopRight; // Corner of the plot, dragged there
};

// Counts of selections in consecutive parts of a measurement with equal counts of the measurement's reference, to
// see whether counts are lost during the measurement, e.g. hydrogen released by the beam. See split.h.
constexpr int DefaultSplitParts = 10;
constexpr int MinSplitParts = 2;
constexpr int MaxSplitParts = 1000;
struct SplitAnalysis {
    QString name;
    int parts = DefaultSplitParts;
    QList<int> cutIds; // Shown selections, references included
    bool relative = false; // Counts shown relative to the mean of each selection
    LegendPosition legendPosition = LegendPosition::TopRight; // Corner of the plot, dragged there
};

// Screening of the Rutherford cross sections of depth profiles
enum class ScreeningModel { None, Andersen };
constexpr ScreeningModel ScreeningModels[] = {ScreeningModel::None, ScreeningModel::Andersen};
QString screeningModelName(ScreeningModel model); // As shown to the user

// Target mass of nuclear stopping: the average mass of the element, or each natural isotope with its own mass,
// weighted by its abundance
enum class NuclearStoppingMass { Average, Isotopes };
constexpr NuclearStoppingMass NuclearStoppingMasses[] = {NuclearStoppingMass::Average, NuclearStoppingMass::Isotopes};
QString nuclearStoppingMassName(NuclearStoppingMass mass); // As shown to the user

// How stopping is interpolated between the points of the stopping files: linearly in energy, as JIBAL does, or with a
// cubic spline in the logarithm of the energy
enum class StoppingInterpolation { Linear, Cubic };
constexpr StoppingInterpolation StoppingInterpolations[] = {StoppingInterpolation::Linear, StoppingInterpolation::Cubic};
QString stoppingInterpolationName(StoppingInterpolation interpolation); // As shown to the user

constexpr int AnyElement = -1; // As the target of a stopping correction, any element. The same as JIBAL_ANY_Z.

// Stopping of incident ions of one element (Z1) in atoms of another (Z2), or of any element, different from that of
// all elements
struct StoppingCorrection {
    int z1 = 1; // Zero while being edited, such corrections are not used or saved
    int z2 = 1; // AnyElement for all targets without a correction of their own
    double electronicFactor = 1; // Multiplies the factor of all elements
    double nuclearFactor = 1; // Same as above
    QString stoppingFile; // Name of the GSTO file of electronic stopping, empty for the one of all elements
    bool operator==(const StoppingCorrection &) const = default;
};

// How stopping and cross sections are calculated, shared by the measurements assigned to them. Measurements without
// physics settings use the defaults of this struct.
struct PhysicsSettings {
    QString name;
    ScreeningModel screening = ScreeningModel::Andersen;
    double electronicFactor = 1; // Electronic stopping of all elements is multiplied by this
    double nuclearFactor = 1; // Same for nuclear stopping
    NuclearStoppingMass nuclearMass = NuclearStoppingMass::Average;
    StoppingInterpolation interpolation = StoppingInterpolation::Linear;
    QString stoppingFile; // Name of the GSTO file of electronic stopping of all elements, empty for JIBAL's choice
    QList<StoppingCorrection> corrections; // At most one for each Z1, Z2 pair
    bool operator==(const PhysicsSettings &) const = default;
    const StoppingCorrection *correction(int z1, int z2) const; // nullptr when the pair has none, exactly as listed
    // The correction that applies to the pair: its own, or that of the incident element in any target. nullptr when
    // neither is listed.
    const StoppingCorrection *correctionFor(int z1, int z2) const;
    // By the incident element, then the target, any target after the elements. Pairs still being edited (an element
    // zero) come last.
    void sortCorrections();
};

// Built-in defaults of new depth profiles, see also the preferences in defaults.h. Depths are areal densities in atoms/m², 1e15 at./cm² = Units::ThinFilmUnit.
constexpr double DefaultDepthBinWidth = 20 * Units::ThinFilmUnit;
constexpr double DefaultStoppingBinWidth = 20 * Units::ThinFilmUnit;
constexpr double DefaultMaxDepth = 5000 * Units::ThinFilmUnit;
constexpr int DefaultMinStoppingBinEvents = 10;
constexpr double DefaultAverageMin = 0;
constexpr double DefaultAverageMax = 500 * Units::ThinFilmUnit;
constexpr double DefaultDensity = 2000; // kg/m³, converts depths to nm

// Shown ranges of a depth profile plot
struct PlotView {
    double depthMin = 0; // atoms/m²
    double depthMax = DefaultAverageMax;
    double concentrationMin = 0; // Atomic fraction
    double concentrationMax = 1;
    bool logScale = false;
    bool operator==(const PlotView &) const = default;
};
QJsonObject plotViewToJson(const PlotView &view);
PlotView plotViewFromJson(const QJsonObject &json);

// Depth profile of some of a measurement's selections from single scattering (slab analysis), see depthprofile.h
struct DepthProfile {
    QString name;
    QList<int> cutIds; // Included selections
    double binWidth = DefaultDepthBinWidth; // atoms/m², of the profile
    double stoppingBinWidth = DefaultStoppingBinWidth; // atoms/m², of the composition used for stopping
    double maxDepth = DefaultMaxDepth; // atoms/m², of the calculation, or where it starts when automatic
    bool autoMaxDepth = true; // The calculation follows the depth the events reach
    int minStoppingBinEvents = DefaultMinStoppingBinEvents; // Pseudo-events from the bin before, see depthprofile.h
    bool autoNormalization = true; // Normalization interval found from the data instead of the one below
    double normalizationMin = 0; // atoms/m², the total concentration is 100 % on average in this interval
    double normalizationMax = DefaultAverageMax;
    double averageMin = DefaultAverageMin; // atoms/m², interval of the average concentrations
    double averageMax = DefaultAverageMax;
    // The averages are over the normalization interval instead of the one above. The renormalized averages are then
    // not needed, the total is 100 % on average over the interval.
    bool averageFollowsNormalization = true;
    QList<int> cutIdsExcludedFromAverage; // Left out of the renormalized averages, e.g. the substrate
    double density = DefaultDensity; // kg/m³
    bool separateIsotopes = false; // Selections of an element are summed into one unless this is set, see depthprofile.h
    // The energy of each event is corrected to the scattering/recoil angle when the detector has an angle calibration,
    // see depthprofile.h
    bool kinematicCorrection = true;
    // Part of the measurement used, as fractions of the counts of the measurement's reference, see split.h
    double rangeMin = 0;
    double rangeMax = 1;
    // Narrower parts of the range above for some selections, by cut id, as fractions of the range. Their events are
    // weighted by the reference counts in the range / in their part, see depthprofile.h.
    QHash<int, std::pair<double, double>> cutRanges;
    // What the plot shows, the calculation is not affected
    QList<int> cutIdsHiddenFromPlot;
    bool showTotal = false;
    bool showNormalization = false; // The interval where the total is 100 %
    bool shadeAverage = true; // The averaging interval is shaded, its ends are always drawn
    bool showLabels = false; // Names of the selections in the plot at their averages
    QList<int> cutIdsWithoutLabel; // Shown selections left without a label
    // Centres of the labels dragged by the user, by the first selection of their curve: x the depth in atoms/m², y the
    // atomic fraction. The other labels are placed automatically.
    QHash<int, QPointF> labelPositions;
    LegendPosition legendPosition = LegendPosition::Default;
    bool showLegendConcentrations = true; // The renormalized averages in the legend
    // Averages rounded to their uncertainties and upper limits for those not clearly above zero, see upperLimit(): in
    // the table of averages, also in the export and reports, and in the legend
    bool roundTableConcentrations = false;
    bool roundLegendConcentrations = true;
    std::optional<PlotView> view; // Zoomed by the user, otherwise everything is shown
};

struct Measurement {
    QString name;
    QString description;
    QString path; // Data file (*.asc), only referenced, never copied or moved
    QList<Cut> cuts;
    ExperimentalSettings *settings = nullptr; // Not owned, belong to the project. nullptr when not assigned.
    DetectorSettings *detector = nullptr; // Same as above
    PhysicsSettings *physics = nullptr; // Same as above, without them the default physics settings are used
    std::vector<std::unique_ptr<EnergySpectrum>> spectra; // Pointers stay valid, spectrum views refer to them
    std::vector<std::unique_ptr<DepthProfile>> depthProfiles; // Same as above
    QString sha256; // Of the data file, see AttachedFile
    std::vector<std::unique_ptr<SplitAnalysis>> splitAnalyses; // Same as above
    // Selection whose counts divide the measurement into parts, see split.h. Zero when there is none, all events then
    // count.
    int referenceCutId = 0;

    // Exit angle from the surface, available when both experimental and detector settings are assigned
    bool hasExitAngle() const { return settings && detector; }
    double exitAngle() const { return detector->scatteringAngle - settings->incidentAngle; }
    // The assigned physics settings, or the defaults
    const PhysicsSettings &physicsOrDefault() const;
    int newCutId() const; // Not used by any cut of this measurement
    void assignCutIds(); // Gives cuts without an id, or with an id already used by an earlier cut, a new one
    const Cut *cut(int id) const; // nullptr when there is no such cut
    const Cut *referenceCut() const { return cut(referenceCutId); } // nullptr when all events count
    QList<QPointF> referencePolygon() const; // Of the reference, empty when all events count
    void removeCut(int index); // Also from the spectra, depth profiles, split analyses and as the reference
    void sortCuts(); // In the order of sortCuts(QList<Cut> &), call after cuts were added or their elements changed
    QString uniqueSpectrumName(const QString &name) const;
    QString uniqueDepthProfileName(const QString &name) const;
    QString uniqueSplitAnalysisName(const QString &name) const;
};

// Copy of a measurement with copies of its spectra, depth profiles and split analyses
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
QJsonObject physicsToJson(const PhysicsSettings &physics);
PhysicsSettings physicsFromJson(const QJsonObject &json);

// Settings copied through the clipboard or exported to a file: {"format": "jara-settings", "version": 4,
// "experimentalSettings": [...], "detectorSettings": [...], "physicsSettings": [...]}. Project files have the same
// lists, so settings can be imported from them too.
struct SettingsCollection {
    QList<ExperimentalSettings> experimental;
    QList<DetectorSettings> detectors;
    QList<PhysicsSettings> physics;
    bool fromProject = false; // Read from a project file, which has much more than these settings
    bool isEmpty() const { return experimental.isEmpty() && detectors.isEmpty() && physics.isEmpty(); }
    qsizetype size() const { return experimental.size() + detectors.size() + physics.size(); }
};
QByteArray settingsCollectionToJson(const SettingsCollection &collection);
bool settingsCollectionFromJson(const QByteArray &data, SettingsCollection &collection, QString &errorMessage);

// Items of a report's settings section, see Report
struct ReportSettings {
    bool ion = true;
    bool energy = true;
    bool incidentAngle = true;
    bool scatteringAngle = true;
    bool tofLength = false;
    bool foil = false;
    bool tofCalibration = false;
    bool angleCalibration = false; // With the kinematic correction of the profile
    bool efficiencies = false;
    bool physics = false;
    bool profileParameters = false;
};

// A PDF of depth profiles of the project: their average concentrations in a table with one column per element, the
// settings of their measurements, their plots and an appendix of split analyses, see reportpdf.h
struct Report {
    enum class NameMode { Auto, On, Off }; // Measurement names in the rows, Auto when a sample has several rows
    enum class RangeMode { Gui, Common, PerProfile }; // Ranges of the plots: as in the tabs, one for all, each its own

    QString name; // In the project tree
    QString title; // Of the PDF
    // Included items. They belong to the measurements of the project, which keep them while they exist; the project
    // removes them from reports when they are removed.
    QList<const DepthProfile *> profiles;
    QList<const SplitAnalysis *> splits;
    NameMode measurementNames = NameMode::Auto;
    ReportSettings settings;
    RangeMode rangeMode = RangeMode::Gui;
    PlotView commonView;
    QHash<const DepthProfile *, PlotView> profileViews; // With RangeMode::PerProfile, the GUI view when missing
    // Of the included profiles and of the options when the PDF was last generated, see depthProfileFingerprint()
    QHash<const DepthProfile *, QString> fingerprints;
    QString optionsFingerprint;
    QDateTime generated; // Invalid until generated
};

// Changes when anything the report shows of the profile changes: the profile, the measurement's selections, settings,
// reference and data file. The zoom of the profile's tab only counts with the ranges of the tabs.
QString depthProfileFingerprint(const Measurement &measurement, const DepthProfile &profile, bool withView);
// Changes when the options of the report or the name of the project change, apart from its name in the project tree
QString reportOptionsFingerprint(const Report &report, const QString &projectName,
                                 const std::function<const Measurement *(const SplitAnalysis *)> &measurementOf);
// Generated, and no included profile has changed since
bool isReportUpToDate(const Report &report, const QString &projectName,
                      const std::function<const Measurement *(const DepthProfile *)> &measurementOf,
                      const std::function<const Measurement *(const SplitAnalysis *)> &splitMeasurementOf);

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
    // The project as saved, with each referenced file stored as storedPath gives it. Files without a checksum get one.
    QByteArray toJson(const std::function<QString(const QString &path)> &storedPath);

    // Compares the checksums of the referenced files with the ones in the project. The checksums are kept until the
    // changes are accepted, so a changed file is reported every time until then. Files without a checksum get one
    // silently.
    struct FileProblem {
        QString path;
        bool missing = false; // Cannot be read, otherwise the checksum has changed
        QString sha256; // Of the file now, when it has changed
    };
    QList<FileProblem> verifyFiles();
    void acceptChangedFiles(const QList<FileProblem> &problems); // The changed checksums replace the ones in the project
    // Every file referenced once, measurements first
    QStringList referencedFiles() const;
    // Archive (.tar.gz or .zip, see archive.h) with the project file and copies of the referenced files, with the
    // paths changed to the copies, so that the project can be opened elsewhere. Missing files are left out.
    bool exportArchive(const QString &archiveName, QString &errorMessage);

    // Local time with the offset from UTC, to the second
    const QDateTime &created() const { return m_created; }
    const QDateTime &saved() const { return m_saved; } // Invalid until saved

    const QString &fileName() const { return m_fileName; }
    // Shown in the project tree and in reports, the file name is used when empty
    const QString &name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }
    QString displayName() const;
    const QString &notes() const { return m_notes; }
    void setNotes(const QString &notes) { m_notes = notes; }
    // Files of the project as a whole, e.g. a plan or a log of the measurements, like the files of samples
    QList<AttachedFile> &files() { return m_files; }
    const QList<AttachedFile> &files() const { return m_files; }
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

    size_t physicsCount() const { return m_physics.size(); }
    PhysicsSettings *physics(size_t index) { return m_physics.at(index).get(); }
    PhysicsSettings *addPhysics(const PhysicsSettings &physics); // Copied
    QString uniquePhysicsName(const QString &name) const;
    void removePhysics(size_t index); // Measurements assigned to them are left without physics settings
    int physicsIndex(const PhysicsSettings *physics) const; // -1 when not in this project
    QList<Measurement *> measurementsUsing(const PhysicsSettings *physics) const;

    size_t sampleCount() const { return m_samples.size(); }
    Sample *sample(size_t index) { return m_samples.at(index).get(); }
    Sample *addSample(const QString &name);
    Sample *addSample(std::unique_ptr<Sample> sample);
    void removeSample(size_t index);
    QString uniqueSampleName(const QString &name) const;

    // Samples, and the measurements of each sample, are kept in natural order by name ("run2" before "run10").
    // Called after names change or items are added.
    void sortByName();

    size_t reportCount() const { return m_reports.size(); }
    Report *report(size_t index) { return m_reports.at(index).get(); }
    Report *addReport(const QString &name); // Includes all depth profiles of the project
    void removeReport(size_t index);
    QString uniqueReportName(const QString &name) const;
    // The measurement a depth profile or a split analysis belongs to, and its sample, nullptr when not in the project
    const Measurement *measurementOf(const DepthProfile *profile) const;
    const Measurement *measurementOf(const SplitAnalysis *split) const;
    const Sample *sampleOf(const Measurement *measurement) const;
    // Before these are removed, so that no report refers to them
    void removeFromReports(const Sample *sample);
    void removeFromReports(const Measurement *measurement);
    void removeFromReports(const DepthProfile *profile);
    void removeFromReports(const SplitAnalysis *split);

private:
    QString m_fileName;
    QString m_name;
    QString m_notes;
    QList<AttachedFile> m_files;
    bool m_modified = false;
    QDateTime m_created = QDateTime::currentDateTime();
    QDateTime m_saved;
    std::vector<std::unique_ptr<ExperimentalSettings>> m_settings; // Pointers stay valid, measurements refer to them
    std::vector<std::unique_ptr<DetectorSettings>> m_detectors; // Same as above
    std::vector<std::unique_ptr<PhysicsSettings>> m_physics; // Same as above
    std::vector<std::unique_ptr<Sample>> m_samples; // Pointers stay valid when samples are added or removed
    std::vector<std::unique_ptr<Report>> m_reports; // Pointers stay valid, report views refer to them

    std::unique_ptr<Report> reportFromJson(const QJsonObject &json) const; // After the samples are read
};

// The directory the open project was last saved to or opened from, the default location of file dialogs. Kept in
// memory only, and after the project is closed.
const QString &projectDirectory();
void setProjectDirectory(const QString &directory);
// fileName in the project directory, as is when there is no project directory yet
QString inProjectDirectory(const QString &fileName);

#endif // JARA_PROJECT_H
