// The game's file system: every .pak archive of an install (base chunk, patches, DLC) merged into one index,
// with loose files under <game>/natives taking precedence (mods). Archives only store path hashes; a name list
// (one path per line, from the community lists) gives them back their paths for browsing.
#pragma once

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

namespace re {

class PakFileSystem {
public:
    PakFileSystem();
    ~PakFileSystem();

    bool open(const QString &gameFolder, QString *error);
    bool loadNames(const QString &listFile, QString *error);

    bool exists(const QString &path) const;
    QByteArray read(const QString &path, QString *error = nullptr) const;
    QByteArray readHash(quint64 hash, QString *error = nullptr) const;

    // Known paths (from the name list) that exist in the archives, sorted.
    const QStringList &names() const { return m_sorted; }
    QStringList list(const QString &prefix, const QString &suffix = {}, int limit = -1) const;
    QString nameOf(quint64 hash) const { return m_names.value(hash); }

    int archiveCount() const { return int(m_archives.size()); }
    qint64 entryCount() const { return m_entries.size(); }
    QStringList archivePaths() const;
    QString folder() const { return m_folder; }

private:
    struct Chunk { qint64 offset = 0; qint64 size = 0; };
    struct Archive {
        QString path;
        QFile file;
        uchar *map = nullptr;
        qint64 size = 0;
        QVector<Chunk> chunks;
        mutable QMutex mutex;
    };
    struct Entry {
        qint64 offset = 0, csize = 0, dsize = 0;
        quint64 attrib = 0;
        int archive = -1;
    };

    bool addArchive(const QString &path, QString *error);
    QByteArray readEntry(const Entry &e, QString *error) const;
    QByteArray rawBytes(const Archive &a, qint64 offset, qint64 size) const;

    QString m_folder;
    QString m_loose;                         // <game>/natives, if present
    std::vector<std::unique_ptr<Archive>> m_archives;
    QHash<quint64, Entry> m_entries;
    QHash<quint64, QString> m_names;
    QStringList m_sorted;
};

} // namespace re
