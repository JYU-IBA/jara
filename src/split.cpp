#include "split.h"

#include <algorithm>
#include <cmath>

EventDivision::EventDivision(const std::vector<ToFEEvent> &events, const QList<QPointF> &referencePolygon)
    : m_events(events), m_allEvents(referencePolygon.size() < 3) {
    if(m_allEvents) {
        return;
    }
    const QPolygonF polygon(referencePolygon);
    const QRectF bounds = polygon.boundingRect();
    for(size_t i = 0; i < events.size(); i++) {
        const QPointF point(events[i].tof, events[i].energy);
        if(bounds.contains(point) && polygon.containsPoint(point, Qt::OddEvenFill)) {
            m_referenceIndices.push_back(i);
        }
    }
}

size_t EventDivision::referenceCounts() const {
    return m_allEvents ? m_events.size() : m_referenceIndices.size();
}

size_t EventDivision::eventIndex(double fraction) const {
    // The whole measurement even without reference counts
    if(fraction >= 1) {
        return m_events.size();
    }
    const size_t counts = referenceCounts();
    if(fraction <= 0 || counts == 0) {
        return 0;
    }
    const auto n = static_cast<size_t>(std::lround(fraction * static_cast<double>(counts)));
    if(n == 0) {
        return 0;
    }
    if(n >= counts) {
        return m_events.size();
    }
    return m_allEvents ? n : m_referenceIndices[n - 1] + 1;
}

size_t EventDivision::referenceCountsIn(double min, double max) const {
    const size_t counts = referenceCounts();
    auto upTo = [counts](double fraction) {
        if(fraction >= 1) {
            return counts;
        }
        return fraction <= 0 ? size_t{0} : static_cast<size_t>(std::lround(fraction * static_cast<double>(counts)));
    };
    return upTo(max) - std::min(upTo(max), upTo(min));
}

std::span<const ToFEEvent> EventDivision::range(double min, double max) const {
    const size_t first = eventIndex(min);
    const size_t last = std::max(first, eventIndex(max));
    return std::span<const ToFEEvent>(m_events).subspan(first, last - first);
}

SplitInputs splitInputs(const Measurement &measurement, const SplitAnalysis &split) {
    SplitInputs inputs;
    inputs.referencePolygon = measurement.referencePolygon();
    inputs.parts = split.parts;
    for(const Cut &cut : measurement.cuts) {
        if(split.cutIds.contains(cut.id)) {
            inputs.cuts.append({cut.id, cut.polygon});
        }
    }
    return inputs;
}

namespace {
SplitFit fitLine(const SplitResult &result, const std::vector<size_t> &counts) {
    SplitFit fit;
    const auto n = static_cast<double>(counts.size());
    double sumX = 0;
    double sumY = 0;
    for(size_t i = 0; i < counts.size(); i++) {
        sumX += result.partMiddle(static_cast<int>(i));
        sumY += static_cast<double>(counts[i]);
    }
    const double meanX = sumX / n;
    fit.mean = sumY / n;
    double sxx = 0;
    double sxy = 0;
    for(size_t i = 0; i < counts.size(); i++) {
        const double dx = result.partMiddle(static_cast<int>(i)) - meanX;
        sxx += dx * dx;
        sxy += dx * (static_cast<double>(counts[i]) - fit.mean);
    }
    fit.slope = sxx > 0 ? sxy / sxx : 0;
    fit.intercept = fit.mean - fit.slope * meanX;
    fit.slopeUncertainty = sxx > 0 ? std::sqrt(fit.mean / sxx) : 0;
    return fit;
}
}

SplitResult calculateSplit(const std::vector<ToFEEvent> &events, const SplitInputs &inputs) {
    SplitResult result;
    result.parts = inputs.parts;
    const EventDivision division(events, inputs.referencePolygon);
    result.referenceCounts = division.referenceCounts();
    if(result.referenceCounts < static_cast<size_t>(inputs.parts)) {
        result.errorMessage = QString("The reference has %1 counts, at least one for each of the %2 parts is needed.")
                                      .arg(result.referenceCounts)
                                      .arg(inputs.parts);
        return result;
    }
    std::vector<std::span<const ToFEEvent>> parts;
    for(int i = 0; i < inputs.parts; i++) {
        const double min = static_cast<double>(i) / inputs.parts;
        const double max = static_cast<double>(i + 1) / inputs.parts;
        parts.push_back(division.range(min, max));
        const size_t partCounts = division.referenceCountsIn(min, max);
        result.minPartReferenceCounts = i == 0 ? partCounts : std::min(result.minPartReferenceCounts, partCounts);
        result.maxPartReferenceCounts = std::max(result.maxPartReferenceCounts, partCounts);
    }
    for(const auto &cut : inputs.cuts) {
        SplitCounts counts;
        counts.cutId = cut.id;
        for(const auto &part : parts) {
            counts.counts.push_back(countEventsIn(part, cut.polygon));
            counts.total += counts.counts.back();
        }
        counts.fit = fitLine(result, counts.counts);
        result.selections.push_back(std::move(counts));
    }
    return result;
}
