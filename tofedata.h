#ifndef JARA_TOFEDATA_H
#define JARA_TOFEDATA_H

#include <vector>

#include <QString>

struct ToFEEvent {
    double tof; // Channels
    double energy; // Channels
};

// Reads an N-column ASCII event file. The first two columns are ToF and E in channels, further columns are ignored.
// Empty lines and lines starting with '#' are skipped.
bool loadAscFile(const QString &fileName, std::vector<ToFEEvent> &events, QString &errorMessage);

#endif // JARA_TOFEDATA_H
