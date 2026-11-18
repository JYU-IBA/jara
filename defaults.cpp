#include "defaults.h"

#include <cmath>

#include <QJsonDocument>
#include <QSettings>

namespace {
// Stored as JSON in the same format as in project files, in SI units. The keys used before SI units are not read.
const QString ExperimentalKey = QStringLiteral("defaults/experimental");
const QString DetectorKey = QStringLiteral("defaults/detector");
const QString ElementColorsKey = QStringLiteral("defaults/elementColors"); // {"14": "#rrggbb", ...}

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

QColor Defaults::elementColor(int z) {
    return customElementColors().value(z, builtInElementColor(z));
}

QColor Defaults::builtInElementColor(int z) {
    // Hues a golden angle apart, so that elements close to each other get clearly different colors
    const int hue = static_cast<int>(std::lround(z * 137.508)) % 360;
    return QColor::fromHsv(hue, 230, 220);
}

QMap<int, QColor> Defaults::customElementColors() {
    QMap<int, QColor> colors;
    const QJsonObject json = load(ElementColorsKey);
    for(auto i = json.begin(); i != json.end(); ++i) {
        bool ok = false;
        const int z = i.key().toInt(&ok);
        const QColor color = QColor::fromString(i.value().toString());
        if(ok && color.isValid()) {
            colors.insert(z, color);
        }
    }
    return colors;
}

void Defaults::setCustomElementColors(const QMap<int, QColor> &colors) {
    QJsonObject json;
    for(auto i = colors.begin(); i != colors.end(); ++i) {
        json.insert(QString::number(i.key()), i.value().name());
    }
    store(ElementColorsKey, json);
}
