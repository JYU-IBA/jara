#pragma once

#include <QString>

namespace Version {
QString version(); // "git describe" of the build (e.g. v0.1.1-3-gabcdef0-dirty), or the plain version without git
QString versionSimple(); // Version number of the latest release tag, e.g. 0.1.1
bool gitPopulated();
bool gitDirty();
QString gitBranch();
QString gitCommitSha1();
QString gitCommitDate();
}
