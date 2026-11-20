#ifndef JARA_UNITS_H
#define JARA_UNITS_H

// Quantities are stored in SI units, both in memory and in files: metres, radians, seconds, joules and kg/m² for
// areal densities. The user interface converts to and from the units below.

extern "C" {
#include <jibal_units.h>
}

namespace Units {
constexpr double Degree = C_DEG;
constexpr double Millimetre = C_MM;
constexpr double Nanosecond = C_NS;
constexpr double Kiloelectronvolt = C_KEV;
constexpr double Megaelectronvolt = C_MEV;
constexpr double MicrogramPerSquareCentimetre = C_UG / C_CM2;
constexpr double ThinFilmUnit = C_TFU; // 1e15 atoms/cm² as atoms/m²
constexpr double AtomicMassUnit = C_U; // kg
}

#endif // JARA_UNITS_H
