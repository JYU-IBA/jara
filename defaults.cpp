#include "defaults.h"

#include <QJsonDocument>
#include <QSettings>

namespace {
// Stored as JSON in the same format as in project files, in SI units. The keys used before SI units are not read.
const QString ExperimentalKey = QStringLiteral("defaults/experimental");
const QString DetectorKey = QStringLiteral("defaults/detector");

QJsonObject load(const QString &key) {
    return QJsonDocument::fromJson(QSettings().value(key).toByteArray()).object();
}

void store(const QString &key, const QJsonObject &json) {
    QSettings().setValue(key, QJsonDocument(json).toJson(QJsonDocument::Compact));
}
}

ExperimentalSettings Defaults::experimentalSettings() {
    const QJsonObject json = load(ExperimentalKey);
    if(json.isEmpty()) {
        ExperimentalSettings settings;
        settings.name = QStringLiteral("Experiment");
        return settings;
    }
    return settingsFromJson(json);
}

void Defaults::setExperimentalSettings(const ExperimentalSettings &settings) {
    store(ExperimentalKey, settingsToJson(settings));
}

DetectorSettings Defaults::detectorSettings() {
    const QJsonObject json = load(DetectorKey);
    if(json.isEmpty()) {
        DetectorSettings detector;
        detector.name = QStringLiteral("Detector");
        return detector;
    }
    return detectorFromJson(json);
}

void Defaults::setDetectorSettings(const DetectorSettings &detector) {
    store(DetectorKey, detectorToJson(detector));
}
