/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#include "archive.h"

#include <algorithm>
#include <cstring>

#include <QFile>
#include <QFileInfo>

#include <zlib.h>

namespace {
constexpr int TarBlock = 512;
constexpr qint64 ReadChunk = 1 << 20;
constexpr int OutputChunk = 1 << 16;
constexpr quint64 MaxTarSize = 077777777777; // 11 octal digits
constexpr quint64 MaxZipSize = 0xffffffff; // Larger ones would need Zip64
constexpr quint16 ZipVersion = 20; // 2.0, deflate
constexpr quint16 ZipFlags = 0x0808; // Sizes and CRC in a data descriptor after the data, UTF-8 names
constexpr quint16 ZipDeflate = 8;

void append16(QByteArray &data, quint16 value) {
    data.append(static_cast<char>(value & 0xff));
    data.append(static_cast<char>(value >> 8));
}

void append32(QByteArray &data, quint32 value) {
    append16(data, static_cast<quint16>(value & 0xffff));
    append16(data, static_cast<quint16>(value >> 16));
}

// Octal number filling a field of a tar header, with a terminating NUL
void setOctal(char *field, int length, quint64 value) {
    const QByteArray digits = QByteArray::number(value, 8).rightJustified(length - 1, '0');
    memcpy(field, digits.constData(), static_cast<size_t>(length - 1));
    field[length - 1] = '\0';
}
}

class ArchiveWriter::Deflater {
public:
    // 15 + 16 makes a gzip stream, -15 raw deflate data for zip
    explicit Deflater(int windowBits) {
        m_valid = deflateInit2(&m_stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, windowBits, 8, Z_DEFAULT_STRATEGY) == Z_OK;
    }
    ~Deflater() {
        if(m_valid) {
            deflateEnd(&m_stream);
        }
    }
    Deflater(const Deflater &) = delete;
    Deflater &operator=(const Deflater &) = delete;
    bool isValid() const { return m_valid; }

    // Appends the compressed output to out. With finish, the rest of the stream is written.
    bool deflate(const char *data, qint64 size, bool finish, QByteArray &out) {
        m_stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data));
        m_stream.avail_in = static_cast<uInt>(size);
        char buffer[OutputChunk];
        while(true) {
            m_stream.next_out = reinterpret_cast<Bytef *>(buffer);
            m_stream.avail_out = OutputChunk;
            const int result = ::deflate(&m_stream, finish ? Z_FINISH : Z_NO_FLUSH);
            if(result == Z_STREAM_ERROR) {
                return false;
            }
            out.append(buffer, OutputChunk - static_cast<qsizetype>(m_stream.avail_out));
            if(finish ? result == Z_STREAM_END : m_stream.avail_out != 0) {
                return true;
            }
        }
    }

private:
    z_stream m_stream{};
    bool m_valid = false;
};

ArchiveWriter::ArchiveWriter(const QString &fileName, Format format) : m_file(fileName), m_format(format) {}

ArchiveWriter::~ArchiveWriter() = default;

ArchiveWriter::Format ArchiveWriter::formatOf(const QString &fileName) {
    return fileName.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive) ? Format::Zip : Format::TarGz;
}

bool ArchiveWriter::open() {
    if(!m_file.open(QIODevice::WriteOnly)) {
        m_errorString = m_file.errorString();
        return false;
    }
    if(m_format == Format::TarGz) {
        m_deflater = std::make_unique<Deflater>(15 + 16);
        if(!m_deflater->isValid()) {
            return fail(QStringLiteral("Could not start compressing."));
        }
    }
    return true;
}

bool ArchiveWriter::fail(const QString &message) {
    m_errorString = message;
    m_file.cancelWriting();
    return false;
}

bool ArchiveWriter::writeRaw(const QByteArray &data) {
    if(m_file.write(data) != data.size()) {
        return fail(m_file.errorString());
    }
    m_written += static_cast<quint64>(data.size());
    return true;
}

