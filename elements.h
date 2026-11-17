#ifndef JARA_ELEMENTS_H
#define JARA_ELEMENTS_H

#include <QList>
#include <QString>

// Element and isotope data from JIBAL
namespace Elements {
struct Isotope {
    int a; // Mass number
    double abundance; // Natural abundance, zero for isotopes that do not occur naturally
};

bool init(QString &errorMessage); // Call once at startup
void cleanup();
int maxZ();
bool isKnown(int z);
QString symbol(int z);
QString label(int z, int a); // "Si" or "28Si", a = 0 means no specific isotope
QList<Isotope> isotopes(int z);
bool hasIsotope(int z, int a);
// Accepts a symbol ("Si", case insensitive), an atomic number ("14") or an isotope ("28Si", "Si-28", "Si28").
// Sets a to zero when no isotope is given.
bool parse(const QString &text, int &z, int &a);
}

#endif // JARA_ELEMENTS_H
