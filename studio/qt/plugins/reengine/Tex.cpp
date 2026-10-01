#include "Tex.h"
#include "Bcn.h"
#include "Binary.h"

#include <QString>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace re {

namespace {
using dir::TextureAsset;

constexpr quint32 kTexMagic = 0x00584554;   // "TEX\0"

bool isRe7Serializer(int v) { return v == 8 || v == 10 || v == 11 || v == 190820018; }

// DXGI -> neutral format
bool mapDxgi(int dxgi, TextureAsset::Format *f, bool *srgb, bool *snorm)
{
    *srgb = false;
    *snorm = false;
    switch (dxgi) {
    case 71: *f = TextureAsset::BC1; return true;
    case 72: *f = TextureAsset::BC1; *srgb = true; return true;
    case 74: *f = TextureAsset::BC2; return true;
    case 75: *f = TextureAsset::BC2; *srgb = true; return true;
    case 77: *f = TextureAsset::BC3; return true;
    case 78: *f = TextureAsset::BC3; *srgb = true; return true;
    case 80: *f = TextureAsset::BC4; return true;
    case 81: *f = TextureAsset::BC4; *snorm = true; return true;
    case 83: *f = TextureAsset::BC5; return true;
    case 84: *f = TextureAsset::BC5; *snorm = true; return true;
    case 95: case 96: *f = TextureAsset::BC6H; *snorm = dxgi == 96; return true;
    case 98: *f = TextureAsset::BC7; return true;
    case 99: *f = TextureAsset::BC7; *srgb = true; return true;
    case 28: *f = TextureAsset::RGBA8; return true;
    case 29: *f = TextureAsset::RGBA8; *srgb = true; return true;
    case 87: *f = TextureAsset::BGRA8; return true;
    case 91: *f = TextureAsset::BGRA8; *srgb = true; return true;
    case 61: case 65: *f = TextureAsset::R8; return true;
    case 49: *f = TextureAsset::RG8; return true;
    case 10: *f = TextureAsset::RGBA16F; return true;
    default: return false;
    }
}

int bytesPerPixel(TextureAsset::Format f)
{
    switch (f) {
    case TextureAsset::RGBA8: case TextureAsset::BGRA8: return 4;
    case TextureAsset::R8: return 1;
    case TextureAsset::RG8: return 2;
    case TextureAsset::RGBA16F: return 8;
    default: return 0;
    }
}

// tightly packed size and row pitch of one level
void levelLayout(const TextureAsset &t, int w, int h, qint64 *size, qint64 *pitch, int *rows)
{
    if (t.isBlockCompressed()) {
        const int bw = (w + 3) / 4, bh = (h + 3) / 4;
        *pitch = qint64(bw) * t.blockBytes();
        *rows = bh;
    } else {
        *pitch = qint64(w) * bytesPerPixel(t.format);
        *rows = h;
    }
    *size = *pitch * *rows;
}

bool bcFormat(TextureAsset::Format f, bcn::Format *out)
{
    switch (f) {
    case TextureAsset::BC1: *out = bcn::BC1; return true;
    case TextureAsset::BC2: *out = bcn::BC2; return true;
    case TextureAsset::BC3: *out = bcn::BC3; return true;
    case TextureAsset::BC4: *out = bcn::BC4; return true;
    case TextureAsset::BC5: *out = bcn::BC5; return true;
    case TextureAsset::BC7: *out = bcn::BC7; return true;
    default: return false;
    }
}

QByteArray toRgba(const TextureAsset &t, const QByteArray &level, int w, int h)
{
    bcn::Format bf;
    if (bcFormat(t.format, &bf)) return bcn::decode(bf, level, w, h, t.signedNorm);
    QByteArray out(qsizetype(w) * h * 4, char(255));
    auto *o = reinterpret_cast<uchar *>(out.data());
    const auto *s = reinterpret_cast<const uchar *>(level.constData());
    const qsizetype n = qsizetype(w) * h;
    switch (t.format) {
    case TextureAsset::RGBA8: if (level.size() >= n * 4) std::copy(s, s + n * 4, o); break;
    case TextureAsset::BGRA8:
        for (qsizetype i = 0; i < n && (i + 1) * 4 <= level.size(); ++i) { o[i * 4] = s[i * 4 + 2]; o[i * 4 + 1] = s[i * 4 + 1]; o[i * 4 + 2] = s[i * 4]; o[i * 4 + 3] = s[i * 4 + 3]; }
        break;
    case TextureAsset::R8:
        for (qsizetype i = 0; i < n && i < level.size(); ++i) o[i * 4] = o[i * 4 + 1] = o[i * 4 + 2] = s[i];
        break;
    case TextureAsset::RG8:
        for (qsizetype i = 0; i < n && (i + 1) * 2 <= level.size(); ++i) { o[i * 4] = s[i * 2]; o[i * 4 + 1] = s[i * 2 + 1]; o[i * 4 + 2] = 0; }
        break;
    default: break;
    }
    return out;
}

QByteArray fromRgba(const TextureAsset &t, const QByteArray &rgba, int w, int h)
{
    bcn::Format bf;
    if (bcFormat(t.format, &bf)) return bcn::encode(bf, rgba, w, h);
    const qsizetype n = qsizetype(w) * h;
    const auto *s = reinterpret_cast<const uchar *>(rgba.constData());
    switch (t.format) {
    case TextureAsset::RGBA8: return rgba;
    case TextureAsset::BGRA8: {
        QByteArray o(n * 4, Qt::Uninitialized);
        for (qsizetype i = 0; i < n; ++i) { o[i * 4] = char(s[i * 4 + 2]); o[i * 4 + 1] = char(s[i * 4 + 1]); o[i * 4 + 2] = char(s[i * 4]); o[i * 4 + 3] = char(s[i * 4 + 3]); }
        return o;
    }
    case TextureAsset::R8: { QByteArray o(n, Qt::Uninitialized); for (qsizetype i = 0; i < n; ++i) o[i] = char(s[i * 4]); return o; }
    case TextureAsset::RG8: { QByteArray o(n * 2, Qt::Uninitialized); for (qsizetype i = 0; i < n; ++i) { o[i * 2] = char(s[i * 4]); o[i * 2 + 1] = char(s[i * 4 + 1]); } return o; }
    default: return {};
    }
}
} // namespace

