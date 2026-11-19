#ifndef JARA_SPLIT_H
#define JARA_SPLIT_H

#include <span>
#include <vector>

#include <QList>
#include <QPointF>
#include <QString>

#include "project.h"
#include "tofedata.h"

// Division of a measurement into parts by the counts of its reference selection, following the order of the events
// in the data file, which is the order they were measured in. With N reference counts, the part from fraction f1 to
// f2 of the measurement starts after the round(f1 N):th reference event and ends with the round(f2 N):th one. The
// events before the first reference event belong to the first part, the ones after the last to the last part.
// Without a reference, every event counts.
//
// Split analyses count selections in n parts of N / n reference counts each. Beam induced losses, e.g. hydrogen
// released from the sample, show up as counts decreasing from part to part. A line is fitted to the counts against
// the middle of each part as a percentage of the reference counts. The counts are assumed to be Poisson distributed
// around a common mean, so all parts get the same weight and the variance is taken to be the mean count.

class EventDivision {
public:
    // Empty polygon: all events count
    EventDivision(const std::vector<ToFEEvent> &events, const QList<QPointF> &referencePolygon);
    size_t referenceCounts() const;
    size_t eventIndex(double fraction) const; // Where the part starting at the fraction (0...1) starts
    std::span<const ToFEEvent> range(double min, double max) const; // Events from fraction min to max

private:
    const std::vector<ToFEEvent> &m_events;
    bool m_allEvents;
    std::vector<size_t> m_referenceIndices; // Of the reference events in the events
};

// Everything a split analysis is calculated from, apart from the events. Comparing these tells whether a calculated
// analysis is still up to date.
struct SplitInputs {
    struct CutInput {
        int id;
        QList<QPointF> polygon;
        bool operator==(const CutInput &) const = default;
    };
    QList<QPointF> referencePolygon; // Empty when all events count
    int parts = DefaultSplitParts;
    QList<CutInput> cuts; // Shown cuts in the order of the measurement's cuts
    bool operator==(const SplitInputs &) const = default;
};

SplitInputs splitInputs(const Measurement &measurement, const SplitAnalysis &split);

// Line fitted to the counts of the parts against the middle of the part in % of the reference counts
struct SplitFit {
    double mean = 0; // Counts per part
    double intercept = 0; // Counts at 0 %
    double slope = 0; // Counts per %
    double slopeUncertainty = 0; // Standard deviation
    // Change over the whole measurement (0...100 %) in % of the mean, 100 * (100 % * slope) / mean, and its uncertainty
    double relativeChange() const { return mean > 0 ? 100 * 100 * slope / mean : 0; }
    double relativeChangeUncertainty() const { return mean > 0 ? 100 * 100 * slopeUncertainty / mean : 0; }
};

struct SplitCounts {
    int cutId = 0;
    std::vector<size_t> counts; // Of each part
    size_t total = 0;
    SplitFit fit;
};

struct SplitResult {
    size_t referenceCounts = 0;
    int parts = 0;
    std::vector<SplitCounts> selections; // In the order of the inputs
    QString errorMessage; // Empty when the analysis could be calculated
    bool isValid() const { return errorMessage.isEmpty(); }
    double partMiddle(int part) const { return (part + 0.5) * 100 / parts; } // %
};

SplitResult calculateSplit(const std::vector<ToFEEvent> &events, const SplitInputs &inputs);

#endif // JARA_SPLIT_H
