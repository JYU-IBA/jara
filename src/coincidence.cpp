#include "coincidence.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>

#include <QSaveFile>

namespace {
const char *skipBlanks(const char *p, const char *end) {
    while(p < end && (*p == ' ' || *p == '\t' || *p == '\r')) {
        p++;
    }
    return p;
}

// An integer followed by a blank or the end of the line, nullptr when there is none
template<typename T> const char *parseInteger(const char *p, const char *end, T &value) {
    const auto result = std::from_chars(p, end, value);
    if(result.ec != std::errc() || (result.ptr < end && *result.ptr != ' ' && *result.ptr != '\t' && *result.ptr != '\r')) {
        return nullptr;
    }
    return result.ptr;
}

// Exactly three integers: ADC, channel and timestamp
bool parseSignal(const char *p, const char *end, ListEvent &event) {
    p = skipBlanks(p, end);
    if(!(p = parseInteger(p, end, event.adc))) {
        return false;
    }
    p = skipBlanks(p, end);
    if(!(p = parseInteger(p, end, event.channel))) {
        return false;
    }
    p = skipBlanks(p, end);
    if(!(p = parseInteger(p, end, event.timestamp))) {
        return false;
    }
    return skipBlanks(p, end) == end;
}

// Buffered writing of coincidences, one per line
class AscWriter {
public:
    explicit AscWriter(const QString &fileName) : m_file(fileName) { m_buffer.reserve(BufferSize + 64); }
    bool open() { return m_file.open(QIODevice::WriteOnly); }
    void write(const CoincidenceEvent &event) {
        append(event.tof, ' ');
        append(event.energy, ' ');
        append(event.timeDifference, '\n');
        if(m_buffer.size() >= BufferSize) {
            m_file.write(m_buffer);
            m_buffer.clear();
        }
    }
    bool commit() {
        m_file.write(m_buffer);
        return m_file.commit();
    }
    QString errorString() const { return m_file.errorString(); }

private:
    static constexpr qsizetype BufferSize = 1 << 20;
    template<typename T> void append(T value, char separator) {
        char number[24];
        const auto result = std::to_chars(number, number + sizeof(number), value);
        m_buffer.append(number, result.ptr - number);
        m_buffer.append(separator);
    }

    QSaveFile m_file;
    QByteArray m_buffer;
};
}

QString coincidencePairingName(CoincidencePairing pairing) {
    switch(pairing) {
        case CoincidencePairing::ClosestToCentre:
            return QStringLiteral("One-to-one, closest to the window centre");
        case CoincidencePairing::DiscardAmbiguous:
            return QStringLiteral("Discard ambiguous");
        case CoincidencePairing::AllCombinations:
            return QStringLiteral("All combinations");
    }
    return {};
}

QString coincidenceText(const CoincidenceSettings &settings) {
    auto adc = [](int number) { return number >= 0 ? QString::number(number) : QStringLiteral("not set"); };
    auto tick = [](qint64 value) { return value < 0 ? QString("−%1").arg(-value) : QString::number(value); };
    QString pairing;
    switch(settings.pairing) {
        case CoincidencePairing::ClosestToCentre:
            pairing = QStringLiteral("one-to-one");
            break;
        case CoincidencePairing::DiscardAmbiguous:
            pairing = QStringLiteral("ambiguous discarded");
            break;
        case CoincidencePairing::AllCombinations:
            pairing = QStringLiteral("all combinations");
            break;
    }
    return QString("ToF ADC %1, E ADC %2, %3 triggers, window %4…%5 ticks, %6")
            .arg(adc(settings.tofAdc), adc(settings.energyAdc), settings.triggerIsEnergy ? "E" : "ToF",
                 tick(settings.windowLow), tick(settings.windowHigh), pairing);
}

QString coincidencePairingId(CoincidencePairing pairing) {
    switch(pairing) {
        case CoincidencePairing::ClosestToCentre:
            return QStringLiteral("closestToCentre");
        case CoincidencePairing::DiscardAmbiguous:
            return QStringLiteral("discardAmbiguous");
        case CoincidencePairing::AllCombinations:
            return QStringLiteral("allCombinations");
    }
    return {};
}

CoincidencePairing coincidencePairingFromId(const QString &id, CoincidencePairing fallback) {
    for(CoincidencePairing pairing : CoincidencePairings) {
        if(coincidencePairingId(pairing) == id) {
            return pairing;
        }
    }
    return fallback;
}

EvntReader::EvntReader(const QString &fileName, const CoincidenceSettings &settings, qint64 chunkSize)
    : m_file(fileName), m_settings(settings), m_chunkSize(std::max<qint64>(chunkSize, 1)) {}

bool EvntReader::open() {
    if(!m_file.open(QIODevice::ReadOnly)) {
        m_error = m_file.errorString();
        return false;
    }
    m_size = m_file.size();
    return true;
}

