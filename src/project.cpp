#include "project.h"
#include "archive.h"
#include "defaults.h"
#include "elements.h"

#include <algorithm>
#include <cmath>

#include <QCollator>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

namespace {
const QString FormatName = QStringLiteral("jara-project");
const QString SettingsFormatName = QStringLiteral("jara-settings");
const QString RecoilTypeName = QStringLiteral("recoil");
const QString ScatteredTypeName = QStringLiteral("scattered");

QString screeningModelId(ScreeningModel model) { // In files
    return model == ScreeningModel::None ? QStringLiteral("none") : QStringLiteral("andersen");
}
// Only files of this version are read, older versions are not converted
constexpr int FormatVersion = 4;

QString pathToJson(const QString &path, const QDir &baseDir) {
    if(path.isEmpty()) {
        return {};
    }
    const QString relative = baseDir.relativeFilePath(path);
    if(QDir::isAbsolutePath(relative) || relative == ".." || relative.startsWith("../")) {
        return path; // Outside the project directory
    }
    return relative;
}

QString pathFromJson(const QJsonValue &value, const QDir &baseDir) {
    const QString path = value.toString();
    return path.isEmpty() ? QString() : QDir::cleanPath(baseDir.absoluteFilePath(path));
}

// ISO 8601 in local time with the offset from UTC, to the second
QString timeToJson(const QDateTime &time) {
    return time.isValid() ? time.toOffsetFromUtc(time.offsetFromUtc()).toString(Qt::ISODate) : QString();
}

QDateTime timeFromJson(const QJsonValue &value) {
    return QDateTime::fromString(value.toString(), Qt::ISODate);
}

// Fills in a missing checksum, files referenced several times are read once
void fillChecksum(const QString &path, QString &sha256, QHash<QString, QString> &known) {
    if(!sha256.isEmpty() || path.isEmpty()) {
        return;
    }
    if(!known.contains(path)) {
        known[path] = fileChecksum(path);
    }
    sha256 = known[path];
}

template<typename T>
int indexOf(const std::vector<std::unique_ptr<T>> &list, const T *item) {
    for(size_t i = 0; i < list.size(); i++) {
        if(list[i].get() == item) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

QJsonArray idsToJson(const QList<int> &ids) {
    QJsonArray json;
    for(const int id : ids) {
        json.append(id);
    }
    return json;
}

QList<int> idsFromJson(const QJsonValue &json) {
    QList<int> ids;
    for(const auto &id : json.toArray()) {
        ids.append(id.toInt());
    }
    return ids;
}

QJsonObject depthProfileToJson(const DepthProfile &profile) {
    QJsonObject normalization{{"auto", profile.autoNormalization},
                              {"min", profile.normalizationMin},
                              {"max", profile.normalizationMax}};
    return QJsonObject{
            {"name", profile.name},
            {"cuts", idsToJson(profile.cutIds)},
            {"binWidth", profile.binWidth},
            {"stoppingBinWidth", profile.stoppingBinWidth},
            {"maxDepth", profile.maxDepth},
            {"minStoppingBinEvents", profile.minStoppingBinEvents},
            {"normalization", normalization},
            {"average", QJsonObject{{"min", profile.averageMin}, {"max", profile.averageMax},
                                    {"excludedCuts", idsToJson(profile.cutIdsExcludedFromAverage)}}},
            {"density", profile.density},
            {"separateIsotopes", profile.separateIsotopes},
            {"autoMaxDepth", profile.autoMaxDepth},
            {"plot", QJsonObject{{"hiddenCuts", idsToJson(profile.cutIdsHiddenFromPlot)},
                                 {"total", profile.showTotal},
                                 {"normalization", profile.showNormalization},
                                 {"legend", legendPositionId(profile.legendPosition)},
                                 {"legendConcentrations", profile.showLegendConcentrations}}}};
}

// Missing or invalid values are left at their defaults
std::unique_ptr<DepthProfile> depthProfileFromJson(const QJsonObject &json) {
    auto profile = std::make_unique<DepthProfile>();
    auto positive = [&json](const char *key, double &value) {
        if(const double read = json[key].toDouble(); read > 0) {
            value = read;
        }
    };
    profile->name = json["name"].toString();
    profile->cutIds = idsFromJson(json["cuts"]);
    positive("binWidth", profile->binWidth);
    positive("stoppingBinWidth", profile->stoppingBinWidth);
    positive("maxDepth", profile->maxDepth);
    positive("density", profile->density);
    profile->minStoppingBinEvents = std::max(1, json["minStoppingBinEvents"].toInt(profile->minStoppingBinEvents));
    const QJsonObject normalization = json["normalization"].toObject();
    profile->autoNormalization = normalization["auto"].toBool(true);
    profile->normalizationMin = normalization["min"].toDouble(profile->normalizationMin);
    profile->normalizationMax = normalization["max"].toDouble(profile->normalizationMax);
    const QJsonObject average = json["average"].toObject();
    profile->averageMin = average["min"].toDouble(profile->averageMin);
    profile->averageMax = average["max"].toDouble(profile->averageMax);
    profile->cutIdsExcludedFromAverage = idsFromJson(average["excludedCuts"]);
    profile->autoMaxDepth = json["autoMaxDepth"].toBool(profile->autoMaxDepth);
    profile->separateIsotopes = json["separateIsotopes"].toBool(profile->separateIsotopes);
    const QJsonObject plot = json["plot"].toObject();
    profile->cutIdsHiddenFromPlot = idsFromJson(plot["hiddenCuts"]);
    profile->showTotal = plot["total"].toBool(profile->showTotal);
    profile->showNormalization = plot["normalization"].toBool(profile->showNormalization);
    profile->legendPosition = legendPositionFromId(plot["legend"].toString(), profile->legendPosition);
    profile->showLegendConcentrations = plot["legendConcentrations"].toBool(profile->showLegendConcentrations);
    return profile;
}

QJsonObject sampleToJson(const Sample &sample, const std::function<QString(const QString &)> &storedPath,
                         const Project &project) {
    QJsonArray metadata;
    for(const auto &entry : sample.metadata) {
        metadata.append(QJsonObject{{"key", entry.key}, {"value", entry.value}});
    }
    QJsonArray files;
    for(const auto &file : sample.files) {
        files.append(QJsonObject{{"path", storedPath(file.path)}, {"description", file.description}, {"sha256", file.sha256}});
    }
    QJsonArray measurements;
    for(const auto &measurement : sample.measurements) {
        QJsonObject object{
                {"name", measurement->name},
                {"description", measurement->description},
                {"path", storedPath(measurement->path)},
                {"sha256", measurement->sha256},
                {"cuts", cutsToJson(measurement->cuts)}};
        QJsonArray spectra;
        for(const auto &spectrum : measurement->spectra) {
            QJsonArray cutIds;
            for(int id : spectrum->cutIds) {
                cutIds.append(id);
            }
            spectra.append(QJsonObject{{"name", spectrum->name}, {"binWidth", spectrum->binWidth}, {"cuts", cutIds}});
        }
        object["spectra"] = spectra;
        QJsonArray profiles;
        for(const auto &profile : measurement->depthProfiles) {
            profiles.append(depthProfileToJson(*profile));
        }
        object["depthProfiles"] = profiles;
        // Settings are referred to by their index in the project
        if(const int index = project.settingsIndex(measurement->settings); index >= 0) {
            object["settings"] = index;
        }
        if(const int index = project.detectorIndex(measurement->detector); index >= 0) {
            object["detector"] = index;
        }
        if(const int index = project.physicsIndex(measurement->physics); index >= 0) {
            object["physics"] = index;
        }
        measurements.append(object);
    }
    return QJsonObject{
            {"name", sample.name},
            {"description", sample.description},
            {"metadata", metadata},
            {"files", files},
            {"measurements", measurements}};
}

std::unique_ptr<Sample> sampleFromJson(const QJsonObject &json, const QDir &baseDir, Project &project) {
    auto sample = std::make_unique<Sample>();
    sample->name = json["name"].toString();
    sample->description = json["description"].toString();
    for(const auto &value : json["metadata"].toArray()) {
        const QJsonObject entry = value.toObject();
        sample->metadata.append({entry["key"].toString(), entry["value"].toString()});
    }
    for(const auto &value : json["files"].toArray()) {
        const QJsonObject file = value.toObject();
        const QString path = pathFromJson(file["path"], baseDir);
        if(path.isEmpty()) {
            continue;
        }
        sample->files.append({path, file["description"].toString(), file["sha256"].toString()});
    }
    for(const auto &value : json["measurements"].toArray()) {
        const QJsonObject measurement = value.toObject();
        sample->measurements.push_back(std::make_unique<Measurement>(
                measurement["name"].toString(), measurement["description"].toString(),
                pathFromJson(measurement["path"], baseDir), cutsFromJson(measurement["cuts"].toArray())));
        sample->measurements.back()->assignCutIds(); // In case of missing or duplicate ids
        sample->measurements.back()->sha256 = measurement["sha256"].toString();
        for(const auto &spectrumValue : measurement["spectra"].toArray()) {
            const QJsonObject object = spectrumValue.toObject();
            auto spectrum = std::make_unique<EnergySpectrum>();
            spectrum->name = object["name"].toString();
            const double binWidth = object["binWidth"].toDouble();
            spectrum->binWidth = binWidth > 0 ? binWidth : DefaultSpectrumBinWidth;
            for(const auto &id : object["cuts"].toArray()) {
                spectrum->cutIds.append(id.toInt());
            }
            sample->measurements.back()->spectra.push_back(std::move(spectrum));
        }
        for(const auto &profileValue : measurement["depthProfiles"].toArray()) {
            sample->measurements.back()->depthProfiles.push_back(depthProfileFromJson(profileValue.toObject()));
        }
        const int settingsIndex = measurement["settings"].toInt(-1);
        if(settingsIndex >= 0 && settingsIndex < static_cast<int>(project.settingsCount())) {
            sample->measurements.back()->settings = project.settings(settingsIndex);
        }
        const int detectorIndex = measurement["detector"].toInt(-1);
        if(detectorIndex >= 0 && detectorIndex < static_cast<int>(project.detectorCount())) {
            sample->measurements.back()->detector = project.detector(detectorIndex);
        }
        const int physicsIndex = measurement["physics"].toInt(-1);
        if(physicsIndex >= 0 && physicsIndex < static_cast<int>(project.physicsCount())) {
            sample->measurements.back()->physics = project.physics(physicsIndex);
        }
    }
    return sample;
}
}

QString uniqueName(const QString &name, const QStringList &taken) {
    if(!taken.contains(name)) {
        return name;
    }
    static const QRegularExpression numbered(QStringLiteral(R"(^(.*) \((\d+)\)$)"));
    const QRegularExpressionMatch match = numbered.match(name);
    const QString base = match.hasMatch() ? match.captured(1) : name;
    for(int n = 2;; n++) {
        const QString candidate = QString("%1 (%2)").arg(base).arg(n);
        if(!taken.contains(candidate)) {
            return candidate;
        }
    }
}

bool canRepresentElement(int z, int a) {
    return a > 0 && Elements::abundance(z, a) > 0;
}

QString cutLabel(int z, int a, CutType type, bool superscript) {
    const QString label = superscript ? Elements::displayLabel(z, a) : Elements::label(z, a);
    return type == CutType::Scattered ? label + QStringLiteral("*") : label;
}

int Measurement::newCutId() const {
    int id = 0;
    for(const Cut &cut : cuts) {
        id = std::max(id, cut.id);
    }
    return id + 1;
}

void Measurement::assignCutIds() {
    QSet<int> used;
    int next = newCutId();
    for(Cut &cut : cuts) {
        if(cut.id <= 0 || used.contains(cut.id)) {
            cut.id = next++;
        }
        used.insert(cut.id);
    }
}

const Cut *Measurement::cut(int id) const {
    for(const Cut &cut : cuts) {
        if(cut.id == id) {
            return &cut;
        }
    }
    return nullptr;
}

void Measurement::removeCut(int index) {
    const int id = cuts.at(index).id;
    cuts.removeAt(index);
    for(const auto &spectrum : spectra) {
        spectrum->cutIds.removeAll(id);
    }
    for(const auto &profile : depthProfiles) {
        profile->cutIds.removeAll(id);
        profile->cutIdsExcludedFromAverage.removeAll(id);
        profile->cutIdsHiddenFromPlot.removeAll(id);
    }
}

QString Measurement::uniqueDepthProfileName(const QString &name) const {
    QStringList names;
    for(const auto &profile : depthProfiles) {
        names.append(profile->name);
    }
    return uniqueName(name, names);
}

QString Measurement::uniqueSpectrumName(const QString &name) const {
    QStringList names;
    for(const auto &spectrum : spectra) {
        names.append(spectrum->name);
    }
    return uniqueName(name, names);
}

std::unique_ptr<Measurement> copyMeasurement(const Measurement &measurement) {
    auto copy = std::make_unique<Measurement>(measurement.name, measurement.description, measurement.path, measurement.cuts,
                                              measurement.settings, measurement.detector, measurement.physics);
    copy->sha256 = measurement.sha256;
    for(const auto &spectrum : measurement.spectra) {
        copy->spectra.push_back(std::make_unique<EnergySpectrum>(*spectrum));
    }
    for(const auto &profile : measurement.depthProfiles) {
        copy->depthProfiles.push_back(std::make_unique<DepthProfile>(*profile));
    }
    return copy;
}

QJsonArray cutsToJson(const QList<Cut> &cuts) {
    QJsonArray result;
    for(const auto &cut : cuts) {
        QJsonArray polygon;
        for(const QPointF &point : cut.polygon) {
            polygon.append(QJsonArray{point.x(), point.y()});
        }
        QJsonObject object{{"id", cut.id}, {"z", cut.z}, {"a", cut.a},
                           {"type", cut.type == CutType::Scattered ? ScatteredTypeName : RecoilTypeName},
                           {"color", cut.color.name()}, {"polygon", polygon}};
        if(cut.representsElement) {
            object["representsElement"] = true;
        }
        if(cut.tofEdge) {
            object["tofEdge"] = QJsonObject{{"channel", cut.tofEdge->channel}, {"sigma", cut.tofEdge->sigma},
                                            {"manual", cut.tofEdge->manual}};
        }
        result.append(object);
    }
    return result;
}

QList<Cut> cutsFromJson(const QJsonArray &json) {
    QList<Cut> cuts;
    for(const auto &value : json) {
        const QJsonObject object = value.toObject();
        Cut cut{object["id"].toInt(), object["z"].toInt(), object["a"].toInt(), {}, QColor::fromString(object["color"].toString())};
        cut.type = object["type"].toString() == ScatteredTypeName ? CutType::Scattered : CutType::Recoil; // Recoil by default
        if(!cut.color.isValid()) {
            cut.color = Defaults::elementColor(cut.z);
        }
        for(const auto &point : object["polygon"].toArray()) {
            const QJsonArray coordinates = point.toArray();
            cut.polygon.append({coordinates.at(0).toDouble(), coordinates.at(1).toDouble()});
        }
        cut.representsElement = object["representsElement"].toBool() && canRepresentElement(cut.z, cut.a);
        if(object.contains("tofEdge")) {
            const QJsonObject edge = object["tofEdge"].toObject();
            cut.tofEdge = ToFEdge{edge["channel"].toDouble(), edge["sigma"].toDouble(), edge["manual"].toBool()};
        }
        cuts.append(cut);
    }
    return cuts;
}

QJsonObject settingsToJson(const ExperimentalSettings &settings) {
    return QJsonObject{
            {"name", settings.name},
            {"beamEnergy", settings.beamEnergy},
            {"beam", QJsonObject{{"z", settings.beamZ}, {"a", settings.beamA}}},
            {"incidentAngle", settings.incidentAngle}};
}

bool isValidMaterial(const Material &material) {
    if(material.elements.isEmpty()) {
        return false;
    }
    double sum = 0;
    for(const MaterialElement &element : material.elements) {
        if(!Elements::isKnown(element.z) || element.fraction < 0) {
            return false;
        }
        sum += element.fraction;
    }
    return std::abs(sum - 1) <= MaterialFractionTolerance;
}

double averageAtomMass(const Material &material) {
    double mass = 0;
    for(const MaterialElement &element : material.elements) {
        mass += element.fraction * Elements::mass(element.z, 0);
    }
    return mass;
}

QString materialText(const Material &material) {
    if(material.elements.size() == 1) {
        return Elements::symbol(material.elements.first().z);
    }
    QStringList parts;
    for(const MaterialElement &element : material.elements) {
        parts.append(QString("%1 %2 %").arg(Elements::symbol(element.z)).arg(element.fraction * 100, 0, 'g', 4));
    }
    return parts.join(QStringLiteral(", "));
}

Material carbon() {
    return Material{{{6, 1.0}}};
}

double efficiency(const EfficiencyCurve &curve, double energy) {
    const QList<QPointF> &points = curve.points;
    if(points.isEmpty() || energy < points.first().x() || energy > points.last().x()) {
        return 0;
    }
    const auto above = std::upper_bound(points.begin(), points.end(), energy,
                                        [](double e, const QPointF &point) { return e < point.x(); });
    if(above == points.end()) {
        return points.last().y();
    }
    const QPointF &high = *above;
    const QPointF &low = *(above - 1);
    return low.y() + (energy - low.x()) / (high.x() - low.x()) * (high.y() - low.y());
}

const EfficiencyCurve *findEfficiency(const QList<EfficiencyCurve> &curves, int z, int a) {
    if(a <= 0) {
        double abundance = -1;
        for(const Elements::Isotope &isotope : Elements::isotopes(z)) {
            if(isotope.abundance > abundance) {
                abundance = isotope.abundance;
                a = isotope.a;
            }
        }
    }
    for(const EfficiencyCurve &curve : curves) {
        if(curve.z == z && curve.a == a) {
            return &curve;
        }
    }
    return nullptr;
}

QJsonObject detectorToJson(const DetectorSettings &detector) {
    QJsonArray material;
    for(const MaterialElement &element : detector.foilMaterial.elements) {
        material.append(QJsonObject{{"z", element.z}, {"fraction", element.fraction}});
    }
    QJsonArray efficiencies;
    for(const EfficiencyCurve &curve : detector.efficiencies) {
        QJsonArray points;
        for(const QPointF &point : curve.points) {
            points.append(QJsonArray{point.x(), point.y()});
        }
        efficiencies.append(QJsonObject{{"z", curve.z}, {"a", curve.a}, {"points", points}});
    }
    return QJsonObject{
            {"name", detector.name},
            {"scatteringAngle", detector.scatteringAngle},
            {"tofLength", detector.tofLength},
            {"tofSlope", detector.tofSlope},
            {"tofOffset", detector.tofOffset},
            {"foil", QJsonObject{{"thickness", detector.foilThickness}, {"material", material}}},
            {"efficiencies", efficiencies}};
}

DetectorSettings detectorFromJson(const QJsonObject &json) {
    DetectorSettings detector;
    detector.name = json["name"].toString();
    detector.scatteringAngle = std::clamp(json["scatteringAngle"].toDouble(MinScatteringAngle), MinScatteringAngle, MaxScatteringAngle);
    detector.tofLength = json["tofLength"].toDouble();
    detector.tofSlope = std::max(0.0, json["tofSlope"].toDouble());
    detector.tofOffset = json["tofOffset"].toDouble();
    if(json.contains("foil")) {
        const QJsonObject foil = json["foil"].toObject();
        detector.foilThickness = std::max(0.0, foil["thickness"].toDouble());
        Material material;
        for(const auto &value : foil["material"].toArray()) {
            const QJsonObject element = value.toObject();
            material.elements.append({element["z"].toInt(), element["fraction"].toDouble()});
        }
        detector.foilMaterial = isValidMaterial(material) ? material : carbon(); // Carbon by default
    }
    for(const auto &value : json["efficiencies"].toArray()) {
        const QJsonObject object = value.toObject();
        EfficiencyCurve curve{object["z"].toInt(), object["a"].toInt()};
        for(const auto &point : object["points"].toArray()) {
            const QJsonArray coordinates = point.toArray();
            curve.points.append({coordinates.at(0).toDouble(), coordinates.at(1).toDouble()});
        }
        std::sort(curve.points.begin(), curve.points.end(), [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
        if(Elements::hasIsotope(curve.z, curve.a) && !curve.points.isEmpty() && !detector.efficiency(curve.z, curve.a)) {
            detector.efficiencies.append(curve);
        }
    }
    return detector;
}

QString legendPositionName(LegendPosition position) {
    switch(position) {
        case LegendPosition::Default:
            return QStringLiteral("Default");
        case LegendPosition::TopRight:
            return QStringLiteral("Top Right");
        case LegendPosition::TopLeft:
            return QStringLiteral("Top Left");
    }
    return {};
}

QString legendPositionId(LegendPosition position) {
    switch(position) {
        case LegendPosition::Default:
            return QStringLiteral("default");
        case LegendPosition::TopRight:
            return QStringLiteral("topRight");
        case LegendPosition::TopLeft:
            return QStringLiteral("topLeft");
    }
    return {};
}

LegendPosition legendPositionFromId(const QString &id, LegendPosition fallback) {
    for(const LegendPosition position : {LegendPosition::Default, LegendPosition::TopRight, LegendPosition::TopLeft}) {
        if(id == legendPositionId(position)) {
            return position;
        }
    }
    return fallback;
}

QString screeningModelName(ScreeningModel model) {
    return model == ScreeningModel::None ? QStringLiteral("None (Rutherford)") : QStringLiteral("Andersen");
}

const StoppingCorrection *PhysicsSettings::correction(int z1, int z2) const {
    for(const StoppingCorrection &c : corrections) {
        if(c.z1 == z1 && c.z2 == z2) {
            return &c;
        }
    }
    return nullptr;
}

const StoppingCorrection *PhysicsSettings::correctionFor(int z1, int z2) const {
    const StoppingCorrection *own = correction(z1, z2);
    return own ? own : correction(z1, AnyElement);
}

const PhysicsSettings &Measurement::physicsOrDefault() const {
    static const PhysicsSettings defaults;
    return physics ? *physics : defaults;
}

QJsonObject physicsToJson(const PhysicsSettings &physics) {
    QJsonArray corrections;
    for(const StoppingCorrection &c : physics.corrections) {
        if(c.z1 <= 0 || c.z2 == 0) {
            continue; // Still being edited
        }
        corrections.append(QJsonObject{{"z1", c.z1}, {"z2", c.z2}, {"electronicFactor", c.electronicFactor},
                                       {"nuclearFactor", c.nuclearFactor}, {"stoppingFile", c.stoppingFile}});
    }
    return QJsonObject{
            {"name", physics.name},
            {"screening", screeningModelId(physics.screening)},
            {"electronicFactor", physics.electronicFactor},
            {"nuclearFactor", physics.nuclearFactor},
            {"stoppingFile", physics.stoppingFile},
            {"corrections", corrections}};
}

PhysicsSettings physicsFromJson(const QJsonObject &json) {
    PhysicsSettings physics;
    physics.name = json["name"].toString();
    physics.screening = json["screening"].toString() == screeningModelId(ScreeningModel::None) ? ScreeningModel::None
                                                                                               : ScreeningModel::Andersen;
    physics.electronicFactor = std::max(0.0, json["electronicFactor"].toDouble(1));
    physics.nuclearFactor = std::max(0.0, json["nuclearFactor"].toDouble(1));
    physics.stoppingFile = json["stoppingFile"].toString();
    for(const auto &value : json["corrections"].toArray()) {
        const QJsonObject object = value.toObject();
        StoppingCorrection c{object["z1"].toInt(), object["z2"].toInt(), std::max(0.0, object["electronicFactor"].toDouble(1)),
                             std::max(0.0, object["nuclearFactor"].toDouble(1)), object["stoppingFile"].toString()};
        if(Elements::isKnown(c.z1) && (Elements::isKnown(c.z2) || c.z2 == AnyElement) && !physics.correction(c.z1, c.z2)) {
            physics.corrections.append(c);
        }
    }
    return physics;
}

ExperimentalSettings settingsFromJson(const QJsonObject &json) {
    ExperimentalSettings settings;
    settings.name = json["name"].toString();
    settings.beamEnergy = json["beamEnergy"].toDouble();
    settings.beamZ = json["beam"]["z"].toInt();
    settings.beamA = json["beam"]["a"].toInt();
    settings.incidentAngle = std::clamp(json["incidentAngle"].toDouble(), MinIncidentAngle, MaxIncidentAngle);
    return settings;
}

QByteArray settingsCollectionToJson(const SettingsCollection &collection) {
    QJsonArray settings;
    for(const auto &s : collection.experimental) {
        settings.append(settingsToJson(s));
    }
    QJsonArray detectors;
    for(const auto &detector : collection.detectors) {
        detectors.append(detectorToJson(detector));
    }
    QJsonArray physics;
    for(const auto &p : collection.physics) {
        physics.append(physicsToJson(p));
    }
    const QJsonObject root{
            {"format", SettingsFormatName},
            {"version", FormatVersion},
            {"experimentalSettings", settings},
            {"detectorSettings", detectors},
            {"physicsSettings", physics}};
    return QJsonDocument(root).toJson();
}

bool settingsCollectionFromJson(const QByteArray &data, SettingsCollection &collection, QString &errorMessage) {
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if(document.isNull()) {
        errorMessage = parseError.errorString();
        return false;
    }
    const QJsonObject root = document.object();
    const QString format = root["format"].toString();
    if(format != SettingsFormatName && format != FormatName) {
        errorMessage = QStringLiteral("Not a JaRA settings or project file.");
        return false;
    }
    const int version = root["version"].toInt();
    if(version != FormatVersion) {
        errorMessage = QString("Unsupported file version %1.").arg(version);
        return false;
    }
    collection = {};
    collection.fromProject = format == FormatName;
    for(const auto &value : root["experimentalSettings"].toArray()) {
        collection.experimental.append(settingsFromJson(value.toObject()));
    }
    for(const auto &value : root["detectorSettings"].toArray()) {
        collection.detectors.append(detectorFromJson(value.toObject()));
    }
    for(const auto &value : root["physicsSettings"].toArray()) {
        collection.physics.append(physicsFromJson(value.toObject()));
    }
    return true;
}

std::unique_ptr<Project> Project::load(const QString &fileName, QString &errorMessage) {
    QFile file(fileName);
    if(!file.open(QIODevice::ReadOnly)) {
        errorMessage = file.errorString();
        return nullptr;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if(document.isNull()) {
        errorMessage = parseError.errorString();
        return nullptr;
    }
    const QJsonObject root = document.object();
    if(root["format"].toString() != FormatName) {
        errorMessage = QStringLiteral("Not a JaRA project file.");
        return nullptr;
    }
    const int version = root["version"].toInt();
    if(version != FormatVersion) {
        errorMessage = QString("Unsupported project file version %1.").arg(version);
        return nullptr;
    }

    auto project = std::make_unique<Project>();
    const QFileInfo fileInfo(fileName);
    const QDir baseDir = fileInfo.absoluteDir();
    for(const auto &value : root["experimentalSettings"].toArray()) {
        project->m_settings.push_back(std::make_unique<ExperimentalSettings>(settingsFromJson(value.toObject())));
    }
    for(const auto &value : root["detectorSettings"].toArray()) {
        project->m_detectors.push_back(std::make_unique<DetectorSettings>(detectorFromJson(value.toObject())));
    }
    for(const auto &value : root["physicsSettings"].toArray()) {
        project->m_physics.push_back(std::make_unique<PhysicsSettings>(physicsFromJson(value.toObject())));
    }
    for(const auto &value : root["samples"].toArray()) {
        project->m_samples.push_back(sampleFromJson(value.toObject(), baseDir, *project));
    }
    project->m_created = timeFromJson(root["created"]);
    project->m_saved = timeFromJson(root["saved"]);
    project->m_fileName = fileInfo.absoluteFilePath();
    project->sortByName();
    return project;
}

QString fileChecksum(const QString &path) {
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if(!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) {
        return {};
    }
    return QString::fromLatin1(hash.result().toHex());
}

QStringList Project::referencedFiles() const {
    QStringList files;
    forEachMeasurement([&files](const Measurement *measurement) {
        if(!measurement->path.isEmpty() && !files.contains(measurement->path)) {
            files.append(measurement->path);
        }
    });
    for(const auto &sample : m_samples) {
        for(const AttachedFile &file : sample->files) {
            if(!files.contains(file.path)) {
                files.append(file.path);
            }
        }
    }
    return files;
}

QList<Project::FileProblem> Project::verifyFiles() {
    QHash<QString, QString> current; // Checksums of the files now
    for(const QString &path : referencedFiles()) {
        current[path] = fileChecksum(path);
    }
    QList<FileProblem> problems;
    auto verify = [&](const QString &path, QString &sha256) {
        const QString &now = current[path];
        const bool known = std::any_of(problems.begin(), problems.end(), [&path](const FileProblem &p) { return p.path == path; });
        if(now.isEmpty()) {
            if(!known) {
                problems.append({path, true}); // The checksum is kept for when the file is found again
            }
            return;
        }
        if(sha256.isEmpty()) {
            sha256 = now;
        } else if(sha256 != now && !known) {
            problems.append({path, false, now});
        }
    };
    for(const auto &sample : m_samples) {
        for(const auto &measurement : sample->measurements) {
            if(!measurement->path.isEmpty()) {
                verify(measurement->path, measurement->sha256);
            }
        }
        for(AttachedFile &file : sample->files) {
            verify(file.path, file.sha256);
        }
    }
    return problems;
}

void Project::acceptChangedFiles(const QList<FileProblem> &problems) {
    auto accept = [&problems, this](const QString &path, QString &sha256) {
        for(const FileProblem &problem : problems) {
            if(problem.path == path && !problem.missing) {
                sha256 = problem.sha256;
                m_modified = true;
            }
        }
    };
    for(const auto &sample : m_samples) {
        for(const auto &measurement : sample->measurements) {
            accept(measurement->path, measurement->sha256);
        }
        for(AttachedFile &file : sample->files) {
            accept(file.path, file.sha256);
        }
    }
}

bool Project::save(const QString &fileName, QString &errorMessage) {
    const QFileInfo fileInfo(fileName);
    const QDir baseDir = fileInfo.absoluteDir();
    const QDateTime previousSave = m_saved;
    m_saved = QDateTime::currentDateTime();
    const QByteArray json = toJson([&baseDir](const QString &path) { return pathToJson(path, baseDir); });
    QSaveFile file(fileName);
    if(!file.open(QIODevice::WriteOnly)) {
        errorMessage = file.errorString();
        m_saved = previousSave;
        return false;
    }
    file.write(json);
    if(!file.commit()) {
        errorMessage = file.errorString();
        m_saved = previousSave;
        return false;
    }
    m_fileName = fileInfo.absoluteFilePath();
    m_modified = false;
    return true;
}

QByteArray Project::toJson(const std::function<QString(const QString &path)> &storedPath) {
    QHash<QString, QString> checksums;
    for(const auto &sample : m_samples) {
        for(const auto &measurement : sample->measurements) {
            fillChecksum(measurement->path, measurement->sha256, checksums);
        }
        for(AttachedFile &file : sample->files) {
            fillChecksum(file.path, file.sha256, checksums);
        }
    }
    QJsonArray settings;
    for(const auto &s : m_settings) {
        settings.append(settingsToJson(*s));
    }
    QJsonArray detectors;
    for(const auto &detector : m_detectors) {
        detectors.append(detectorToJson(*detector));
    }
    QJsonArray physics;
    for(const auto &p : m_physics) {
        physics.append(physicsToJson(*p));
    }
    QJsonArray samples;
    for(const auto &sample : m_samples) {
        samples.append(sampleToJson(*sample, storedPath, *this));
    }
    QJsonObject root{
            {"format", FormatName},
            {"version", FormatVersion},
            {"experimentalSettings", settings},
            {"detectorSettings", detectors},
            {"physicsSettings", physics},
            {"samples", samples}};
    // Unknown for projects saved before the times were stored
    if(m_created.isValid()) {
        root["created"] = timeToJson(m_created);
    }
    if(m_saved.isValid()) {
        root["saved"] = timeToJson(m_saved);
    }
    return QJsonDocument(root).toJson();
}

bool Project::exportArchive(const QString &archiveName, QString &errorMessage) {
    // Everything goes in a directory named after the project, measurements in data and attached files in files. Files
    // with the same name are numbered. Files that cannot be read are left out and keep their paths.
    const QString name = m_fileName.isEmpty() ? QStringLiteral("project") : QFileInfo(m_fileName).completeBaseName();
    QStringList measurementPaths;
    forEachMeasurement([&measurementPaths](const Measurement *measurement) { measurementPaths.append(measurement->path); });
    QHash<QString, QString> archived; // Path in the archive relative to the project file, by the path now
    QSet<QString> taken; // In lower case, the archive may be extracted on a file system that ignores case
    for(const QString &path : referencedFiles()) {
        const QFileInfo info(path);
        if(!info.isReadable() || !info.isFile()) {
            continue;
        }
        const QString directory = measurementPaths.contains(path) ? QStringLiteral("data/") : QStringLiteral("files/");
        QString candidate = directory + info.fileName();
        for(int n = 2; taken.contains(candidate.toLower()); n++) {
            const QString suffix = info.completeSuffix().isEmpty() ? QString() : "." + info.completeSuffix();
            candidate = QString("%1%2 (%3)%4").arg(directory, info.baseName()).arg(n).arg(suffix);
        }
        taken.insert(candidate.toLower());
        archived[path] = candidate;
    }

    const QByteArray json = toJson([&archived](const QString &path) { return archived.value(path, path); });
    ArchiveWriter writer(archiveName, ArchiveWriter::formatOf(archiveName));
    bool written = writer.open() && writer.addData(name + "/" + name + ".json", json, QDateTime::currentDateTime());
    for(const QString &path : referencedFiles()) {
        if(written && archived.contains(path)) {
            written = writer.addFile(name + "/" + archived[path], path);
        }
    }
    if(!written || !writer.close()) {
        errorMessage = writer.errorString();
        return false;
    }
    return true;
}

ExperimentalSettings *Project::addSettings(const ExperimentalSettings &settings) {
    m_settings.push_back(std::make_unique<ExperimentalSettings>(settings));
    m_modified = true;
    return m_settings.back().get();
}

void Project::removeSettings(size_t index) {
    const ExperimentalSettings *settings = m_settings.at(index).get();
    for(Measurement *measurement : measurementsUsing(settings)) {
        measurement->settings = nullptr;
    }
    m_settings.erase(m_settings.begin() + static_cast<std::ptrdiff_t>(index));
    m_modified = true;
}

QString Project::uniqueSettingsName(const QString &name) const {
    QStringList names;
    for(const auto &settings : m_settings) {
        names.append(settings->name);
    }
    return uniqueName(name, names);
}

QString Project::uniqueDetectorName(const QString &name) const {
    QStringList names;
    for(const auto &detector : m_detectors) {
        names.append(detector->name);
    }
    return uniqueName(name, names);
}

int Project::settingsIndex(const ExperimentalSettings *settings) const {
    return indexOf(m_settings, settings);
}

QList<Measurement *> Project::measurementsUsing(const ExperimentalSettings *settings) const {
    QList<Measurement *> result;
    forEachMeasurement([&](Measurement *measurement) {
        if(settings && measurement->settings == settings) {
            result.append(measurement);
        }
    });
    return result;
}

DetectorSettings *Project::addDetector(const DetectorSettings &detector) {
    m_detectors.push_back(std::make_unique<DetectorSettings>(detector));
    m_modified = true;
    return m_detectors.back().get();
}

void Project::removeDetector(size_t index) {
    for(Measurement *measurement : measurementsUsing(m_detectors.at(index).get())) {
        measurement->detector = nullptr;
    }
    m_detectors.erase(m_detectors.begin() + static_cast<std::ptrdiff_t>(index));
    m_modified = true;
}

int Project::detectorIndex(const DetectorSettings *detector) const {
    return indexOf(m_detectors, detector);
}

QList<Measurement *> Project::measurementsUsing(const DetectorSettings *detector) const {
    QList<Measurement *> result;
    forEachMeasurement([&](Measurement *measurement) {
        if(detector && measurement->detector == detector) {
            result.append(measurement);
        }
    });
    return result;
}

PhysicsSettings *Project::addPhysics(const PhysicsSettings &physics) {
    m_physics.push_back(std::make_unique<PhysicsSettings>(physics));
    m_modified = true;
    return m_physics.back().get();
}

QString Project::uniquePhysicsName(const QString &name) const {
    QStringList names;
    for(const auto &physics : m_physics) {
        names.append(physics->name);
    }
    return uniqueName(name, names);
}

void Project::removePhysics(size_t index) {
    for(Measurement *measurement : measurementsUsing(m_physics.at(index).get())) {
        measurement->physics = nullptr;
    }
    m_physics.erase(m_physics.begin() + static_cast<std::ptrdiff_t>(index));
    m_modified = true;
}

int Project::physicsIndex(const PhysicsSettings *physics) const {
    return indexOf(m_physics, physics);
}

QList<Measurement *> Project::measurementsUsing(const PhysicsSettings *physics) const {
    QList<Measurement *> result;
    forEachMeasurement([&](Measurement *measurement) {
        if(physics && measurement->physics == physics) {
            result.append(measurement);
        }
    });
    return result;
}

Sample *Project::addSample(const QString &name) {
    auto sample = std::make_unique<Sample>();
    sample->name = name;
    m_samples.push_back(std::move(sample));
    m_modified = true;
    return m_samples.back().get();
}

Sample *Project::addSample(std::unique_ptr<Sample> sample) {
    m_samples.push_back(std::move(sample));
    m_modified = true;
    return m_samples.back().get();
}

QString Project::uniqueSampleName(const QString &name) const {
    QStringList names;
    for(const auto &sample : m_samples) {
        names.append(sample->name);
    }
    return uniqueName(name, names);
}

void Project::sortByName() {
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    auto byName = [&collator](const auto &a, const auto &b) { return collator.compare(a->name, b->name) < 0; };
    std::stable_sort(m_samples.begin(), m_samples.end(), byName);
    for(const auto &sample : m_samples) {
        std::stable_sort(sample->measurements.begin(), sample->measurements.end(), byName);
    }
}

void Project::removeSample(size_t index) {
    m_samples.erase(m_samples.begin() + static_cast<std::ptrdiff_t>(index));
    m_modified = true;
}
