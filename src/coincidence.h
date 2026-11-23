/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#ifndef JARA_COINCIDENCE_H
#define JARA_COINCIDENCE_H

#include <deque>
#include <functional>
#include <queue>
#include <span>
#include <vector>

#include <QFile>
#include <QString>
#include <QtTypes>

// Coincidences of ToF and energy signals in list-mode files (*.evnt), which have one signal per line: ADC number,
// channel and timestamp in clock ticks. A coincidence is a signal of the trigger ADC and a signal of the other (partner)
// ADC whose timestamp is inside a window relative to the trigger's. All signals in the window are considered, not
// just the next few lines. Files are processed as a stream, so their length is not limited by memory.

// How triggers with several partners in their window, or partners in the window of several triggers, are paired
enum class CoincidencePairing {
    ClosestToCentre, // Each signal is used at most once, pairs closest to the centre of the window first
    DiscardAmbiguous, // Only triggers with a single partner that has no other trigger
    AllCombinations, // Every pair
};
constexpr CoincidencePairing CoincidencePairings[] = {CoincidencePairing::ClosestToCentre, CoincidencePairing::DiscardAmbiguous,
                                                      CoincidencePairing::AllCombinations};
QString coincidencePairingName(CoincidencePairing pairing); // As shown to the user
QString coincidencePairingId(CoincidencePairing pairing); // In files
CoincidencePairing coincidencePairingFromId(const QString &id, CoincidencePairing fallback);

// Coincidence parameters of a detector system, part of the detector settings. Times in clock ticks.
struct CoincidenceSettings {
    int tofAdc = -1; // ADC numbers, -1 when not set
    int energyAdc = -1;
    bool triggerIsEnergy = true; // Otherwise ToF triggers
    // Window of the partner's timestamp minus the trigger's, both ends included
    qint64 windowLow = -600;
    qint64 windowHigh = -440;
    // Signals are put in time order in a buffer this long. A signal later than this compared to newer signals in the
    // file is dropped as out of order.
    qint64 reorderSpan = 10000000;
    CoincidencePairing pairing = CoincidencePairing::ClosestToCentre;

    bool hasAdcs() const { return tofAdc >= 0 && energyAdc >= 0 && tofAdc != energyAdc; }
};

// "ToF ADC 1, E ADC 0, E triggers, window −600…−440 ticks, one-to-one", without the reorder span
QString coincidenceText(const CoincidenceSettings &settings);

struct ListEvent {
    qint64 timestamp; // Ticks
    int channel;
    int adc;
};

struct CoincidenceEvent {
    int tof; // Channels
    int energy;
    qint64 timeDifference; // Partner minus trigger, ticks
};

struct CoincidenceStatistics {
    size_t triggers = 0; // Signals of the trigger ADC
    size_t partners = 0; // Signals of the other ADC
    size_t candidatePairs = 0; // Pairs in the window
    size_t ambiguousTriggers = 0; // With more than one partner in their window
    size_t ambiguousPartners = 0; // In the window of more than one trigger
    size_t events = 0; // Coincidences found with the pairing
};

// Called after each chunk read with the bytes read and the size of the file, returns false to cancel
using ReadProgress = std::function<bool(qint64 done, qint64 total)>;

// Reads the signals of the two ADCs of a list-mode file in chunks and returns them in time order. A line with
// exactly three integers is a signal, all other lines (the header with the metadata of the measurement) are ignored.
class EvntReader {
public:
    EvntReader(const QString &fileName, const CoincidenceSettings &settings, qint64 chunkSize = 1 << 22);
    bool open(); // errorString() tells why it failed
    bool next(ListEvent &event); // false at the end of the file, on an error or when cancelled
    void setProgress(const ReadProgress &progress) { m_progress = progress; }
    bool failed() const { return !m_error.isEmpty(); }
    const QString &errorString() const { return m_error; }
    qint64 bytesRead() const { return m_bytesRead; }
    qint64 size() const { return m_size; }
    size_t ignoredLines() const { return m_ignoredLines; }
    size_t outOfOrder() const { return m_outOfOrder; } // Signals dropped, see CoincidenceSettings::reorderSpan

private:
    struct Buffered {
        ListEvent event;
        quint64 sequence; // Signals with equal timestamps stay in file order
        bool operator>(const Buffered &other) const {
            return event.timestamp != other.event.timestamp ? event.timestamp > other.event.timestamp : sequence > other.sequence;
        }
    };