bool readTexHeader(const QByteArray &data, TexInfo *info)
{
    Reader r(data);
    if (r.u32() != kTexMagic) return false;
    info->version = r.i32();
    info->width = r.u16();
    info->height = r.u16();
    info->depth = r.u16();
    const int a = r.u8(), b = r.u8();
    info->dxgi = r.i32();
    r.i32();                                   // swizzle control
    const quint32 cubeMarker = r.u32();
    info->cube = cubeMarker != 0;
    if (isRe7Serializer(info->version)) { info->mipCount = a; info->imageCount = std::max(1, b); }
    else { info->imageCount = std::max(1, a); info->mipCount = b / 16; }
    return r.ok() && info->width > 0 && info->height > 0;
}

QSharedPointer<TextureAsset> parseTex(const QByteArray &data, QString *error, int image)
{
    TexInfo info;
    if (!readTexHeader(data, &info)) { if (error) *error = QStringLiteral("not a texture"); return {}; }
    auto t = QSharedPointer<TextureAsset>::create();
    if (!mapDxgi(info.dxgi, &t->format, &t->srgb, &t->signedNorm)) {
        if (error) *error = QStringLiteral("texture format %1 is not supported yet").arg(info.dxgi);
        return {};
    }
    t->width = info.width;
    t->height = info.height;
    t->arraySize = info.imageCount;
    t->cube = info.cube;
    Reader r(data);
    // the mip table lists every image's levels in turn: image 0 levels 0..n-1, image 1 levels 0..n-1, ...
    const qint64 table = (isRe7Serializer(info.version) ? 32 : 40) + qint64(std::clamp(image, 0, info.imageCount - 1)) * info.mipCount * 16;
    for (int level = 0; level < info.mipCount; ++level) {
        const qint64 off = r.at<qint64>(table + level * 16);
        const qint32 pitch = r.at<qint32>(table + level * 16 + 8);
        const qint32 size = r.at<qint32>(table + level * 16 + 12);
        const int w = std::max(1, info.width >> level), h = std::max(1, info.height >> level);
        qint64 want, wantPitch;
        int rows;
        levelLayout(*t, w, h, &want, &wantPitch, &rows);
        if (!r.has(off, size)) break;
        if (pitch > wantPitch && rows > 0) {           // padded rows
            QByteArray packed(want, Qt::Uninitialized);
            for (int y = 0; y < rows; ++y) std::memcpy(packed.data() + y * wantPitch, r.ptr(off + qint64(y) * pitch), size_t(wantPitch));
            t->mips << packed;
        } else {
            t->mips << r.bytesAt(off, std::min<qint64>(size, want));
        }
    }
    if (t->mips.isEmpty()) { if (error) *error = QStringLiteral("texture has no data"); return {}; }
    return t;
}

