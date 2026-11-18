#ifndef JARA_DEFAULTS_H
#define JARA_DEFAULTS_H

#include <QColor>
#include <QMap>

#include "project.h"

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
}

#endif // JARA_DEFAULTS_H
