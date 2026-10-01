#include "Pak.h"
#include "Codec.h"
#include "Hash.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <cstring>

namespace re {

namespace {
constexpr quint32 kMagic = 0x414B504B;            // "KPKA"
constexpr quint16 kHeaderPadding = 0x04, kEntryKey = 0x08, kHeaderMarker = 0x10, kChunkTable = 0x20, kHashRemap = 0x40;
constexpr qint64 kChunkPlain = 512 * 1024;

// Priority of an archive in the merged index (higher wins).
int priorityOf(const QString &path)
{
    static const QRegularExpression patchRx(QStringLiteral("\\.patch_(\\d+)\\.pak$"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression subRx(QStringLiteral("\\.sub_(\\d+)\\.pak$"), QRegularExpression::CaseInsensitiveOption);
    const QString n = QFileInfo(path).fileName();
    if (path.contains(QLatin1String("/dlc/"), Qt::CaseInsensitive)) return 0;
    if (auto m = patchRx.match(n); m.hasMatch()) return 100000 + m.captured(1).toInt() * 10 + (n.contains(QLatin1String(".sub_")) ? 1 : 0);
    if (auto m = subRx.match(n); m.hasMatch()) return 1000 + m.captured(1).toInt();
    return 500;
}
} // namespace

PakFileSystem::PakFileSystem() = default;

PakFileSystem::~PakFileSystem()
{
    for (auto &a : m_archives)
        if (a->map) a->file.unmap(a->map);
}

bool PakFileSystem::open(const QString &gameFolder, QString *error)
{
    m_folder = QDir(gameFolder).absolutePath();
    const QString loose = m_folder + QStringLiteral("/natives");
    m_loose = QFileInfo(loose).isDir() ? loose : QString();

    QStringList paks;
    QDirIterator top(m_folder, {QStringLiteral("*.pak")}, QDir::Files);
    while (top.hasNext()) paks << top.next();
    const QString dlc = m_folder + QStringLiteral("/dlc");
    if (QFileInfo(dlc).isDir()) {
        QDirIterator it(dlc, {QStringLiteral("*.pak")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) paks << it.next();
    }
    if (paks.isEmpty()) {
        if (error) *error = QStringLiteral("no .pak archives in %1").arg(m_folder);
        return false;
    }
    // lowest priority first, so later archives overwrite earlier entries
    std::stable_sort(paks.begin(), paks.end(), [](const QString &a, const QString &b) {
        const int pa = priorityOf(a), pb = priorityOf(b);
        return pa != pb ? pa < pb : a < b;
    });
    for (const QString &p : paks) {
        QString err;
        if (!addArchive(p, &err) && error && error->isEmpty()) *error = err;   // one bad pak does not stop the rest
    }
    return !m_entries.isEmpty();
}

bool PakFileSystem::addArchive(const QString &path, QString *error)
{
    auto a = std::make_unique<Archive>();
    a->path = path;
    a->file.setFileName(path);
    if (!a->file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot open %1").arg(path);
        return false;
    }
    a->size = a->file.size();
    QFile &f = a->file;
    const QByteArray head = f.read(16);
    if (head.size() != 16) return false;
    quint32 magic, fileCount;
    quint8 major, minor;
    quint16 features;
    std::memcpy(&magic, head.constData(), 4);
    major = quint8(head[4]);
    minor = quint8(head[5]);
    std::memcpy(&features, head.constData() + 6, 2);
    std::memcpy(&fileCount, head.constData() + 8, 4);
    if (magic != kMagic || !((major == 4 && minor <= 2) || (major == 2 && minor == 0))) {
        if (error) *error = QStringLiteral("%1: unsupported archive version %2.%3").arg(path).arg(major).arg(minor);
        return false;
    }
    const int entrySize = major == 4 ? 48 : 24;
    QByteArray table = f.read(qint64(fileCount) * entrySize);
    if (table.size() != qint64(fileCount) * entrySize) return false;
    if (features & kHeaderMarker) f.skip(4);
    if (features & kHeaderPadding) f.skip(9);
    QHash<quint64, quint64> remap;                      // stored hash -> public hash
    if (features & kHashRemap) {
        quint64 count = 0;
        f.read(reinterpret_cast<char *>(&count), 8);
        const QByteArray raw = f.read(qint64(count) * 16);
        for (quint64 i = 0; i + 1 <= count && qint64(i * 16 + 16) <= raw.size(); ++i) {
            quint64 stored, pub;
            std::memcpy(&stored, raw.constData() + i * 16, 8);
            std::memcpy(&pub, raw.constData() + i * 16 + 8, 8);
            if (!remap.contains(stored)) remap.insert(stored, pub);
        }
    }
    if (features & kEntryKey) {
        const QByteArray key = f.read(128);
        decryptEntryTable(table, key);
    }
    if (features & kChunkTable) {
        qint32 hdr[2] = {0, 0};
        f.read(reinterpret_cast<char *>(hdr), 8);
        const QByteArray raw = f.read(qint64(qMax(0, hdr[1])) * 8);
        for (int i = 0; i < hdr[1] && qint64(i * 8 + 8) <= raw.size(); ++i) {
            quint32 off32, word;
            std::memcpy(&off32, raw.constData() + i * 8, 4);
            std::memcpy(&word, raw.constData() + i * 8 + 4, 4);
            a->chunks.push_back({qint64(off32) | (qint64(word & 0x3FF) << 32), qint64(word >> 10)});
        }
    }

    const int index = int(m_archives.size());
    const char *p = table.constData();
    for (quint32 i = 0; i < fileCount; ++i, p += entrySize) {
        Entry e;
        quint64 hash;
        if (major == 4) {
            quint32 lo, hi;
            std::memcpy(&lo, p, 4);
            std::memcpy(&hi, p + 4, 4);
            std::memcpy(&e.offset, p + 8, 8);
            std::memcpy(&e.csize, p + 16, 8);
            std::memcpy(&e.dsize, p + 24, 8);
            std::memcpy(&e.attrib, p + 32, 8);
            hash = (quint64(hi) << 32) | lo;
        } else {
            quint32 lo, hi;
            std::memcpy(&e.offset, p, 8);
            std::memcpy(&e.csize, p + 8, 8);
            std::memcpy(&hi, p + 16, 4);
            std::memcpy(&lo, p + 20, 4);
            e.dsize = e.csize;
            hash = (quint64(hi) << 32) | lo;
        }
        e.archive = index;
        if (!remap.isEmpty()) hash = remap.value(hash, hash);
        m_entries.insert(hash, e);
    }
    // Map the whole archive: reads become lock-free page faults. Very large maps can fail on low memory;
    // then reads go through the file with a lock.
    a->map = f.map(0, a->size);
    m_archives.push_back(std::move(a));
    return true;
}

bool PakFileSystem::loadNames(const QString &listFile, QString *error)
{
    QFile f(listFile);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot read name list %1").arg(listFile);
        return false;
    }
    const QByteArray all = f.readAll();
    m_names.clear();
    m_sorted.clear();
    qsizetype start = 0;
    while (start < all.size()) {
        qsizetype end = all.indexOf('\n', start);
        if (end < 0) end = all.size();
        qsizetype e = end;
        while (e > start && (all[e - 1] == '\r' || all[e - 1] == ' ')) --e;
        if (e > start) {
            const QString path = QString::fromUtf8(all.constData() + start, e - start);
            const quint64 h = pathHash(path);
            if (m_entries.contains(h) && !m_names.contains(h)) {
                m_names.insert(h, path);
                m_sorted << path;
            }
        }
        start = end + 1;
    }
    m_sorted.sort(Qt::CaseInsensitive);
    return true;
}

bool PakFileSystem::exists(const QString &path) const
{
    if (!m_loose.isEmpty() && QFileInfo::exists(m_folder + QLatin1Char('/') + normalizePath(path))) return true;
    return m_entries.contains(pathHash(path));
}

QByteArray PakFileSystem::read(const QString &path, QString *error) const
{
    if (!m_loose.isEmpty()) {
        QFile loose(m_folder + QLatin1Char('/') + normalizePath(path));
        if (loose.open(QIODevice::ReadOnly)) return loose.readAll();
    }
    const auto it = m_entries.constFind(pathHash(path));
    if (it == m_entries.cend()) {
        if (error) *error = QStringLiteral("not in the game files: %1").arg(path);
        return {};
    }
    return readEntry(*it, error);
}

QByteArray PakFileSystem::readHash(quint64 hash, QString *error) const
{
    const auto it = m_entries.constFind(hash);
    if (it == m_entries.cend()) {
        if (error) *error = QStringLiteral("no entry %1").arg(hash, 16, 16, QLatin1Char('0'));
        return {};
    }
    return readEntry(*it, error);
}

QByteArray PakFileSystem::rawBytes(const Archive &a, qint64 offset, qint64 size) const
{
    if (offset < 0 || size < 0 || offset + size > a.size) return {};
    if (a.map) return QByteArray(reinterpret_cast<const char *>(a.map + offset), size);
    QMutexLocker lock(&a.mutex);
    auto &f = const_cast<QFile &>(a.file);
    if (!f.seek(offset)) return {};
    return f.read(size);
}

QByteArray PakFileSystem::readEntry(const Entry &e, QString *error) const
{
    const Archive &a = *m_archives[size_t(e.archive)];
    const int compression = int(e.attrib & 0xF);
    const int encryption = int((e.attrib & 0x00FF0000) >> 16);

    // v4.2 archives split big uncompressed-flagged resources into 512 KiB zstd chunks
    if (!a.chunks.isEmpty() && compression == 0 && encryption == 0 && (e.attrib & 0x1000000) && e.offset >= 0 && e.offset < a.chunks.size()) {
        QByteArray out;
        out.reserve(e.dsize);
        qint64 remaining = e.csize;
        int id = int(e.offset);
        while (remaining > 0 && id < a.chunks.size()) {
            const Chunk &c = a.chunks[id++];
            const QByteArray payload = rawBytes(a, c.offset, c.size);
            if (c.size == kChunkPlain) out += payload;
            else out += zstdDecompress(payload.constData(), payload.size(), kChunkPlain);
            remaining -= c.size;
        }
        out.truncate(e.dsize);
        return out;
    }

    QByteArray data = rawBytes(a, e.offset, compression ? e.csize : (e.csize ? e.csize : e.dsize));
    if (data.isEmpty() && e.dsize) {
        if (error) *error = QStringLiteral("read failed in %1").arg(QFileInfo(a.path).fileName());
        return {};
    }
    if (encryption) data = decryptResource(data);
    if (compression == 1) {
        QByteArray out = inflateAny(data.constData(), data.size(), e.dsize);
        if (out.isEmpty()) out = zstdDecompress(data.constData(), data.size(), e.dsize);
        return out;
    }
    if (compression == 2) {
        QByteArray out = zstdDecompress(data.constData(), data.size(), e.dsize);
        if (out.isEmpty()) out = inflateAny(data.constData(), data.size(), e.dsize);
        if (out.isEmpty() && error) *error = zstdAvailable() ? QStringLiteral("zstd failed") : QStringLiteral("libzstd.dll missing");
        return out;
    }
    return data;
}

QStringList PakFileSystem::list(const QString &prefix, const QString &suffix, int limit) const
{
    QStringList out;
    auto it = std::lower_bound(m_sorted.cbegin(), m_sorted.cend(), prefix, [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    for (; it != m_sorted.cend(); ++it) {
        if (!it->startsWith(prefix, Qt::CaseInsensitive)) break;
        if (!suffix.isEmpty() && !it->contains(suffix, Qt::CaseInsensitive)) continue;
        out << *it;
        if (limit > 0 && out.size() >= limit) break;
    }
    return out;
}

QStringList PakFileSystem::archivePaths() const
{
    QStringList r;
    for (const auto &a : m_archives) r << a->path;
    return r;
}

} // namespace re