bool ArchiveWriter::writeTarHeader(const QByteArray &name, quint64 size, const QDateTime &modified, char type) {
    char header[TarBlock] = {};
    memcpy(header, name.constData(), static_cast<size_t>(std::min<qsizetype>(name.size(), 100)));
    setOctal(header + 100, 8, 0644); // Mode
    setOctal(header + 108, 8, 0); // Owner
    setOctal(header + 116, 8, 0); // Group
    setOctal(header + 124, 12, size);
    setOctal(header + 136, 12, static_cast<quint64>(std::max<qint64>(0, modified.toSecsSinceEpoch())));
    header[156] = type;
    memcpy(header + 257, "ustar", 6);
    memcpy(header + 263, "00", 2);
    // The checksum is counted with its own field as spaces
    memset(header + 148, ' ', 8);
    unsigned int checksum = 0;
    for(const char c : header) {
        checksum += static_cast<unsigned char>(c);
    }
    setOctal(header + 148, 7, checksum);
    header[155] = ' ';
    QByteArray out;
    if(!m_deflater->deflate(header, TarBlock, false, out)) {
        return fail(QStringLiteral("Could not compress the archive."));
    }
    return writeRaw(out);
}

bool ArchiveWriter::beginEntry(const QString &archivePath, quint64 size, const QDateTime &modified) {
    const QByteArray name = archivePath.toUtf8();
    m_entrySize = 0;
    if(m_format == Format::TarGz) {
        if(size > MaxTarSize) {
            return fail(QString("%1 is too large for a tar archive.").arg(archivePath));
        }
        if(name.size() > 100) {
            // A pax extended header holds the full name, the length of a record includes its own digits
            const QByteArray field = " path=" + name + "\n";
            qsizetype length = field.size() + 1;
            while(QByteArray::number(length).size() + field.size() != length) {
                length = QByteArray::number(length).size() + field.size();
            }
            const QByteArray record = QByteArray::number(length) + field;
            if(!writeTarHeader("PaxHeader", static_cast<quint64>(record.size()), modified, 'x')) {
                return false;
            }
            if(!writeEntryData(record.constData(), record.size()) || !endEntry()) {
                return false;
            }
            m_entrySize = 0; // Of the entry itself from here on
        }
        return writeTarHeader(name.left(100), size, modified);
    }
    if(size > MaxZipSize) {
        return fail(QString("%1 is too large for a zip archive, use .tar.gz instead.").arg(archivePath));
    }
    // DOS time in local time, with two second resolution
    const QDateTime local = modified.toLocalTime();
    const QDate date = local.date();
    const QTime time = local.time();
    ZipEntry entry;
    entry.name = name;
    entry.offset = m_written;
    entry.dosDate = static_cast<quint16>(((std::max(date.year(), 1980) - 1980) << 9) | (date.month() << 5) | date.day());
    entry.dosTime = static_cast<quint16>((time.hour() << 11) | (time.minute() << 5) | (time.second() / 2));
    m_zipEntries.append(entry);
    QByteArray header;
    append32(header, 0x04034b50);
    append16(header, ZipVersion);
    append16(header, ZipFlags);
    append16(header, ZipDeflate);
    append16(header, entry.dosTime);
    append16(header, entry.dosDate);
    append32(header, 0); // CRC and sizes come after the data
    append32(header, 0);
    append32(header, 0);
    append16(header, static_cast<quint16>(name.size()));
    append16(header, 0); // Extra field
    header.append(name);
    m_deflater = std::make_unique<Deflater>(-15);
    if(!m_deflater->isValid()) {
        return fail(QStringLiteral("Could not start compressing."));
    }
    return writeRaw(header);
}

bool ArchiveWriter::writeEntryData(const char *data, qint64 size) {
    if(m_format == Format::Zip) {
        ZipEntry &entry = m_zipEntries.last();
        entry.crc = static_cast<quint32>(crc32(entry.crc, reinterpret_cast<const Bytef *>(data), static_cast<uInt>(size)));
        entry.size += static_cast<quint64>(size);
    }
    m_entrySize += static_cast<quint64>(size);
    QByteArray out;
    if(!m_deflater->deflate(data, size, false, out)) {
        return fail(QStringLiteral("Could not compress the archive."));
    }
    return writeRaw(out);
}

