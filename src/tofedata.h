#ifndef JARA_TOFEDATA_H
#define JARA_TOFEDATA_H

#include <span>
#include <vector>

#include <QList>
#include <QPointF>
#include <QPolygonF>
#include <QString>

struct ToFEEvent {
    double tof; // Channels
    double energy; // Channels
};

// Reads an N-column ASCII event file. The first two columns are ToF and E in channels, further columns are ignored.
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
