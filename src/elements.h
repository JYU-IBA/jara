/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#ifndef JARA_ELEMENTS_H
#define JARA_ELEMENTS_H

#include <QList>
#include <QPoint>
#include <QString>

struct jibal;

// Where the JIBAL configuration file comes from, a global preference (see defaults.h) read once at startup
enum class JibalConfigSource {
    Distribution, // The one installed with JaRA, or without one the one JIBAL finds itself
    User, // JIBAL's user configuration, e.g. ~/.jibal/jibal.conf
    File // A file chosen by the user
};

// Element and isotope data from JIBAL
namespace Elements {
struct Isotope {
    int a; // Mass number
    double abundance; // Natural abundance, zero for isotopes that do not occur naturally
    double mass; // kg
};

// Initializes JIBAL with the configuration of the source, file being the one of File. Call once at startup.
bool init(JibalConfigSource source, const QString &file, QString &errorMessage);
QString sourceId(JibalConfigSource source); // "distribution", "user" or "file", as stored in JSON
JibalConfigSource sourceFromId(const QString &id); // Distribution for unknown ids
// Where the configuration file in use comes from, whatever the preference was: "distribution" (installed with JaRA),
// "user" (JIBAL's user configuration), "file" (another file, chosen or found by JIBAL) or "none" (JIBAL read none).
// With the distribution preference and no file installed with JaRA, JIBAL finds one itself, e.g. the user's.
QString configSourceInUse();
QString configSourceDescription(const QString &id); // Of configSourceInUse(), for people
QString jibalVersion();
QString bundledConfigFile(); // Installed with JaRA, empty when there is none
QString userConfigFile(); // JIBAL's user configuration file, whether it exists or not
QString configFileFor(JibalConfigSource source, const QString &file); // For init(), file is the one of File
QString configFileInUse(); // Read by JIBAL, empty when it found none or is not initialized
void cleanup();
jibal *library(); // Also for stopping, see stopping.h
int maxZ();
bool isKnown(int z);
QString symbol(int z);
QString label(int z, int a); // "Si" or "28Si", a = 0 means no specific isotope
QString displayLabel(int z, int a); // Like label(), but with the mass number in Unicode superscript digits
QList<Isotope> isotopes(int z);
bool hasIsotope(int z, int a);
double abundance(int z, int a); // Natural abundance of the isotope, zero when it does not occur naturally
// Mass of the isotope in kg, or the average mass of the naturally occurring isotopes weighted by their abundances when
// a = 0. Zero when not known.
double mass(int z, int a);
// Accepts a symbol ("Si", case insensitive), an atomic number ("14") or an isotope ("28Si", "Si-28", "Si28").
// Sets a to zero when no isotope is given.
bool parse(const QString &text, int &z, int &a);

// Lanthanides and actinides are shown below the main periodic table, after a gap row
constexpr int LanthanideRow = 8;
QPoint tablePosition(int z); // Column (x) and row (y) of an element in the periodic table
}

#endif // JARA_ELEMENTS_H