    void readChunk();
    void parseLine(const char *begin, const char *end);

    QFile m_file;
    CoincidenceSettings m_settings;
    qint64 m_chunkSize;
    qint64 m_size = 0;
    qint64 m_bytesRead = 0;
    QByteArray m_partialLine; // Carried over to the next chunk
    bool m_atEnd = false;
    QString m_error;
    ReadProgress m_progress;
    std::priority_queue<Buffered, std::vector<Buffered>, std::greater<>> m_buffer;
    std::deque<ListEvent> m_ready; // Released from the buffer in time order
    quint64 m_sequence = 0;
    qint64 m_newest = 0; // Timestamp of the newest signal read
    bool m_released = false;
    qint64 m_lastReleased = 0;
    size_t m_ignoredLines = 0;
    size_t m_outOfOrder = 0;
};

// Finds coincidences in signals given in time order and passes them on in the order of the trigger timestamps. Only
// the signals that can still be paired are kept. Pairs are collected in blocks that no later pair can connect to, and
// each block is paired as a whole, which gives the same result as pairing all signals at once.
class CoincidenceFinder {
public:
    using Output = std::function<void(const CoincidenceEvent &event)>;
    CoincidenceFinder(const CoincidenceSettings &settings, Output output);
    void add(const ListEvent &event); // Signals of other ADCs are ignored
    void finish(); // After the last signal
    const CoincidenceStatistics &statistics() const { return m_statistics; }

private:
    struct Signal {
        qint64 timestamp;
        int channel;
        quint64 id; // Order of the signals of the ADC
    };
    struct Pair {
        Signal trigger;
        Signal partner;
    };

    void processTriggers(qint64 now, bool all); // Those whose window has passed, or all
    void processTrigger(const Signal &trigger);
    void resolveBlock();

    CoincidenceSettings m_settings;
    Output m_output;
    int m_triggerAdc;
    int m_partnerAdc;
    std::deque<Signal> m_triggers; // Not processed yet
    std::deque<Signal> m_partners; // That can still be in the window of a trigger
    quint64 m_nextTriggerId = 0;
    quint64 m_nextPartnerId = 0;
    std::vector<Pair> m_block;
    qint64 m_blockEnd = 0; // Latest timestamp of the signals in the block
    CoincidenceStatistics m_statistics;
};

// The first maxSignals signals of the two ADCs of a file, for a preview
struct EvntPreview {
    std::vector<ListEvent> events; // In time order
    size_t ignoredLines = 0;
    size_t outOfOrder = 0;
    bool truncated = false; // The file has more signals
    double fractionRead = 1; // Of the bytes of the file
};
bool loadEvntFile(const QString &fileName, const CoincidenceSettings &settings, size_t maxSignals, EvntPreview &preview,
                  QString &errorMessage, const ReadProgress &progress = {});

// Coincidences of signals in time order
std::vector<CoincidenceEvent> findCoincidences(std::span<const ListEvent> events, const CoincidenceSettings &settings,
                                               CoincidenceStatistics &statistics);

// Streams a list-mode file into a ToF-E event file with one coincidence per line: ToF and E channels and the time
// difference, separated by spaces. The output file is replaced only when the conversion succeeds.
struct ConversionResult {
    CoincidenceStatistics statistics;
    size_t ignoredLines = 0;
    size_t outOfOrder = 0;
};
bool convertEvntFile(const QString &evntPath, const QString &ascPath, const CoincidenceSettings &settings,
                     ConversionResult &result, QString &errorMessage, const ReadProgress &progress = {},
                     qint64 chunkSize = 1 << 22);

#endif // JARA_COINCIDENCE_H
