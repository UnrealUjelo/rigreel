// Decompression (deflate, zstd) and the archive decryption RE Engine uses for its file index and some resources.
// The keys are the public halves published by the community tools (REasy, REtool); they only unlock files of a
// game the user has installed.
#pragma once

#include <QByteArray>

namespace re {

bool zstdAvailable();
QByteArray zstdDecompress(const char *src, qint64 size, qint64 expected);
QByteArray inflateAny(const char *src, qint64 size, qint64 expected);     // zlib header or raw deflate

void decryptEntryTable(QByteArray &table, const QByteArray &encryptedKey128);
QByteArray decryptResource(const QByteArray &data);                        // returns the plain bytes

} // namespace re
