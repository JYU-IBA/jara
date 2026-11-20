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
const QString PhysicsKey = QStringLiteral("defaults/physics");
const QString HistogramKey = QStringLiteral("defaults/histogram");
const QString DepthProfileKey = QStringLiteral("defaults/depthProfile");
const QString CoincidenceKey = QStringLiteral("defaults/coincidence");
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

PhysicsSettings Defaults::physicsSettings() {
    const QJsonObject json = load(PhysicsKey);
    if(json.isEmpty()) {
        PhysicsSettings physics;
        physics.name = QStringLiteral("Physics");
        return physics;
    }
    return physicsFromJson(json);
}

void Defaults::setPhysicsSettings(const PhysicsSettings &physics) {
    store(PhysicsKey, physicsToJson(physics));
}

QColor Defaults::elementColor(int z) {
    return customElementColors().value(z, builtInElementColor(z));
}

QColor Defaults::builtInElementColor(int z) {
    // Elements common in ERDA have colors chosen to be told apart, the same as in Potku
    static const QMap<int, QColor> chosen{
            {1, QColor(0xb4, 0x90, 0x3c)},  // H
            {3, QColor(0xcf, 0x8a, 0xed)},  // Li
            {6, QColor(0x51, 0x3c, 0x34)},  // C
            {7, QColor(0x00, 0xaa, 0x00)},  // N
            {8, QColor(0x00, 0x00, 0xff)},  // O
            {9, QColor(0xef, 0x7b, 0x19)},  // F
            {11, QColor(0x6a, 0xa8, 0x87)}, // Na
            {12, QColor(0x4b, 0xed, 0x41)}, // Mg
            {13, QColor(0x47, 0xbf, 0xed)}, // Al
            {14, QColor(0x80, 0x00, 0x80)}, // Si
            {16, QColor(0xae, 0xa2, 0x16)}, // S
            {17, QColor(0xc2, 0x00, 0xc2)}, // Cl
            {18, QColor(0xed, 0x86, 0xc1)}, // Ar
            {24, QColor(0x49, 0xdd, 0xed)}, // Cr
            {26, QColor(0xed, 0x55, 0x61)}, // Fe
            {28, QColor(0x7a, 0x22, 0xed)}, // Ni
            {29, QColor(0xff, 0xaa, 0x00)}, // Cu
            {79, QColor(0xea, 0xc5, 0x17)}, // Au
    };
    if(const auto found = chosen.find(z); found != chosen.end()) {
        return found.value();
    }
    if(z <= 0) {
        return QColor(0x80, 0x80, 0x80); // Reference selections without an element
    }
    // Others have hues a golden angle apart, so that elements close to each other get clearly different colors
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

DepthProfilePreferences Defaults::depthProfilePreferences() {
    DepthProfilePreferences preferences;
    const QJsonObject json = load(DepthProfileKey);
    preferences.maxIterations = std::max(1, json["maxIterations"].toInt(preferences.maxIterations));
    auto positive = [&json](const char *key, double &value) {
        if(const double read = json[key].toDouble(); read > 0) {
            value = read;
        }
    };
    positive("tolerance", preferences.tolerance);
    positive("binWidth", preferences.binWidth);
    positive("stoppingBinWidth", preferences.stoppingBinWidth);
    positive("maxDepth", preferences.maxDepth);
    preferences.smoothingEvents = std::max(1, json["smoothingEvents"].toInt(preferences.smoothingEvents));
    preferences.autoMaxDepth = json["autoMaxDepth"].toBool(preferences.autoMaxDepth);
    preferences.legendPosition = legendPositionFromId(json["legendPosition"].toString(), preferences.legendPosition);
    preferences.showLegendConcentrations = json["legendConcentrations"].toBool(preferences.showLegendConcentrations);
    preferences.roundTableConcentrations = json["roundTableConcentrations"].toBool(preferences.roundTableConcentrations);
    preferences.roundLegendConcentrations = json["roundLegendConcentrations"].toBool(preferences.roundLegendConcentrations);
    if(preferences.legendPosition == LegendPosition::Default) {
        preferences.legendPosition = LegendPosition::TopRight;
    }
    return preferences;
}

void Defaults::setDepthProfilePreferences(const DepthProfilePreferences &preferences) {
    store(DepthProfileKey, QJsonObject{{"maxIterations", preferences.maxIterations},
                                       {"tolerance", preferences.tolerance},
                                       {"binWidth", preferences.binWidth},
                                       {"stoppingBinWidth", preferences.stoppingBinWidth},
                                       {"smoothingEvents", preferences.smoothingEvents},
                                       {"maxDepth", preferences.maxDepth},
                                       {"autoMaxDepth", preferences.autoMaxDepth},
                                       {"legendPosition", legendPositionId(preferences.legendPosition)},
                                       {"legendConcentrations", preferences.showLegendConcentrations},
                                       {"roundTableConcentrations", preferences.roundTableConcentrations},
                                       {"roundLegendConcentrations", preferences.roundLegendConcentrations}});
}

DepthProfile Defaults::depthProfile() {
    const DepthProfilePreferences preferences = depthProfilePreferences();
    DepthProfile profile;
    profile.binWidth = preferences.binWidth;
    profile.stoppingBinWidth = preferences.stoppingBinWidth;
    profile.minStoppingBinEvents = preferences.smoothingEvents;
    profile.maxDepth = preferences.maxDepth;
    profile.autoMaxDepth = preferences.autoMaxDepth;
    profile.showLegendConcentrations = preferences.showLegendConcentrations;
    profile.roundTableConcentrations = preferences.roundTableConcentrations;
    profile.roundLegendConcentrations = preferences.roundLegendConcentrations;
    return profile;
}

CoincidencePreferences Defaults::coincidencePreferences() {
    CoincidencePreferences preferences;
    const QJsonObject json = load(CoincidenceKey);
    preferences.plotMargin = std::max(0, json["plotMargin"].toInt(preferences.plotMargin));
    preferences.previewSignals = std::max(1000, json["previewSignals"].toInt(preferences.previewSignals));
    preferences.logScale = json["logScale"].toBool(preferences.logScale);
    return preferences;
}

void Defaults::setCoincidencePreferences(const CoincidencePreferences &preferences) {
    store(CoincidenceKey, QJsonObject{{"plotMargin", preferences.plotMargin}, {"previewSignals", preferences.previewSignals},
                                      {"logScale", preferences.logScale}});
}
