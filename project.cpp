#include "project.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {
const QString FormatName = QStringLiteral("jara-project");
constexpr int FormatVersion = 1;

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

QJsonArray cutsToJson(const QList<Cut> &cuts) {
    QJsonArray result;
    for(const auto &cut : cuts) {
        QJsonArray polygon;
        for(const QPointF &point : cut.polygon) {
            polygon.append(QJsonArray{point.x(), point.y()});
        }
        result.append(QJsonObject{{"z", cut.z}, {"a", cut.a}, {"polygon", polygon}});
    }
    return result;
}

QList<Cut> cutsFromJson(const QJsonArray &json) {
    QList<Cut> cuts;
    for(const auto &value : json) {
        const QJsonObject object = value.toObject();
        Cut cut{object["z"].toInt(), object["a"].toInt(), {}};
        for(const auto &point : object["polygon"].toArray()) {
            const QJsonArray coordinates = point.toArray();
            cut.polygon.append({coordinates.at(0).toDouble(), coordinates.at(1).toDouble()});
        }
        cuts.append(cut);
    }
    return cuts;
}

QJsonObject sampleToJson(const Sample &sample, const QDir &baseDir) {
    QJsonArray metadata;
    for(const auto &entry : sample.metadata) {
        metadata.append(QJsonObject{{"key", entry.key}, {"value", entry.value}});
    }
    QJsonArray files;
    for(const auto &file : sample.files) {
        files.append(QJsonObject{{"path", pathToJson(file.path, baseDir)}, {"description", file.description}});
    }
    QJsonArray measurements;
    for(const auto &measurement : sample.measurements) {
        measurements.append(QJsonObject{
                {"name", measurement->name},
                {"description", measurement->description},
                {"path", pathToJson(measurement->path, baseDir)},
                {"cuts", cutsToJson(measurement->cuts)}});
    }
    return QJsonObject{
            {"name", sample.name},
            {"description", sample.description},
            {"metadata", metadata},
            {"files", files},
            {"measurements", measurements}};
}

std::unique_ptr<Sample> sampleFromJson(const QJsonObject &json, const QDir &baseDir) {
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
        sample->files.append({path, file["description"].toString()});
    }
    for(const auto &value : json["measurements"].toArray()) {
        const QJsonObject measurement = value.toObject();
        sample->measurements.push_back(std::make_unique<Measurement>(
                measurement["name"].toString(), measurement["description"].toString(),
                pathFromJson(measurement["path"], baseDir), cutsFromJson(measurement["cuts"].toArray())));
    }
    return sample;
}
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
    if(version < 1 || version > FormatVersion) {
        errorMessage = QString("Unsupported project file version %1.").arg(version);
        return nullptr;
    }

    auto project = std::make_unique<Project>();
    const QFileInfo fileInfo(fileName);
    const QDir baseDir = fileInfo.absoluteDir();
    for(const auto &value : root["samples"].toArray()) {
        project->m_samples.push_back(sampleFromJson(value.toObject(), baseDir));
    }
    project->m_fileName = fileInfo.absoluteFilePath();
    return project;
}

bool Project::save(const QString &fileName, QString &errorMessage) {
    const QFileInfo fileInfo(fileName);
    const QDir baseDir = fileInfo.absoluteDir();
    QJsonArray samples;
    for(const auto &sample : m_samples) {
        samples.append(sampleToJson(*sample, baseDir));
    }
    const QJsonObject root{
            {"format", FormatName},
            {"version", FormatVersion},
            {"samples", samples}};

    QSaveFile file(fileName);
    if(!file.open(QIODevice::WriteOnly)) {
        errorMessage = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson());
    if(!file.commit()) {
        errorMessage = file.errorString();
        return false;
    }
    m_fileName = fileInfo.absoluteFilePath();
    m_modified = false;
    return true;
}

Sample *Project::addSample(const QString &name) {
    auto sample = std::make_unique<Sample>();
    sample->name = name;
    m_samples.push_back(std::move(sample));
    m_modified = true;
    return m_samples.back().get();
}

void Project::removeSample(size_t index) {
    m_samples.erase(m_samples.begin() + static_cast<std::ptrdiff_t>(index));
    m_modified = true;
}