QSharedPointer<TextureAsset> atlasTex(const QVector<QSharedPointer<TextureAsset>> &layers, QString *error)
{
    if (layers.isEmpty() || !layers.first()) { if (error) *error = QStringLiteral("no layers"); return {}; }
    const TextureAsset &first = *layers.first();
    for (const auto &l : layers)
        if (!l || l->format != first.format || l->width != first.width || l->height != first.height || l->mips.size() != first.mips.size()) {
            if (error) *error = QStringLiteral("texture array layers differ");
            return {};
        }
    const int n = int(layers.size());
    const int cols = int(std::ceil(std::sqrt(double(n)))), rows = (n + cols - 1) / cols;
    auto t = QSharedPointer<TextureAsset>::create();
    t->format = first.format;
    t->srgb = first.srgb;
    t->signedNorm = first.signedNorm;
    t->width = first.width * cols;
    t->height = first.height * rows;
    t->arraySize = n;
    t->gridColumns = cols;
    t->gridRows = rows;
    // copy each layer's level into its cell, block row by block row, while a cell is at least one block wide
    const int unit = first.isBlockCompressed() ? 4 : 1;
    for (int level = 0; level < first.mips.size(); ++level) {
        const int lw = std::max(1, first.width >> level), lh = std::max(1, first.height >> level);
        if (lw < unit || lh < unit || lw % unit || lh % unit) break;
        qint64 cellSize, cellPitch;
        int cellRows;
        levelLayout(first, lw, lh, &cellSize, &cellPitch, &cellRows);
        const qint64 pitch = cellPitch * cols;
        QByteArray out(pitch * cellRows * rows, char(0));
        for (int i = 0; i < n; ++i) {
            const QByteArray &src = layers[i]->mips[level];
            const int cx = i % cols, cy = i / cols;
            for (int y = 0; y < cellRows && (y + 1) * cellPitch <= src.size(); ++y)
                std::memcpy(out.data() + (qint64(cy) * cellRows + y) * pitch + cx * cellPitch, src.constData() + y * cellPitch, size_t(cellPitch));
        }
        t->mips << out;
    }
    // the smallest levels mix the layers (only seen from very far away)
    if (t->format != TextureAsset::BC6H && t->format != TextureAsset::RGBA16F && !t->mips.isEmpty()) {
        int level = int(t->mips.size()) - 1;
        int w = std::max(1, t->width >> level), h = std::max(1, t->height >> level);
        QByteArray rgba = toRgba(*t, t->mips.last(), w, h);
        while (w > 1 || h > 1) {
            rgba = bcn::halve(rgba, w, h);
            w = std::max(1, w / 2);
            h = std::max(1, h / 2);
            t->mips << fromRgba(*t, rgba, w, h);
        }
    }
    return t;
}

QSharedPointer<TextureAsset> mergeTex(QSharedPointer<TextureAsset> resident, QSharedPointer<TextureAsset> streaming, int maxSize)
{
    QSharedPointer<TextureAsset> base = streaming && (!resident || streaming->width >= resident->width) ? streaming : resident;
    if (!base) return {};
    auto t = QSharedPointer<TextureAsset>::create(*base);
    // append resident levels smaller than what the streaming copy has
    if (resident && base != resident && resident->format == base->format) {
        const int lastW = std::max(1, t->width >> (t->mips.size() - 1));
        for (int i = 0; i < resident->mips.size(); ++i) {
            const int w = std::max(1, resident->width >> i);
            if (w < lastW) t->mips << resident->mips[i];
        }
    }
    // drop levels above the cap
    while (t->mips.size() > 1 && std::max(t->width, t->height) > maxSize) {
        t->mips.removeFirst();
        t->width = std::max(1, t->width / 2);
        t->height = std::max(1, t->height / 2);
    }
    // complete the chain down to 1x1
    if (t->format != TextureAsset::BC6H && t->format != TextureAsset::RGBA16F) {
        int level = int(t->mips.size()) - 1;
        int w = std::max(1, t->width >> level), h = std::max(1, t->height >> level);
        if (w > 1 || h > 1) {
            QByteArray rgba = toRgba(*t, t->mips.last(), w, h);
            while (w > 1 || h > 1) {
                rgba = bcn::halve(rgba, w, h);
                w = std::max(1, w / 2);
                h = std::max(1, h / 2);
                t->mips << fromRgba(*t, rgba, w, h);
            }
        }
    }
    return t;
}

QByteArray textureRgba(const TextureAsset &t, int size, int *w, int *h)
{
    int level = 0;
    while (level + 1 < t.mips.size() && std::max(t.width >> (level + 1), t.height >> (level + 1)) >= size) ++level;
    *w = std::max(1, t.width >> level);
    *h = std::max(1, t.height >> level);
    return toRgba(t, t.mips.value(level), *w, *h);
}

} // namespace re
