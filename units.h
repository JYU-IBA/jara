#ifndef JARA_UNITS_H
#define JARA_UNITS_H

// Quantities are stored in SI units, both in memory and in files: metres, radians, seconds and joules. The user
// interface converts to and from the units below.

extern "C" {
#include <jibal_units.h>
}

namespace Units {
constexpr double Degree = C_DEG;
constexpr double Millimetre = C_MM;
constexpr double Nanosecond = C_NS;
constexpr double Kiloelectronvolt = C_KEV;
constexpr double Megaelectronvolt = C_MEV;
}

#endif // JARA_UNITS_H
