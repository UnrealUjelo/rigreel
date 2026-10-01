// Game textures cached on disk for the renderer: block-compressed with their full mip chain, in KTX files in the
// user's cache (our own reader loads them back; GpuTexture uploads them). Conversion runs once per texture;
// later sessions load straight from the cache.
#pragma once

#include <director/GamePlugin.h>
#include <QUrl>

class TextureStore {
public:
    static QString cacheRoot();
    static QString cacheDir(const QString &gameId);
    // Converts on first use and returns a file URL. Safe to call from worker threads.
    static QUrl ensure(dir::IGameSource *src, const QString &gameId, const QString &texPath, int maxSize, QString *error = nullptr);
    static bool writeKtx(const dir::TextureAsset &t, const QString &file, QString *error);
    static dir::TexturePtr readKtx(const QString &file, QString *error = nullptr);
    static QUrl placeholder(const QString &kind);   // "white" | "normal" | "black"
    // a soft studio sky (Radiance .hdr) for image-based light when the scene has no environment of its own
    static QUrl studioSky();
};
