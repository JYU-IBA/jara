#ifndef JARA_ARCHIVE_H
#define JARA_ARCHIVE_H

#include <memory>

#include <QDateTime>
#include <QList>
#include <QSaveFile>
#include <QString>

// Writes .tar.gz and .zip archives, compressing with zlib as the files are read, so large files need not fit in
// memory. Paths in the archive use forward slashes and are stored in UTF-8. The archive file is only replaced when
// close() succeeds.
class ArchiveWriter {
public:
    enum class Format { TarGz, Zip };

    ArchiveWriter(const QString &fileName, Format format);
    ~ArchiveWriter();
    bool open();
    bool addFile(const QString &archivePath, const QString &sourcePath);
    bool addData(const QString &archivePath, const QByteArray &data, const QDateTime &modified);
    bool close();
    const QString &errorString() const { return m_errorString; }

    // Format from the suffix of the file name, .zip or anything else for .tar.gz
    static Format formatOf(const QString &fileName);

private:
    class Deflater;
    struct ZipEntry {
        QByteArray name;
        quint32 crc = 0;
        quint64 compressedSize = 0;
        quint64 size = 0;
        quint64 offset = 0;
        quint16 dosTime = 0;
        quint16 dosDate = 0;
    };

    bool beginEntry(const QString &archivePath, quint64 size, const QDateTime &modified);
    bool writeEntryData(const char *data, qint64 size);
    bool endEntry();
    bool writeTarHeader(const QByteArray &name, quint64 size, const QDateTime &modified, char type = '0');
    bool writeRaw(const QByteArray &data); // To the file, not compressed
    bool fail(const QString &message);

    QSaveFile m_file;
    Format m_format;
    std::unique_ptr<Deflater> m_deflater; // Of the whole tar stream, or of the current zip entry
    QList<ZipEntry> m_zipEntries;
    quint64 m_written = 0; // Bytes written to the file
    quint64 m_entrySize = 0; // Bytes of the current entry, which pads a tar entry to a full block
    QString m_errorString;
};

#endif // JARA_ARCHIVE_H
