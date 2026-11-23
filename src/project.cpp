/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

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
const QString ReferenceTypeName = QStringLiteral("reference");

QString cutTypeId(CutType type) { // In files
    switch(type) {
    case CutType::Recoil:
        return RecoilTypeName;
    case CutType::Scattered:
        return ScatteredTypeName;
    case CutType::Reference:
        return ReferenceTypeName;
    }
    return RecoilTypeName;
}

CutType cutTypeFromId(const QString &id) { // Recoil by default
    if(id == ScatteredTypeName) {
        return CutType::Scattered;
    }
    return id == ReferenceTypeName ? CutType::Reference : CutType::Recoil;
}

// The range is only stored when it is not the whole measurement
void rangeToJson(QJsonObject &json, double min, double max) {
    if(min > 0 || max < 1) {
        json["range"] = QJsonArray{min, max};
    }
}

void rangeFromJson(const QJsonObject &json, double &min, double &max) {
    const QJsonArray range = json["range"].toArray();
    if(range.size() != 2) {
        return;
    }
    const double first = std::clamp(range.at(0).toDouble(0), 0.0, 1.0);
    const double last = std::clamp(range.at(1).toDouble(1), 0.0, 1.0);
    if(first < last) {
        min = first;
        max = last;
    }
}

QString screeningModelId(ScreeningModel model) { // In files
    return model == ScreeningModel::None ? QStringLiteral("none") : QStringLiteral("andersen");
}

QString nuclearStoppingMassId(NuclearStoppingMass mass) { // In files
    return mass == NuclearStoppingMass::Isotopes ? QStringLiteral("isotopes") : QStringLiteral("average");
}

