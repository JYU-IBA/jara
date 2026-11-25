/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#pragma once

#include <QString>

namespace Version {
// Semantic version of the build from "git describe": the version of the latest release tag, with the commits since it,
// the commit and uncommitted changes as build metadata (e.g. 0.1.1+3.gabcdef0.dirty), or the plain version without git
QString version();
QString versionSimple(); // Version number of the latest release tag, e.g. 0.1.1
bool gitPopulated();
bool gitDirty();
QString gitBranch();
QString gitCommitSha1();
QString gitCommitDate();
}