bool EvntReader::next(ListEvent &event) {
    while(m_ready.empty() && !m_atEnd && !failed()) {
        readChunk();
    }
    if(m_ready.empty() && m_atEnd && !failed()) {
        while(!m_buffer.empty()) {
            m_ready.push_back(m_buffer.top().event);
            m_buffer.pop();
        }
    }
    if(m_ready.empty() || failed()) {
        return false;
    }
    event = m_ready.front();
    m_ready.pop_front();
    return true;
}

void EvntReader::readChunk() {
    QByteArray chunk = m_partialLine + m_file.read(m_chunkSize);
    if(m_file.error() != QFileDevice::NoError) {
        m_error = m_file.errorString();
        return;
    }
    m_bytesRead = m_file.pos();
    m_atEnd = m_file.atEnd();
    const char *p = chunk.constData();
    const char *end = p + chunk.size();
    while(p < end) {
        const char *lineEnd = std::find(p, end, '\n');
        if(lineEnd == end && !m_atEnd) {
            break; // Completed by the next chunk
        }
        parseLine(p, lineEnd);
        p = lineEnd + 1;
    }
    m_partialLine = p < end ? QByteArray(p, end - p) : QByteArray();
    if(m_progress && !m_progress(m_bytesRead, m_size)) {
        m_error = QStringLiteral("Cancelled.");
    }
}

void EvntReader::parseLine(const char *begin, const char *end) {
    ListEvent event{};
    if(!parseSignal(begin, end, event)) {
        if(skipBlanks(begin, end) != end) {
            m_ignoredLines++;
        }
        return;
    }
    if(event.adc != m_settings.tofAdc && event.adc != m_settings.energyAdc) {
        return;
    }
    if(m_released && event.timestamp < m_lastReleased) {
        m_outOfOrder++;
        return;
    }
    m_newest = std::max(m_newest, event.timestamp);
    m_buffer.push({event, m_sequence++});
    // The oldest signals are released once no signal within the reorder span can come before them
    while(!m_buffer.empty() && m_buffer.top().event.timestamp < m_newest - m_settings.reorderSpan) {
        m_lastReleased = m_buffer.top().event.timestamp;
        m_released = true;
        m_ready.push_back(m_buffer.top().event);
        m_buffer.pop();
    }
}

CoincidenceFinder::CoincidenceFinder(const CoincidenceSettings &settings, Output output)
    : m_settings(settings), m_output(std::move(output)),
      m_triggerAdc(settings.triggerIsEnergy ? settings.energyAdc : settings.tofAdc),
      m_partnerAdc(settings.triggerIsEnergy ? settings.tofAdc : settings.energyAdc) {}

void CoincidenceFinder::add(const ListEvent &event) {
    if(event.adc == m_triggerAdc) {
        m_triggers.push_back({event.timestamp, event.channel, m_nextTriggerId++});
        m_statistics.triggers++;
    } else if(event.adc == m_partnerAdc) {
        m_partners.push_back({event.timestamp, event.channel, m_nextPartnerId++});
        m_statistics.partners++;
    } else {
        return;
    }
    processTriggers(event.timestamp, false);
}

void CoincidenceFinder::finish() {
    processTriggers(0, true);
    resolveBlock();
}

void CoincidenceFinder::processTriggers(qint64 now, bool all) {
    // All partners of a trigger have arrived once the time is past its window
    while(!m_triggers.empty() && (all || m_triggers.front().timestamp + m_settings.windowHigh < now)) {
        processTrigger(m_triggers.front());
        m_triggers.pop_front();
    }
    // Later triggers are not earlier than the oldest one waiting, or than now
    const qint64 oldest = m_triggers.empty() ? now : m_triggers.front().timestamp;
    while(!all && !m_partners.empty() && m_partners.front().timestamp < oldest + m_settings.windowLow) {
        m_partners.pop_front();
    }
}

void CoincidenceFinder::processTrigger(const Signal &trigger) {
    // Pairs of this and later triggers start no earlier than this. A block that ends before it is complete.
    if(!m_block.empty() && trigger.timestamp + std::min<qint64>(m_settings.windowLow, 0) > m_blockEnd) {
        resolveBlock();
    }
    const qint64 low = trigger.timestamp + m_settings.windowLow;
    const qint64 high = trigger.timestamp + m_settings.windowHigh;
    auto it = std::lower_bound(m_partners.begin(), m_partners.end(), low,
                               [](const Signal &signal, qint64 time) { return signal.timestamp < time; });
    size_t count = 0;
    for(; it != m_partners.end() && it->timestamp <= high; ++it) {
        m_block.push_back({trigger, *it});
        m_blockEnd = std::max({m_blockEnd, trigger.timestamp, it->timestamp});
        count++;
    }
    m_statistics.candidatePairs += count;
    if(count > 1) {
        m_statistics.ambiguousTriggers++;
    }
}

