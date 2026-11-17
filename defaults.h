#ifndef JARA_DEFAULTS_H
#define JARA_DEFAULTS_H

#include "project.h"

// Program-wide default settings, used for new projects and new settings sets. Stored with QSettings.
namespace Defaults {
ExperimentalSettings experimentalSettings();
void setExperimentalSettings(const ExperimentalSettings &settings);
DetectorSettings detectorSettings();
void setDetectorSettings(const DetectorSettings &detector);
}

#endif // JARA_DEFAULTS_H