bool ArchiveWriter::endEntry() {
    if(m_format == Format::TarGz) {
        // Padded to a full block
        const quint64 padding = (TarBlock - m_entrySize % TarBlock) % TarBlock;
        const char zeros[TarBlock] = {};
        QByteArray out;
        if(!m_deflater->deflate(zeros, static_cast<qint64>(padding), false, out)) {
            return fail(QStringLiteral("Could not compress the archive."));
        }
        return writeRaw(out);
    }
    QByteArray out;
    if(!m_deflater->deflate(nullptr, 0, true, out) || !writeRaw(out)) {
        return m_errorString.isEmpty() ? fail(QStringLiteral("Could not compress the archive.")) : false;
    }
    ZipEntry &entry = m_zipEntries.last();
    entry.compressedSize = m_written - entry.offset - 30 - static_cast<quint64>(entry.name.size());
    if(entry.compressedSize > MaxZipSize) {
        return fail(QString("%1 is too large for a zip archive, use .tar.gz instead.").arg(QString::fromUtf8(entry.name)));
    }
    QByteArray descriptor;
    append32(descriptor, 0x08074b50);
    append32(descriptor, entry.crc);
    append32(descriptor, static_cast<quint32>(entry.compressedSize));
    append32(descriptor, static_cast<quint32>(entry.size));
    return writeRaw(descriptor);
}

bool ArchiveWriter::addFile(const QString &archivePath, const QString &sourcePath) {
    QFile file(sourcePath);
    if(!file.open(QIODevice::ReadOnly)) {
        return fail(QString("Could not read %1:\n%2").arg(sourcePath, file.errorString()));
    }
    const auto size = static_cast<quint64>(file.size());
    if(!beginEntry(archivePath, size, QFileInfo(file).lastModified())) {
        return false;
    }
    QByteArray buffer;
    while(!file.atEnd()) {
        buffer = file.read(ReadChunk);
        if(buffer.isEmpty() && file.error() != QFile::NoError) {
            return fail(QString("Could not read %1:\n%2").arg(sourcePath, file.errorString()));
        }
        if(!writeEntryData(buffer.constData(), buffer.size())) {
            return false;
        }
    }
    // A tar header has the size in advance
    if(m_entrySize != size) {
        return fail(QString("%1 changed while it was archived.").arg(sourcePath));
    }
    return endEntry();
}

bool ArchiveWriter::addData(const QString &archivePath, const QByteArray &data, const QDateTime &modified) {
    return beginEntry(archivePath, static_cast<quint64>(data.size()), modified) && writeEntryData(data.constData(), data.size())
           && endEntry();
}

bool ArchiveWriter::close() {
    if(m_format == Format::TarGz) {
        // Two empty blocks end a tar archive
        const char zeros[2 * TarBlock] = {};
        QByteArray out;
        if(!m_deflater->deflate(zeros, sizeof zeros, false, out) || !m_deflater->deflate(nullptr, 0, true, out)) {
            return fail(QStringLiteral("Could not compress the archive."));
        }
        if(!writeRaw(out)) {
            return false;
        }
    } else {
        const quint64 directoryOffset = m_written;
        QByteArray directory;
        for(const ZipEntry &entry : m_zipEntries) {
            append32(directory, 0x02014b50);
            append16(directory, ZipVersion); // Made by
            append16(directory, ZipVersion); // Needed
            append16(directory, ZipFlags);
            append16(directory, ZipDeflate);
            append16(directory, entry.dosTime);
            append16(directory, entry.dosDate);
            append32(directory, entry.crc);
            append32(directory, static_cast<quint32>(entry.compressedSize));
            append32(directory, static_cast<quint32>(entry.size));
            append16(directory, static_cast<quint16>(entry.name.size()));
            append16(directory, 0); // Extra field
            append16(directory, 0); // Comment
            append16(directory, 0); // Disk
            append16(directory, 0); // Internal attributes
            append32(directory, 0); // External attributes
            append32(directory, static_cast<quint32>(entry.offset));
            directory.append(entry.name);
        }
        if(directoryOffset + static_cast<quint64>(directory.size()) > MaxZipSize || m_zipEntries.size() > 0xffff) {
            return fail(QStringLiteral("The archive is too large for a zip file, use .tar.gz instead."));
        }
        const auto entries = static_cast<quint16>(m_zipEntries.size());
        QByteArray end;
        append32(end, 0x06054b50);
        append16(end, 0); // Disk
        append16(end, 0); // Disk of the directory
        append16(end, entries);
        append16(end, entries);
        append32(end, static_cast<quint32>(directory.size()));
        append32(end, static_cast<quint32>(directoryOffset));
        append16(end, 0); // Comment
        if(!writeRaw(directory + end)) {
            return false;
        }
    }
    if(!m_file.commit()) {
        m_errorString = m_file.errorString();
        return false;
    }
    return true;
}
