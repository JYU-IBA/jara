#ifndef JARA_TOFEDATA_H
#define JARA_TOFEDATA_H

#include <limits>
#include <span>
#include <vector>

#include <QList>
#include <QPointF>
#include <QPolygonF>
#include <QString>

struct ToFEEvent {
    double tof; // Channels
    double energy; // Channels
    // Third column, the coincidence time difference in ticks, which follows the position in the detector, see
    // anglecalibration.h. NaN when the file has no third column.
    double timing = std::numeric_limits<double>::quiet_NaN();
};

// Reads an N-column ASCII event file. The first two columns are ToF and E in channels, the third is the timing when
// there is one, further columns are ignored.
// Empty lines and lines starting with '#' are skipped.
bool loadAscFile(const QString &fileName, std::vector<ToFEEvent> &events, QString &errorMessage);

// Calls f for every event inside the polygon, ToF and E in channels
template<typename F> void forEachEventIn(std::span<const ToFEEvent> events, const QList<QPointF> &points, F f) {
    const QPolygonF polygon(points);
    const QRectF bounds = polygon.boundingRect();
    for(const auto &event : events) {
        const QPointF point(event.tof, event.energy);
        if(bounds.contains(point) && polygon.containsPoint(point, Qt::OddEvenFill)) {
            f(event);
        }
    }
}

size_t countEventsIn(std::span<const ToFEEvent> events, const QList<QPointF> &polygon);

#endif // JARA_TOFEDATA_H
