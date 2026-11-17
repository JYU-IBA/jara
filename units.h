#ifndef JARA_UNITS_H
#define JARA_UNITS_H

// Quantities are stored in SI units, both in memory and in files: metres, radians and seconds. Energies are the
// exception, they are stored in MeV. The user interface converts to and from the units below.

extern "C" {
#include <jibal_units.h>
}

namespace Units {
constexpr double Degree = C_DEG;
constexpr double Millimetre = C_MM;
constexpr double Nanosecond = C_NS;
}

#endif // JARA_UNITS_H