QString stoppingInterpolationId(StoppingInterpolation interpolation) { // In files
    return interpolation == StoppingInterpolation::Cubic ? QStringLiteral("cubic") : QStringLiteral("linear");
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

// In the order of the ids, so that saving again gives the same file
QJsonArray labelPositionsToJson(const QHash<int, QPointF> &positions) {
    QList<int> ids = positions.keys();
    std::sort(ids.begin(), ids.end());
    QJsonArray json;
    for(const int id : ids) {
        const QPointF position = positions.value(id);
        json.append(QJsonObject{{"cut", id}, {"depth", position.x()}, {"concentration", position.y()}});
    }
    return json;
}

const QString ReportNameModeIds[] = {QStringLiteral("auto"), QStringLiteral("on"), QStringLiteral("off")};
const QString ReportRangeModeIds[] = {QStringLiteral("gui"), QStringLiteral("common"), QStringLiteral("perProfile")};

QJsonObject reportSettingsToJson(const ReportSettings &settings) {
    return QJsonObject{{"ion", settings.ion}, {"energy", settings.energy}, {"incidentAngle", settings.incidentAngle},
                       {"scatteringAngle", settings.scatteringAngle}, {"tofLength", settings.tofLength},
                       {"foil", settings.foil}, {"tofCalibration", settings.tofCalibration},
                       {"angleCalibration", settings.angleCalibration},
                       {"efficiencies", settings.efficiencies}, {"physics", settings.physics},
                       {"profileParameters", settings.profileParameters}};
}

ReportSettings reportSettingsFromJson(const QJsonObject &json) {
    ReportSettings settings;
    auto read = [&json](const char *key, bool &value) { value = json[key].toBool(value); };
    read("ion", settings.ion);
    read("energy", settings.energy);
    read("incidentAngle", settings.incidentAngle);
    read("scatteringAngle", settings.scatteringAngle);
    read("tofLength", settings.tofLength);
    read("foil", settings.foil);
    read("tofCalibration", settings.tofCalibration);
    read("angleCalibration", settings.angleCalibration);
    read("efficiencies", settings.efficiencies);
    read("physics", settings.physics);
    read("profileParameters", settings.profileParameters);
    return settings;
}

QJsonObject depthProfileToJson(const DepthProfile &profile) {
    QJsonObject normalization{{"auto", profile.autoNormalization},
                              {"min", profile.normalizationMin},
                              {"max", profile.normalizationMax}};
    QJsonObject json{
            {"name", profile.name},
            {"cuts", idsToJson(profile.cutIds)},
            {"binWidth", profile.binWidth},
            {"stoppingBinWidth", profile.stoppingBinWidth},
            {"maxDepth", profile.maxDepth},
            {"minStoppingBinEvents", profile.minStoppingBinEvents},
            {"normalization", normalization},
            {"average", QJsonObject{{"min", profile.averageMin}, {"max", profile.averageMax},
                                    {"followsNormalization", profile.averageFollowsNormalization},
                                    {"excludedCuts", idsToJson(profile.cutIdsExcludedFromAverage)}}},
            {"density", profile.density},
            {"separateIsotopes", profile.separateIsotopes},
            {"kinematicCorrection", profile.kinematicCorrection},
            {"autoMaxDepth", profile.autoMaxDepth},
            {"plot", QJsonObject{{"hiddenCuts", idsToJson(profile.cutIdsHiddenFromPlot)},
                                 {"total", profile.showTotal},
                                 {"normalization", profile.showNormalization},
                                 {"shadeAverage", profile.shadeAverage},
                                 {"labels", profile.showLabels},
                                 {"unlabeledCuts", idsToJson(profile.cutIdsWithoutLabel)},
                                 {"labelPositions", labelPositionsToJson(profile.labelPositions)},
                                 {"legend", legendPositionId(profile.legendPosition)},
                                 {"legendConcentrations", profile.showLegendConcentrations},
                                 {"roundLegendConcentrations", profile.roundLegendConcentrations}}},
            {"roundTableConcentrations", profile.roundTableConcentrations}};
    rangeToJson(json, profile.rangeMin, profile.rangeMax);
    if(profile.view) {
        QJsonObject plot = json["plot"].toObject();
        plot["view"] = plotViewToJson(*profile.view);
        json["plot"] = plot;
    }
    if(!profile.cutRanges.isEmpty()) {
        QList<int> ids = profile.cutRanges.keys();
        std::sort(ids.begin(), ids.end()); // Saving again gives the same file
        QJsonArray cutRanges;
        for(const int id : ids) {
            const auto [min, max] = profile.cutRanges.value(id);
            cutRanges.append(QJsonObject{{"cut", id}, {"min", min}, {"max", max}});
        }
        json["cutRanges"] = cutRanges;
    }
    return json;
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
    profile->averageFollowsNormalization = average["followsNormalization"].toBool(profile->averageFollowsNormalization);
    profile->cutIdsExcludedFromAverage = idsFromJson(average["excludedCuts"]);
    profile->autoMaxDepth = json["autoMaxDepth"].toBool(profile->autoMaxDepth);
    profile->separateIsotopes = json["separateIsotopes"].toBool(profile->separateIsotopes);
    profile->kinematicCorrection = json["kinematicCorrection"].toBool(profile->kinematicCorrection);
    const QJsonObject plot = json["plot"].toObject();
    profile->cutIdsHiddenFromPlot = idsFromJson(plot["hiddenCuts"]);
    profile->showTotal = plot["total"].toBool(profile->showTotal);
    profile->showNormalization = plot["normalization"].toBool(profile->showNormalization);
    profile->shadeAverage = plot["shadeAverage"].toBool(profile->shadeAverage);
    if(plot.contains("view")) {
        profile->view = plotViewFromJson(plot["view"].toObject());
    }
    profile->showLabels = plot["labels"].toBool(profile->showLabels);
    profile->cutIdsWithoutLabel = idsFromJson(plot["unlabeledCuts"]);
    for(const auto &value : plot["labelPositions"].toArray()) {
        const QJsonObject position = value.toObject();
        profile->labelPositions.insert(position["cut"].toInt(),
                                       QPointF(position["depth"].toDouble(), position["concentration"].toDouble()));
    }
    profile->legendPosition = legendPositionFromId(plot["legend"].toString(), profile->legendPosition);
    profile->showLegendConcentrations = plot["legendConcentrations"].toBool(profile->showLegendConcentrations);
    profile->roundLegendConcentrations = plot["roundLegendConcentrations"].toBool(profile->roundLegendConcentrations);
    profile->roundTableConcentrations = json["roundTableConcentrations"].toBool(profile->roundTableConcentrations);
    rangeFromJson(json, profile->rangeMin, profile->rangeMax);
    for(const auto &value : json["cutRanges"].toArray()) {
        const QJsonObject range = value.toObject();
        const double min = range["min"].toDouble(0);
        const double max = range["max"].toDouble(1);
        if(min >= 0 && min < max && max <= 1) {
            profile->cutRanges.insert(range["cut"].toInt(), {min, max});
        }
    }
    return profile;
}

QJsonArray attachedFilesToJson(const QList<AttachedFile> &files, const std::function<QString(const QString &)> &storedPath) {
    QJsonArray json;
    for(const auto &file : files) {
        json.append(QJsonObject{{"path", storedPath(file.path)}, {"description", file.description}, {"sha256", file.sha256}});
    }
    return json;
}

QList<AttachedFile> attachedFilesFromJson(const QJsonArray &json, const QDir &baseDir) {
    QList<AttachedFile> files;
    for(const auto &value : json) {
        const QJsonObject file = value.toObject();
        const QString path = pathFromJson(file["path"], baseDir);
        if(!path.isEmpty()) {
            files.append({path, file["description"].toString(), file["sha256"].toString()});
        }
    }
    return files;
}

QJsonObject sampleToJson(const Sample &sample, const std::function<QString(const QString &)> &storedPath,
                         const Project &project) {
    QJsonArray metadata;
    for(const auto &entry : sample.metadata) {
        metadata.append(QJsonObject{{"key", entry.key}, {"value", entry.value}});
    }
    const QJsonArray files = attachedFilesToJson(sample.files, storedPath);
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
            QJsonObject spectrumObject{{"name", spectrum->name}, {"binWidth", spectrum->binWidth}, {"cuts", cutIds},
                                       {"legend", legendPositionId(spectrum->legendPosition)}};
            rangeToJson(spectrumObject, spectrum->rangeMin, spectrum->rangeMax);
            spectra.append(spectrumObject);
        }
        object["spectra"] = spectra;
        QJsonArray splits;
        for(const auto &split : measurement->splitAnalyses) {
            splits.append(QJsonObject{{"name", split->name}, {"parts", split->parts}, {"cuts", idsToJson(split->cutIds)},
                                      {"relative", split->relative}, {"legend", legendPositionId(split->legendPosition)}});
        }
        object["splitAnalyses"] = splits;
        if(measurement->referenceCutId > 0) {
            object["referenceCut"] = measurement->referenceCutId;
        }
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
    sample->files = attachedFilesFromJson(json["files"].toArray(), baseDir);
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
            rangeFromJson(object, spectrum->rangeMin, spectrum->rangeMax);
            spectrum->legendPosition = legendPositionFromId(object["legend"].toString(), spectrum->legendPosition);
            sample->measurements.back()->spectra.push_back(std::move(spectrum));
        }
        for(const auto &splitValue : measurement["splitAnalyses"].toArray()) {
            const QJsonObject object = splitValue.toObject();
            auto split = std::make_unique<SplitAnalysis>();
            split->name = object["name"].toString();
            split->parts = std::clamp(object["parts"].toInt(DefaultSplitParts), MinSplitParts, MaxSplitParts);
            split->cutIds = idsFromJson(object["cuts"]);
            split->relative = object["relative"].toBool();
            split->legendPosition = legendPositionFromId(object["legend"].toString(), split->legendPosition);
            sample->measurements.back()->splitAnalyses.push_back(std::move(split));
        }
        const int referenceCutId = measurement["referenceCut"].toInt();
        if(sample->measurements.back()->cut(referenceCutId)) {
            sample->measurements.back()->referenceCutId = referenceCutId;
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

double cutAbundance(const Cut &cut) {
    return cut.abundance.value_or(Elements::abundance(cut.z, cut.a));
}

double abundanceCorrection(const Cut &cut) {
    return cut.representsElement ? 1 / cutAbundance(cut) : 1;
}

QJsonObject plotViewToJson(const PlotView &view) {
    return QJsonObject{{"depthMin", view.depthMin}, {"depthMax", view.depthMax},
                       {"concentrationMin", view.concentrationMin}, {"concentrationMax", view.concentrationMax},
                       {"log", view.logScale}};
}

PlotView plotViewFromJson(const QJsonObject &json) {
    PlotView view;
    view.depthMin = json["depthMin"].toDouble(view.depthMin);
    view.depthMax = json["depthMax"].toDouble(view.depthMax);
    view.concentrationMin = json["concentrationMin"].toDouble(view.concentrationMin);
    view.concentrationMax = json["concentrationMax"].toDouble(view.concentrationMax);
    view.logScale = json["log"].toBool(view.logScale);
    if(view.depthMax <= view.depthMin) {
        view.depthMax = view.depthMin + DefaultAverageMax;
    }
    if(view.concentrationMax <= view.concentrationMin) {
        view.concentrationMax = view.concentrationMin + 1;
    }
    return view;
}

namespace {
QString jsonFingerprint(const QJsonObject &json) {
    // The keys of JSON objects are sorted, so the same contents give the same text
    const QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}
}

QString depthProfileFingerprint(const Measurement &measurement, const DepthProfile &profile, bool withView) {
    DepthProfile copy = profile;
    if(!withView) {
        copy.view.reset();
    }
    QJsonObject json{{"profile", depthProfileToJson(copy)},
                     {"cuts", cutsToJson(measurement.cuts)},
                     {"physics", physicsToJson(measurement.physicsOrDefault())},
                     {"referenceCut", measurement.referenceCutId},
                     {"path", measurement.path},
                     {"sha256", measurement.sha256}};
    if(measurement.settings) {
        json["settings"] = settingsToJson(*measurement.settings);
    }
    if(measurement.detector) {
        json["detector"] = detectorToJson(*measurement.detector);
    }
    return jsonFingerprint(json);
}

QString reportOptionsFingerprint(const Report &report, const QString &projectName,
                                 const std::function<const Measurement *(const SplitAnalysis *)> &measurementOf) {
    // The included profiles have fingerprints of their own, their order is that of the project
    QJsonArray views;
    for(const DepthProfile *profile : report.profiles) {
        views.append(report.rangeMode == Report::RangeMode::PerProfile && report.profileViews.contains(profile)
                             ? QJsonValue(plotViewToJson(report.profileViews.value(profile)))
                             : QJsonValue());
    }
    // The split analyses and the parts of their measurements that they depend on
    QJsonArray splits;
    for(const SplitAnalysis *split : report.splits) {
        const Measurement *measurement = measurementOf(split);
        splits.append(QJsonObject{{"split", QJsonObject{{"name", split->name}, {"parts", split->parts},
                                                        {"cuts", idsToJson(split->cutIds)}, {"relative", split->relative}}},
                                  {"cuts", measurement ? cutsToJson(measurement->cuts) : QJsonArray()},
                                  {"referenceCut", measurement ? measurement->referenceCutId : 0},
                                  {"sha256", measurement ? measurement->sha256 : QString()}});
    }
    return jsonFingerprint(QJsonObject{{"project", projectName},
                                       {"title", report.title},
                                       {"measurementNames", static_cast<int>(report.measurementNames)},
                                       {"settings", reportSettingsToJson(report.settings)},
                                       {"range", static_cast<int>(report.rangeMode)},
                                       {"commonView", plotViewToJson(report.commonView)},
                                       {"views", views},
                                       {"splits", splits}});
}

bool isReportUpToDate(const Report &report, const QString &projectName,
                      const std::function<const Measurement *(const DepthProfile *)> &measurementOf,
                      const std::function<const Measurement *(const SplitAnalysis *)> &splitMeasurementOf) {
    if(!report.generated.isValid() || report.fingerprints.size() != report.profiles.size()
       || report.optionsFingerprint != reportOptionsFingerprint(report, projectName, splitMeasurementOf)) {
        return false;
    }
    const bool withView = report.rangeMode == Report::RangeMode::Gui;
    for(const DepthProfile *profile : report.profiles) {
        const Measurement *measurement = measurementOf(profile);
        if(!measurement || report.fingerprints.value(profile) != depthProfileFingerprint(*measurement, *profile, withView)) {
            return false;
        }
    }
    return true;
}

QString cutLabel(int z, int a, CutType type, bool superscript) {
    if(type == CutType::Reference && z <= 0) {
        return QStringLiteral("Ref");
    }
    const QString label = superscript ? Elements::displayLabel(z, a) : Elements::label(z, a);
    switch(type) {
    case CutType::Scattered:
        return label + QStringLiteral("*");
    case CutType::Reference:
        return label + QStringLiteral(" (ref)");
    default:
        return label;
    }
}

QString cutLabel(const Cut &cut, bool superscript) {
    if(cut.type == CutType::Reference && cut.z <= 0) {
        return QString("Ref %1").arg(cut.id);
    }
    return cutLabel(cut.z, cut.a, cut.type, superscript);
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

QList<QPointF> Measurement::referencePolygon() const {
    const Cut *reference = referenceCut();
    return reference ? reference->polygon : QList<QPointF>();
}

void Measurement::removeCut(int index) {
    const int id = cuts.at(index).id;
    cuts.removeAt(index);
    if(referenceCutId == id) {
        referenceCutId = 0;
    }
    for(const auto &split : splitAnalyses) {
        split->cutIds.removeAll(id);
    }
    for(const auto &spectrum : spectra) {
        spectrum->cutIds.removeAll(id);
    }
    for(const auto &profile : depthProfiles) {
        profile->cutIds.removeAll(id);
        profile->cutIdsExcludedFromAverage.removeAll(id);
        profile->cutIdsHiddenFromPlot.removeAll(id);
        profile->cutIdsWithoutLabel.removeAll(id);
        profile->labelPositions.remove(id);
        profile->cutRanges.remove(id);
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

QString Measurement::uniqueSplitAnalysisName(const QString &name) const {
    QStringList names;
    for(const auto &split : splitAnalyses) {
        names.append(split->name);
    }
    return uniqueName(name, names);
}

std::unique_ptr<Measurement> copyMeasurement(const Measurement &measurement) {
    auto copy = std::make_unique<Measurement>(measurement.name, measurement.description, measurement.path, measurement.cuts,
                                              measurement.settings, measurement.detector, measurement.physics);
    copy->sha256 = measurement.sha256;
    copy->referenceCutId = measurement.referenceCutId;
    for(const auto &split : measurement.splitAnalyses) {
        copy->splitAnalyses.push_back(std::make_unique<SplitAnalysis>(*split));
    }
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
                           {"type", cutTypeId(cut.type)},
                           {"color", cut.color.name()}, {"polygon", polygon}};
        if(cut.representsElement) {
            object["representsElement"] = true;
        }
        if(cut.abundance) {
            object["abundance"] = *cut.abundance;
        }
        if(cut.weight != 1) {
            object["weight"] = cut.weight;
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
        cut.type = cutTypeFromId(object["type"].toString());
        if(cut.type == CutType::Reference && !Elements::isKnown(cut.z)) {
            cut.z = 0;
            cut.a = 0;
        }
        if(!cut.color.isValid()) {
            cut.color = Defaults::elementColor(cut.z);
        }
        for(const auto &point : object["polygon"].toArray()) {
            const QJsonArray coordinates = point.toArray();
            cut.polygon.append({coordinates.at(0).toDouble(), coordinates.at(1).toDouble()});
        }
        cut.representsElement = object["representsElement"].toBool() && canRepresentElement(cut.z, cut.a);
        const double abundance = object["abundance"].toDouble();
        if(abundance > 0 && abundance <= 1 && canRepresentElement(cut.z, cut.a)) {
            cut.abundance = abundance;
        }
        cut.weight = std::clamp(object["weight"].toDouble(1), MinCutWeight, MaxCutWeight);
        if(object.contains("tofEdge")) {
            const QJsonObject edge = object["tofEdge"].toObject();
            cut.tofEdge = ToFEdge{edge["channel"].toDouble(), edge["sigma"].toDouble(), edge["manual"].toBool()};
        }
        cuts.append(cut);
    }
    sortCuts(cuts);
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

void sortElements(Material &material) {
    std::stable_sort(material.elements.begin(), material.elements.end(),
                     [](const MaterialElement &a, const MaterialElement &b) { return a.z < b.z; });
}

void sortEfficiencies(QList<EfficiencyCurve> &curves) {
    std::stable_sort(curves.begin(), curves.end(), [](const EfficiencyCurve &a, const EfficiencyCurve &b) {
        return a.z != b.z ? a.z < b.z : a.a < b.a;
    });
}

void PhysicsSettings::sortCorrections() {
    auto key = [](int z) { return z > 0 ? z : z == AnyElement ? 1000 : 1001; }; // Any, then not given yet
    std::stable_sort(corrections.begin(), corrections.end(), [&key](const StoppingCorrection &a, const StoppingCorrection &b) {
        const bool aMissing = a.z1 == 0 || a.z2 == 0;
        const bool bMissing = b.z1 == 0 || b.z2 == 0;
        if(aMissing != bMissing) {
            return bMissing;
        }
        return key(a.z1) != key(b.z1) ? key(a.z1) < key(b.z1) : key(a.z2) < key(b.z2);
    });
}

bool cutBefore(const Cut &a, const Cut &b) {
    auto typeOrder = [](CutType type) { return type == CutType::Recoil ? 0 : type == CutType::Scattered ? 1 : 2; };
    const bool aNoElement = a.z <= 0;
    const bool bNoElement = b.z <= 0;
    if(aNoElement != bNoElement) {
        return bNoElement;
    }
    if(a.z != b.z) {
        return a.z < b.z;
    }
    if(a.a != b.a) {
        return a.a < b.a;
    }
    return typeOrder(a.type) < typeOrder(b.type);
}

void sortCuts(QList<Cut> &cuts) {
    std::stable_sort(cuts.begin(), cuts.end(), cutBefore);
}

void Measurement::sortCuts() {
    ::sortCuts(cuts);
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

namespace {
QJsonObject coincidenceToJson(const CoincidenceSettings &settings) {
    return QJsonObject{{"tofAdc", settings.tofAdc},
                       {"energyAdc", settings.energyAdc},
                       {"trigger", settings.triggerIsEnergy ? "energy" : "tof"},
                       {"window", QJsonArray{settings.windowLow, settings.windowHigh}},
                       {"reorderSpan", settings.reorderSpan},
                       {"pairing", coincidencePairingId(settings.pairing)}};
}

QJsonObject angleCalibrationToJson(const AngleCalibration &calibration) {
    return QJsonObject{{"centre", calibration.centre},
                       {"centreSlope", calibration.centreSlope},
                       {"halfWidth", calibration.halfWidth},
                       {"sigma", calibration.sigma},
                       {"reversed", calibration.reversed},
                       {"maxTof", calibration.maxTof},
                       {"timingSmoothing", calibration.timingSmoothing}};
}

AngleCalibration angleCalibrationFromJson(const QJsonObject &json) {
    AngleCalibration calibration;
    calibration.centre = json["centre"].toDouble();
    calibration.centreSlope = json["centreSlope"].toDouble();
    calibration.halfWidth = json["halfWidth"].toDouble();
    calibration.sigma = std::max(0.0, json["sigma"].toDouble());
    calibration.reversed = json["reversed"].toBool();
    calibration.maxTof = json["maxTof"].toDouble();
    calibration.timingSmoothing = std::max(0.0, json["timingSmoothing"].toDouble());
    calibration.valid = calibration.halfWidth > 0;
    return calibration;
}

CoincidenceSettings coincidenceFromJson(const QJsonObject &json) {
    CoincidenceSettings settings;
    settings.tofAdc = std::max(-1, json["tofAdc"].toInt(-1));
    settings.energyAdc = std::max(-1, json["energyAdc"].toInt(-1));
    settings.triggerIsEnergy = json["trigger"].toString() != QLatin1String("tof");
    const QJsonArray window = json["window"].toArray();
    if(window.size() == 2 && window.at(0).toInteger() <= window.at(1).toInteger()) {
        settings.windowLow = window.at(0).toInteger();
        settings.windowHigh = window.at(1).toInteger();
    }
    settings.reorderSpan = std::max<qint64>(0, json["reorderSpan"].toInteger(settings.reorderSpan));
    settings.pairing = coincidencePairingFromId(json["pairing"].toString(), settings.pairing);
    return settings;
}
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
    QJsonObject json{
            {"name", detector.name},
            {"scatteringAngle", detector.scatteringAngle},
            {"tofLength", detector.tofLength},
            {"tofSlope", detector.tofSlope},
            {"tofOffset", detector.tofOffset},
            {"foil", QJsonObject{{"thickness", detector.foilThickness}, {"material", material}}},
            {"efficiencies", efficiencies},
            {"coincidence", coincidenceToJson(detector.coincidence)},
            {"detectorDistance", detector.detectorDistance},
            {"windowWidth", detector.windowWidth}};
    if(detector.angleCalibration.valid) {
        json["angleCalibration"] = angleCalibrationToJson(detector.angleCalibration);
    }
    return json;
}

DetectorSettings detectorFromJson(const QJsonObject &json) {
    DetectorSettings detector;
    detector.name = json["name"].toString();
    detector.scatteringAngle = std::clamp(json["scatteringAngle"].toDouble(MinScatteringAngle), MinScatteringAngle, MaxScatteringAngle);
    detector.tofLength = json["tofLength"].toDouble();
    detector.tofSlope = std::max(0.0, json["tofSlope"].toDouble());
    detector.tofOffset = json["tofOffset"].toDouble();
    detector.coincidence = coincidenceFromJson(json["coincidence"].toObject());
    detector.detectorDistance = std::max(0.0, json["detectorDistance"].toDouble());
    detector.windowWidth = std::max(0.0, json["windowWidth"].toDouble());
    if(json.contains("angleCalibration")) {
        detector.angleCalibration = angleCalibrationFromJson(json["angleCalibration"].toObject());
    }
    if(json.contains("foil")) {
        const QJsonObject foil = json["foil"].toObject();
        detector.foilThickness = std::max(0.0, foil["thickness"].toDouble());
        Material material;
        for(const auto &value : foil["material"].toArray()) {
            const QJsonObject element = value.toObject();
            material.elements.append({element["z"].toInt(), element["fraction"].toDouble()});
        }
        detector.foilMaterial = isValidMaterial(material) ? material : carbon(); // Carbon by default
        sortElements(detector.foilMaterial);
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
    sortEfficiencies(detector.efficiencies);
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
        case LegendPosition::BottomRight:
            return QStringLiteral("Bottom Right");
        case LegendPosition::BottomLeft:
            return QStringLiteral("Bottom Left");
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
        case LegendPosition::BottomRight:
            return QStringLiteral("bottomRight");
        case LegendPosition::BottomLeft:
            return QStringLiteral("bottomLeft");
    }
    return {};
}

LegendPosition legendPositionFromId(const QString &id, LegendPosition fallback) {
    if(id == legendPositionId(LegendPosition::Default)) {
        return LegendPosition::Default;
    }
    for(const LegendPosition position : LegendCorners) {
        if(id == legendPositionId(position)) {
            return position;
        }
    }
    return fallback;
}

Qt::Corner legendCorner(LegendPosition position) {
    switch(position) {
        case LegendPosition::TopLeft:
            return Qt::TopLeftCorner;
        case LegendPosition::BottomRight:
            return Qt::BottomRightCorner;
        case LegendPosition::BottomLeft:
            return Qt::BottomLeftCorner;
        default:
            return Qt::TopRightCorner;
    }
}

LegendPosition legendPosition(Qt::Corner corner) {
    switch(corner) {
        case Qt::TopLeftCorner:
            return LegendPosition::TopLeft;
        case Qt::BottomRightCorner:
            return LegendPosition::BottomRight;
        case Qt::BottomLeftCorner:
            return LegendPosition::BottomLeft;
        default:
            return LegendPosition::TopRight;
    }
}

QString screeningModelName(ScreeningModel model) {
    return model == ScreeningModel::None ? QStringLiteral("None (Rutherford)") : QStringLiteral("Andersen");
}

QString nuclearStoppingMassName(NuclearStoppingMass mass) {
    return mass == NuclearStoppingMass::Isotopes ? QStringLiteral("Isotopes") : QStringLiteral("Average mass");
}

QString stoppingInterpolationName(StoppingInterpolation interpolation) {
    return interpolation == StoppingInterpolation::Cubic ? QStringLiteral("Cubic") : QStringLiteral("Linear");
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
            {"nuclearMass", nuclearStoppingMassId(physics.nuclearMass)},
            {"interpolation", stoppingInterpolationId(physics.interpolation)},
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
    // The average mass when the key is missing
    physics.nuclearMass = json["nuclearMass"].toString() == nuclearStoppingMassId(NuclearStoppingMass::Isotopes)
                                  ? NuclearStoppingMass::Isotopes
                                  : NuclearStoppingMass::Average;
    // Linear when the key is missing
    physics.interpolation = json["interpolation"].toString() == stoppingInterpolationId(StoppingInterpolation::Cubic)
                                    ? StoppingInterpolation::Cubic
                                    : StoppingInterpolation::Linear;
    physics.stoppingFile = json["stoppingFile"].toString();
    for(const auto &value : json["corrections"].toArray()) {
        const QJsonObject object = value.toObject();
        StoppingCorrection c{object["z1"].toInt(), object["z2"].toInt(), std::max(0.0, object["electronicFactor"].toDouble(1)),
                             std::max(0.0, object["nuclearFactor"].toDouble(1)), object["stoppingFile"].toString()};
        if(Elements::isKnown(c.z1) && (Elements::isKnown(c.z2) || c.z2 == AnyElement) && !physics.correction(c.z1, c.z2)) {
            physics.corrections.append(c);
        }
    }
    physics.sortCorrections();
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
    // Before sorting, the indices are of the order in the file
    for(const auto &value : root["reports"].toArray()) {
        project->m_reports.push_back(project->reportFromJson(value.toObject()));
    }
    project->m_name = root["name"].toString();
    project->m_notes = root["notes"].toString();
    project->m_files = attachedFilesFromJson(root["files"].toArray(), baseDir);
    project->m_created = timeFromJson(root["created"]);
    project->m_saved = timeFromJson(root["saved"]);
    project->m_fileName = fileInfo.absoluteFilePath();
    project->sortByName();
    return project;
}

std::unique_ptr<Report> Project::reportFromJson(const QJsonObject &json) const {
    // [sample, measurement, item], nullptr for indices outside the project
    auto measurementAt = [this](const QJsonArray &reference) -> const Measurement * {
        const auto s = reference.at(0).toInteger(-1);
        const auto m = reference.at(1).toInteger(-1);
        if(reference.size() != 3 || s < 0 || s >= static_cast<qint64>(m_samples.size())) {
            return nullptr;
        }
        const auto &measurements = m_samples[static_cast<size_t>(s)]->measurements;
        return m >= 0 && m < static_cast<qint64>(measurements.size()) ? measurements[static_cast<size_t>(m)].get() : nullptr;
    };
    auto profileAt = [&measurementAt](const QJsonValue &value) -> const DepthProfile * {
        const QJsonArray reference = value.toArray();
        const Measurement *measurement = measurementAt(reference);
        const auto i = reference.at(2).toInteger(-1);
        return measurement && i >= 0 && i < static_cast<qint64>(measurement->depthProfiles.size())
                       ? measurement->depthProfiles[static_cast<size_t>(i)].get()
                       : nullptr;
    };
    auto report = std::make_unique<Report>();
    report->name = json["name"].toString();
    report->title = json["title"].toString(report->name);
    for(const auto &value : json["profiles"].toArray()) {
        if(const DepthProfile *profile = profileAt(value); profile && !report->profiles.contains(profile)) {
            report->profiles.append(profile);
        }
    }
    for(const auto &value : json["splits"].toArray()) {
        const QJsonArray reference = value.toArray();
        const Measurement *measurement = measurementAt(reference);
        const auto i = reference.at(2).toInteger(-1);
        if(measurement && i >= 0 && i < static_cast<qint64>(measurement->splitAnalyses.size())) {
            report->splits.append(measurement->splitAnalyses[static_cast<size_t>(i)].get());
        }
    }
    for(int mode = 0; mode < 3; mode++) {
        if(json["measurementNames"].toString() == ReportNameModeIds[mode]) {
            report->measurementNames = static_cast<Report::NameMode>(mode);
        }
        if(json["range"].toString() == ReportRangeModeIds[mode]) {
            report->rangeMode = static_cast<Report::RangeMode>(mode);
        }
    }
    report->settings = reportSettingsFromJson(json["settings"].toObject());
    if(json.contains("commonView")) {
        report->commonView = plotViewFromJson(json["commonView"].toObject());
    }
    for(const auto &value : json["profileViews"].toArray()) {
        if(const DepthProfile *profile = profileAt(value.toObject()["profile"])) {
            report->profileViews.insert(profile, plotViewFromJson(value.toObject()));
        }
    }
    for(const auto &value : json["fingerprints"].toArray()) {
        if(const DepthProfile *profile = profileAt(value.toObject()["profile"])) {
            report->fingerprints.insert(profile, value.toObject()["sha256"].toString());
        }
    }
    report->generated = timeFromJson(json["generated"]);
    report->optionsFingerprint = json["optionsFingerprint"].toString();
    return report;
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
    for(const AttachedFile &file : m_files) {
        if(!files.contains(file.path)) {
            files.append(file.path);
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
    for(AttachedFile &file : m_files) {
        verify(file.path, file.sha256);
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
    for(AttachedFile &file : m_files) {
        accept(file.path, file.sha256);
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
    for(AttachedFile &file : m_files) {
        fillChecksum(file.path, file.sha256, checksums);
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
    // Items of reports are referred to by their indices: [sample, measurement, item]
    QHash<const void *, QJsonArray> references;
    for(qsizetype s = 0; s < static_cast<qsizetype>(m_samples.size()); s++) {
        const auto &measurements = m_samples[static_cast<size_t>(s)]->measurements;
        for(qsizetype m = 0; m < static_cast<qsizetype>(measurements.size()); m++) {
            const Measurement &measurement = *measurements[static_cast<size_t>(m)];
            for(qsizetype i = 0; i < static_cast<qsizetype>(measurement.depthProfiles.size()); i++) {
                references.insert(measurement.depthProfiles[static_cast<size_t>(i)].get(), QJsonArray{s, m, i});
            }
            for(qsizetype i = 0; i < static_cast<qsizetype>(measurement.splitAnalyses.size()); i++) {
                references.insert(measurement.splitAnalyses[static_cast<size_t>(i)].get(), QJsonArray{s, m, i});
            }
        }
    }
    QJsonArray reports;
    for(const auto &report : m_reports) {
        QJsonArray profiles;
        QJsonArray views;
        QJsonArray fingerprints;
        for(const DepthProfile *profile : report->profiles) {
            const QJsonArray reference = references.value(profile);
            profiles.append(reference);
            if(report->profileViews.contains(profile)) {
                QJsonObject view = plotViewToJson(report->profileViews.value(profile));
                view["profile"] = reference;
                views.append(view);
            }
            if(report->fingerprints.contains(profile)) {
                fingerprints.append(QJsonObject{{"profile", reference}, {"sha256", report->fingerprints.value(profile)}});
            }
        }
        QJsonArray splits;
        for(const SplitAnalysis *split : report->splits) {
            splits.append(references.value(split));
        }
        QJsonObject object{{"name", report->name},
                           {"title", report->title},
                           {"profiles", profiles},
                           {"splits", splits},
                           {"measurementNames", ReportNameModeIds[static_cast<int>(report->measurementNames)]},
                           {"settings", reportSettingsToJson(report->settings)},
                           {"range", ReportRangeModeIds[static_cast<int>(report->rangeMode)]},
                           {"commonView", plotViewToJson(report->commonView)},
                           {"profileViews", views},
                           {"fingerprints", fingerprints}};
        if(report->generated.isValid()) {
            object["generated"] = timeToJson(report->generated);
            object["optionsFingerprint"] = report->optionsFingerprint;
        }
        reports.append(object);
    }
    QJsonObject root{
            {"format", FormatName},
            {"version", FormatVersion},
            {"experimentalSettings", settings},
            {"detectorSettings", detectors},
            {"physicsSettings", physics},
            {"samples", samples},
            {"reports", reports}};
    // Unknown for projects saved before the times were stored
    if(!m_name.isEmpty()) {
        root["name"] = m_name;
    }
    if(!m_notes.isEmpty()) {
        root["notes"] = m_notes;
    }
    if(!m_files.isEmpty()) {
        root["files"] = attachedFilesToJson(m_files, storedPath);
    }
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
    removeFromReports(m_samples.at(index).get());
    m_samples.erase(m_samples.begin() + static_cast<std::ptrdiff_t>(index));
    m_modified = true;
}

QString Project::displayName() const {
    if(!m_name.isEmpty()) {
        return m_name;
    }
    return m_fileName.isEmpty() ? QStringLiteral("Untitled") : QFileInfo(m_fileName).completeBaseName();
}

Report *Project::addReport(const QString &name) {
    auto report = std::make_unique<Report>();
    report->name = uniqueReportName(name);
    report->title = report->name;
    forEachMeasurement([&report](const Measurement *measurement) {
        for(const auto &profile : measurement->depthProfiles) {
            report->profiles.append(profile.get());
        }
    });
    m_reports.push_back(std::move(report));
    m_modified = true;
    return m_reports.back().get();
}

void Project::removeReport(size_t index) {
    m_reports.erase(m_reports.begin() + static_cast<std::ptrdiff_t>(index));
    m_modified = true;
}

QString Project::uniqueReportName(const QString &name) const {
    QStringList names;
    for(const auto &report : m_reports) {
        names.append(report->name);
    }
    return uniqueName(name, names);
}

const Measurement *Project::measurementOf(const DepthProfile *profile) const {
    const Measurement *result = nullptr;
    forEachMeasurement([&](const Measurement *measurement) {
        for(const auto &candidate : measurement->depthProfiles) {
            if(candidate.get() == profile) {
                result = measurement;
            }
        }
    });
    return result;
}

const Measurement *Project::measurementOf(const SplitAnalysis *split) const {
    const Measurement *result = nullptr;
    forEachMeasurement([&](const Measurement *measurement) {
        for(const auto &candidate : measurement->splitAnalyses) {
            if(candidate.get() == split) {
                result = measurement;
            }
        }
    });
    return result;
}

const Sample *Project::sampleOf(const Measurement *measurement) const {
    for(const auto &sample : m_samples) {
        for(const auto &candidate : sample->measurements) {
            if(candidate.get() == measurement) {
                return sample.get();
            }
        }
    }
    return nullptr;
}

void Project::removeFromReports(const Sample *sample) {
    for(const auto &measurement : sample->measurements) {
        removeFromReports(measurement.get());
    }
}

void Project::removeFromReports(const Measurement *measurement) {
    for(const auto &profile : measurement->depthProfiles) {
        removeFromReports(profile.get());
    }
    for(const auto &split : measurement->splitAnalyses) {
        removeFromReports(split.get());
    }
}

void Project::removeFromReports(const DepthProfile *profile) {
    for(const auto &report : m_reports) {
        report->profiles.removeAll(profile);
        report->profileViews.remove(profile);
        report->fingerprints.remove(profile);
    }
}

void Project::removeFromReports(const SplitAnalysis *split) {
    for(const auto &report : m_reports) {
        report->splits.removeAll(split);
    }
}

namespace {
QString &projectDirectoryStorage() {
    static QString directory;
    return directory;
}
}

const QString &projectDirectory() {
    return projectDirectoryStorage();
}

void setProjectDirectory(const QString &directory) {
    projectDirectoryStorage() = directory;
}

QString inProjectDirectory(const QString &fileName) {
    return projectDirectory().isEmpty() ? fileName : QDir(projectDirectory()).filePath(fileName);
}
