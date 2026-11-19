#include "tofedata.h"

#include <algorithm>
#include <charconv>

#include <QFile>

namespace {
const char *skipWhitespace(const char *p, const char *end) {
    while(p < end && (*p == ' ' || *p == '\t' || *p == ',')) {
        p++;
    }
    return p;
}

// std::from_chars is locale independent, unlike strtod
const char *parseNumber(const char *p, const char *end, double &value) {
    p = skipWhitespace(p, end);
    if(p < end && *p == '+') {
        p++;
    }
    const auto result = std::from_chars(p, end, value);
    return result.ec == std::errc() ? result.ptr : nullptr;
}
}

bool loadAscFile(const QString &fileName, std::vector<ToFEEvent> &events, QString &errorMessage) {
    QFile file(fileName);
    if(!file.open(QIODevice::ReadOnly)) {
        errorMessage = file.errorString();
        return false;
    }
    events.clear();
    const QByteArray data = file.readAll();
    const char *p = data.constData();
    const char *dataEnd = p + data.size();
    int lineNumber = 0;
    while(p < dataEnd) {
        lineNumber++;
        const char *lineEnd = std::find(p, dataEnd, '\n');
        const char *start = skipWhitespace(p, lineEnd);
        if(start < lineEnd && *start != '\r' && *start != '#') {
            ToFEEvent event{};
            const char *next = parseNumber(start, lineEnd, event.tof);
            if(next) {
                next = parseNumber(next, lineEnd, event.energy);
            }
            if(!next) {
                errorMessage = QString("Line %1: expected ToF and E channels in the first two columns.").arg(lineNumber);
                return false;
            }
            events.push_back(event);
        }
        p = lineEnd + 1;
    }
    return true;
}

size_t countEventsIn(const std::vector<ToFEEvent> &events, const QList<QPointF> &polygon) {
    size_t count = 0;
    forEachEventIn(events, polygon, [&count](const ToFEEvent &) { count++; });
    return count;
}
