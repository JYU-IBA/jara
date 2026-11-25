/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#ifndef JARA_DEFAULTS_H
#define JARA_DEFAULTS_H

#include <QColor>
#include <QMap>

#include "elements.h"
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

struct DepthProfilePreferences {
    int maxIterations = 10; // Of one run, the user can continue iterating
    double tolerance = 0.001; // Atomic fraction. Converged when no concentration in the stopping bins changes more.
    // Parameters of new depth profiles, depths in atoms/m²
    double binWidth = DefaultDepthBinWidth;
    double stoppingBinWidth = DefaultStoppingBinWidth;
    int smoothingEvents = DefaultMinStoppingBinEvents;
    double maxDepth = DefaultMaxDepth;
    bool autoMaxDepth = true;
    LegendPosition legendPosition = LegendPosition::TopRight; // Of depth profiles that use the default, never Default
    bool showLegendConcentrations = true; // Of new depth profiles
    bool roundTableConcentrations = false; // Of new depth profiles
    bool roundLegendConcentrations = true; // Of new depth profiles
};

// The dialog for converting list-mode files, see coincidence.h. The coincidence parameters are in the detector settings.
struct CoincidencePreferences {
    int plotMargin = 100; // Ticks. The plot of the time differences shows this much more on both sides of the window.
    int previewSignals = 5000000; // Read from the beginning of a file for the plot and the statistics
    bool logScale = true; // Of the distribution of the time differences, also changed from its context menu
};

// The JIBAL configuration file, read once at startup, so a change takes effect when JaRA is restarted
struct JibalConfigPreference {
    JibalConfigSource source = JibalConfigSource::Distribution;
    QString file; // Of JibalConfigSource::File
};

// Program-wide default settings, used for new projects and new settings sets. Stored with QSettings.
namespace Defaults {
ExperimentalSettings experimentalSettings();
void setExperimentalSettings(const ExperimentalSettings &settings);
DetectorSettings detectorSettings();
void setDetectorSettings(const DetectorSettings &detector);
PhysicsSettings physicsSettings();
void setPhysicsSettings(const PhysicsSettings &physics);

// Colors of new selections by element. Only colors changed by the user are stored, the others are built in.
QColor elementColor(int z);
QColor builtInElementColor(int z);
QMap<int, QColor> customElementColors(); // By Z
void setCustomElementColors(const QMap<int, QColor> &colors);

HistogramPreferences histogramPreferences();
void setHistogramPreferences(const HistogramPreferences &preferences);

DepthProfilePreferences depthProfilePreferences();
void setDepthProfilePreferences(const DepthProfilePreferences &preferences);
DepthProfile depthProfile(); // A new depth profile with the parameters from the preferences

CoincidencePreferences coincidencePreferences();
void setCoincidencePreferences(const CoincidencePreferences &preferences);

JibalConfigPreference jibalConfigPreference();
void setJibalConfigPreference(const JibalConfigPreference &preference);
}

#endif // JARA_DEFAULTS_H
