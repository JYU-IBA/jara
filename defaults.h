#ifndef JARA_DEFAULTS_H
#define JARA_DEFAULTS_H

#include <QColor>
#include <QMap>

#include "project.h"

enum class ColorScheme { Viridis, Rainbow, Hot, Grayscale };
constexpr ColorScheme ColorSchemes[] = {ColorScheme::Viridis, ColorScheme::Rainbow, ColorScheme::Hot, ColorScheme::Grayscale};
QString colorSchemeName(ColorScheme scheme);

struct HistogramPreferences {
    int minBinX = 1; // Channels per bin, ToF
    int minBinY = 1; // E
    int maxBinX = 16;
    int maxBinY = 16;
    ColorScheme colorScheme = ColorScheme::Viridis;
    bool logScale = true; // Default for new histograms, can be changed in each histogram
};

// Program-wide default settings, used for new projects and new settings sets. Stored with QSettings.
namespace Defaults {
ExperimentalSettings experimentalSettings();
void setExperimentalSettings(const ExperimentalSettings &settings);
DetectorSettings detectorSettings();
void setDetectorSettings(const DetectorSettings &detector);

// Colors of new selections by element. Only colors changed by the user are stored, the others are built in.
QColor elementColor(int z);
QColor builtInElementColor(int z);
QMap<int, QColor> customElementColors(); // By Z
void setCustomElementColors(const QMap<int, QColor> &colors);

HistogramPreferences histogramPreferences();
void setHistogramPreferences(const HistogramPreferences &preferences);
}

#endif // JARA_DEFAULTS_H
