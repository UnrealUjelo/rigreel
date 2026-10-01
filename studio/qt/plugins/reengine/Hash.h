// RE Engine hashes: MurmurHash3 x86_32 with seed 0xFFFFFFFF, over UTF-16LE text.
// Archive entries are keyed by (hash(UPPER path) << 32 | hash(lower path)); bones, fields and material
// parameters by hash(name).
#pragma once

#include <QString>
#include <cstring>

namespace re {

inline quint32 murmur3(const void *key, qsizetype len, quint32 seed = 0xFFFFFFFFu)
{
    const auto *data = static_cast<const quint8 *>(key);
    const qsizetype nblocks = len / 4;
    quint32 h1 = seed;
    const quint32 c1 = 0xcc9e2d51, c2 = 0x1b873593;
    auto rotl = [](quint32 x, int r) { return (x << r) | (x >> (32 - r)); };
    for (qsizetype i = 0; i < nblocks; ++i) {
        quint32 k1;
        std::memcpy(&k1, data + i * 4, 4);
        k1 *= c1; k1 = rotl(k1, 15); k1 *= c2;
        h1 ^= k1; h1 = rotl(h1, 13); h1 = h1 * 5 + 0xe6546b64;
    }
    const quint8 *tail = data + nblocks * 4;
    quint32 k1 = 0;
    switch (len & 3) {
    case 3: k1 ^= quint32(tail[2]) << 16; [[fallthrough]];
    case 2: k1 ^= quint32(tail[1]) << 8; [[fallthrough]];
    case 1: k1 ^= tail[0]; k1 *= c1; k1 = rotl(k1, 15); k1 *= c2; h1 ^= k1;
    }
    h1 ^= quint32(len);
    h1 ^= h1 >> 16; h1 *= 0x85ebca6b; h1 ^= h1 >> 13; h1 *= 0xc2b2ae35; h1 ^= h1 >> 16;
    return h1;
}

inline quint32 hashWide(const QString &s) { return murmur3(s.utf16(), s.size() * 2); }
inline quint32 hashAscii(const QString &s) { const QByteArray a = s.toLatin1(); return murmur3(a.constData(), a.size()); }

inline QString normalizePath(QString p)
{
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (p.contains(QLatin1String("//"))) p.replace(QLatin1String("//"), QLatin1String("/"));
    if (p.startsWith(QLatin1Char('/'))) p.remove(0, 1);
    return p;
}

inline quint64 pathHash(const QString &path)
{
    const QString p = normalizePath(path);
    const quint64 lo = hashWide(p.toLower());
    const quint64 hi = hashWide(p.toUpper());
    return (hi << 32) | lo;
}

} // namespace re
