#include "defaults.h"

#include <algorithm>
#include <cmath>

#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>

namespace {
// Stored as JSON in the same format as in project files, in SI units. Keys used by earlier versions are not read.
const QString ExperimentalKey = QStringLiteral("defaults/experimentalSettings");
const QString DetectorKey = QStringLiteral("defaults/detector");
const QString HistogramKey = QStringLiteral("defaults/histogram");
const QString ElementColorsKey = QStringLiteral("defaults/elementColors"); // {"14": "#rrggbb", ...}

QJsonObject load(const QString &key) {
    return QJsonDocument::fromJson(QSettings().value(key).toByteArray()).object();
}

void store(const QString &key, const QJsonObject &json) {
    QSettings().setValue(key, QJsonDocument(json).toJson(QJsonDocument::Compact));
}

// Identifiers in the stored JSON
QString colorSchemeId(ColorScheme scheme) {
    switch(scheme) {
        case ColorScheme::Viridis:
            return QStringLiteral("viridis");
        case ColorScheme::Rainbow:
            return QStringLiteral("rainbow");
        case ColorScheme::Hot:
            return QStringLiteral("hot");
        case ColorScheme::Grayscale:
            return QStringLiteral("grayscale");
    }
    return {};
}

// Reads [x, y] bin widths, keeping the given values when they are missing or invalid
void binsFromJson(const QJsonValue &value, int &x, int &y) {
    const QJsonArray array = value.toArray();
    if(array.size() == 2 && array.at(0).toInt() >= 1 && array.at(1).toInt() >= 1) {
        x = array.at(0).toInt();
        y = array.at(1).toInt();
    }
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

QString colorSchemeName(ColorScheme scheme) {
    switch(scheme) {
        case ColorScheme::Viridis:
            return QStringLiteral("Viridis");
        case ColorScheme::Rainbow:
            return QStringLiteral("Rainbow");
        case ColorScheme::Hot:
            return QStringLiteral("Hot");
        case ColorScheme::Grayscale:
            return QStringLiteral("Grayscale");
    }
    return {};
}

HistogramPreferences Defaults::histogramPreferences() {
    HistogramPreferences preferences;
    const QJsonObject json = load(HistogramKey);
    binsFromJson(json["minBin"], preferences.minBinX, preferences.minBinY);
    binsFromJson(json["maxBin"], preferences.maxBinX, preferences.maxBinY);
    preferences.maxBinX = std::max(preferences.maxBinX, preferences.minBinX);
    preferences.maxBinY = std::max(preferences.maxBinY, preferences.minBinY);
    for(ColorScheme scheme : ColorSchemes) {
        if(json["colorScheme"].toString() == colorSchemeId(scheme)) {
            preferences.colorScheme = scheme;
        }
    }
    preferences.logScale = json["logScale"].toBool(preferences.logScale);
    return preferences;
}

void Defaults::setHistogramPreferences(const HistogramPreferences &preferences) {
    store(HistogramKey, QJsonObject{{"minBin", QJsonArray{preferences.minBinX, preferences.minBinY}},
                                    {"maxBin", QJsonArray{preferences.maxBinX, preferences.maxBinY}},
                                    {"colorScheme", colorSchemeId(preferences.colorScheme)},
                                    {"logScale", preferences.logScale}});
}
