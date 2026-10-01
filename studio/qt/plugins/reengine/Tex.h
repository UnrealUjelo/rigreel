// RE Engine .tex reader. A texture may exist twice: the resident file (small mips) and a "streaming" copy
// with the large mips (natives/<plat>/streaming/...). readTexture() merges both, caps the size and completes
// the mip chain down to 1x1 so the renderer can use it as is.
#pragma once

#include <director/Assets.h>
#include <QByteArray>

namespace re {

struct TexInfo {
    int version = 0, width = 0, height = 0, depth = 1, mipCount = 0, imageCount = 1, dxgi = 0;
    bool cube = false;
};

bool readTexHeader(const QByteArray &data, TexInfo *info);
// Parse one file into an asset (one image of an array, all mips present in the file).
QSharedPointer<dir::TextureAsset> parseTex(const QByteArray &data, QString *error, int image = 0);
// Merge resident + streaming parts, cap to maxSize, fill the missing small mips.
QSharedPointer<dir::TextureAsset> mergeTex(QSharedPointer<dir::TextureAsset> resident, QSharedPointer<dir::TextureAsset> streaming, int maxSize);
// The layers of a texture array (each merged, same size and format) as one 2D grid atlas with a full mip chain.
QSharedPointer<dir::TextureAsset> atlasTex(const QVector<QSharedPointer<dir::TextureAsset>> &layers, QString *error);
// RGBA8 of the level closest to `size` (thumbnails, CPU checks).
QByteArray textureRgba(const dir::TextureAsset &t, int size, int *w, int *h);

} // namespace re