void CoincidenceFinder::resolveBlock() {
    if(m_block.empty()) {
        return;
    }
    std::unordered_map<quint64, int> pairsOfTrigger;
    std::unordered_map<quint64, int> pairsOfPartner;
    for(const Pair &pair : m_block) {
        pairsOfTrigger[pair.trigger.id]++;
        pairsOfPartner[pair.partner.id]++;
    }
    m_statistics.ambiguousPartners += std::count_if(pairsOfPartner.begin(), pairsOfPartner.end(),
                                                    [](const auto &entry) { return entry.second > 1; });

    switch(m_settings.pairing) {
        case CoincidencePairing::AllCombinations:
            break;
        case CoincidencePairing::DiscardAmbiguous:
            std::erase_if(m_block, [&](const Pair &pair) {
                return pairsOfTrigger[pair.trigger.id] != 1 || pairsOfPartner[pair.partner.id] != 1;
            });
            break;
        case CoincidencePairing::ClosestToCentre: {
            // Twice the distance from the centre stays an integer. Equal distances keep the time order.
            auto distance = [this](const Pair &pair) {
                const qint64 difference = pair.partner.timestamp - pair.trigger.timestamp;
                return std::abs(2 * difference - m_settings.windowLow - m_settings.windowHigh);
            };
            std::stable_sort(m_block.begin(), m_block.end(), [&](const Pair &a, const Pair &b) { return distance(a) < distance(b); });
            std::unordered_set<quint64> usedTriggers;
            std::unordered_set<quint64> usedPartners;
            std::erase_if(m_block, [&](const Pair &pair) {
                if(usedTriggers.contains(pair.trigger.id) || usedPartners.contains(pair.partner.id)) {
                    return true;
                }
                usedTriggers.insert(pair.trigger.id);
                usedPartners.insert(pair.partner.id);
                return false;
            });
            std::sort(m_block.begin(), m_block.end(), [](const Pair &a, const Pair &b) { return a.trigger.id < b.trigger.id; });
            break;
        }
    }

    for(const Pair &pair : m_block) {
        const Signal &tof = m_settings.triggerIsEnergy ? pair.partner : pair.trigger;
        const Signal &energy = m_settings.triggerIsEnergy ? pair.trigger : pair.partner;
        m_output({tof.channel, energy.channel, pair.partner.timestamp - pair.trigger.timestamp});
        m_statistics.events++;
    }
    m_block.clear();
    m_blockEnd = 0;
}

bool loadEvntFile(const QString &fileName, const CoincidenceSettings &settings, size_t maxSignals, EvntPreview &preview,
                  QString &errorMessage, const ReadProgress &progress) {
    preview = {};
    EvntReader reader(fileName, settings);
    reader.setProgress(progress);
    if(!reader.open()) {
        errorMessage = reader.errorString();
        return false;
    }
    ListEvent event;
    while(preview.events.size() < maxSignals && reader.next(event)) {
        preview.events.push_back(event);
    }
    if(reader.failed()) {
        errorMessage = reader.errorString();
        return false;
    }
    preview.truncated = preview.events.size() >= maxSignals && reader.next(event);
    preview.ignoredLines = reader.ignoredLines();
    preview.outOfOrder = reader.outOfOrder();
    preview.fractionRead = preview.truncated && reader.size() > 0 ? static_cast<double>(reader.bytesRead()) / reader.size() : 1;
    return true;
}

std::vector<CoincidenceEvent> findCoincidences(std::span<const ListEvent> events, const CoincidenceSettings &settings,
                                               CoincidenceStatistics &statistics) {
    std::vector<CoincidenceEvent> coincidences;
    CoincidenceFinder finder(settings, [&coincidences](const CoincidenceEvent &event) { coincidences.push_back(event); });
    for(const ListEvent &event : events) {
        finder.add(event);
    }
    finder.finish();
    statistics = finder.statistics();
    return coincidences;
}

bool convertEvntFile(const QString &evntPath, const QString &ascPath, const CoincidenceSettings &settings,
                     ConversionResult &result, QString &errorMessage, const ReadProgress &progress, qint64 chunkSize) {
    result = {};
    EvntReader reader(evntPath, settings, chunkSize);
    reader.setProgress(progress);
    if(!reader.open()) {
        errorMessage = reader.errorString();
        return false;
    }
    AscWriter writer(ascPath);
    if(!writer.open()) {
        errorMessage = writer.errorString();
        return false;
    }
    CoincidenceFinder finder(settings, [&writer](const CoincidenceEvent &event) { writer.write(event); });
    ListEvent event;
    while(reader.next(event)) {
        finder.add(event);
    }
    if(reader.failed()) {
        errorMessage = reader.errorString();
        return false; // The output file is not replaced
    }
    finder.finish();
    if(!writer.commit()) {
        errorMessage = writer.errorString();
        return false;
    }
    result.statistics = finder.statistics();
    result.ignoredLines = reader.ignoredLines();
    result.outOfOrder = reader.outOfOrder();
    return true;
}
