#ifndef JARA_ELEMENTS_H
#define JARA_ELEMENTS_H

#include <QList>
#include <QPoint>
#include <QString>

struct jibal;

// Element and isotope data from JIBAL
namespace Elements {
struct Isotope {
    int a; // Mass number
    double abundance; // Natural abundance, zero for isotopes that do not occur naturally
    double mass; // kg
};

bool init(QString &errorMessage); // Call once at startup
void cleanup();
jibal *library(); // Also for stopping, see stopping.h
int maxZ();
bool isKnown(int z);
QString symbol(int z);
QString label(int z, int a); // "Si" or "28Si", a = 0 means no specific isotope
QString displayLabel(int z, int a); // Like label(), but with the mass number in Unicode superscript digits
QList<Isotope> isotopes(int z);
bool hasIsotope(int z, int a);
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
