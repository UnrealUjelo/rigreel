#include "TextureStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMutex>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QWaitCondition>
#include <cmath>
#include <cstring>

namespace {
// GL constants for the KTX header
enum : quint32 {
    GL_UNSIGNED_BYTE = 0x1401, GL_HALF_FLOAT = 0x140B,
    GL_RED = 0x1903, GL_RGB = 0x1907, GL_RGBA = 0x1908, GL_RG = 0x8227,
    GL_R8 = 0x8229, GL_RG8 = 0x822B, GL_RGBA8 = 0x8058, GL_SRGB8_ALPHA8 = 0x8C43, GL_RGBA16F = 0x881A,
    DXT1 = 0x83F1, DXT3 = 0x83F2, DXT5 = 0x83F3, SRGB_DXT1 = 0x8C4D, SRGB_DXT3 = 0x8C4E, SRGB_DXT5 = 0x8C4F,
    RGTC1 = 0x8DBB, SIGNED_RGTC1 = 0x8DBC, RGTC2 = 0x8DBD, SIGNED_RGTC2 = 0x8DBE,
    BPTC = 0x8E8C, SRGB_BPTC = 0x8E8D, BPTC_SF = 0x8E8E, BPTC_UF = 0x8E8F,
};

// Textures being converted right now: a second thread asking for the same file waits instead of racing.
QMutex g_mutex;
QWaitCondition g_done;
QSet<QString> g_busy;
} // namespace

QString TextureStore::cacheRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
}

QString TextureStore::cacheDir(const QString &gameId)
{
    return cacheRoot() + QStringLiteral("/%1/textures").arg(gameId);
}

bool TextureStore::writeKtx(const dir::TextureAsset &t, const QString &file, QString *error)
{
    using F = dir::TextureAsset;
    quint32 type = 0, typeSize = 1, format = 0, internal = 0, base = GL_RGBA;
    switch (t.format) {
    case F::BC1: internal = t.srgb ? SRGB_DXT1 : DXT1; base = GL_RGBA; break;
    case F::BC2: internal = t.srgb ? SRGB_DXT3 : DXT3; break;
    case F::BC3: internal = t.srgb ? SRGB_DXT5 : DXT5; break;
    case F::BC4: internal = t.signedNorm ? SIGNED_RGTC1 : RGTC1; base = GL_RED; break;
    case F::BC5: internal = t.signedNorm ? SIGNED_RGTC2 : RGTC2; base = GL_RG; break;
    case F::BC6H: internal = t.signedNorm ? BPTC_SF : BPTC_UF; base = GL_RGB; break;
    case F::BC7: internal = t.srgb ? SRGB_BPTC : BPTC; break;
    case F::RGBA8: case F::BGRA8: type = GL_UNSIGNED_BYTE; format = GL_RGBA; internal = t.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8; break;
    case F::R8: type = GL_UNSIGNED_BYTE; format = GL_RED; internal = GL_R8; base = GL_RED; break;
    case F::RG8: type = GL_UNSIGNED_BYTE; format = GL_RG; internal = GL_RG8; base = GL_RG; break;
    case F::RGBA16F: type = GL_HALF_FLOAT; typeSize = 2; format = GL_RGBA; internal = GL_RGBA16F; break;
    default:
        if (error) *error = QStringLiteral("unsupported texture format");
        return false;
    }
    QByteArray out;
    const uchar id[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    out.append(reinterpret_cast<const char *>(id), 12);
    auto u32 = [&](quint32 v) { out.append(reinterpret_cast<const char *>(&v), 4); };
    u32(0x04030201);
    u32(type);
    u32(typeSize);
    u32(format);
    u32(internal);
    u32(base);
    u32(quint32(t.width));
    u32(quint32(t.height));
    u32(0);
    u32(0);
    u32(1);
    u32(quint32(t.mips.size()));
    // key/value data: an atlas remembers its grid ("DirectorGrid" = "columns rows")
    QByteArray kv;
    if (t.gridColumns > 1 || t.gridRows > 1) {
        const QByteArray pair = QByteArrayLiteral("DirectorGrid") + '\0' + QByteArray::number(t.gridColumns) + ' ' + QByteArray::number(t.gridRows) + '\0';
        const quint32 n = quint32(pair.size());
        kv.append(reinterpret_cast<const char *>(&n), 4);
        kv += pair;
        while (kv.size() % 4) kv.append('\0');
    }
    u32(quint32(kv.size()));
    out += kv;
    for (const QByteArray &level : t.mips) {
        QByteArray data = level;
        if (t.format == F::BGRA8)
            for (qsizetype i = 0; i + 3 < data.size(); i += 4) std::swap(data[i], data[i + 2]);
        u32(quint32(data.size()));
        out += data;
        while (out.size() % 4) out.append('\0');
    }
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly) || f.write(out) != out.size() || !f.commit()) {
        if (error) *error = QStringLiteral("cannot write %1").arg(file);
        return false;
    }
    return true;
}

QUrl TextureStore::ensure(dir::IGameSource *src, const QString &gameId, const QString &texPath, int maxSize, QString *error)
{
    if (!src || texPath.isEmpty()) return {};
    const QByteArray key = QCryptographicHash::hash((texPath.toLower() + QLatin1Char('@') + QString::number(maxSize) + QStringLiteral("v4")).toUtf8(),
                                                    QCryptographicHash::Sha1).toHex().left(24);
    const QString file = cacheDir(gameId) + QStringLiteral("/%1/%2.ktx").arg(QString::fromLatin1(key.left(2)), QString::fromLatin1(key));
    {
        QMutexLocker lock(&g_mutex);
        while (g_busy.contains(file)) g_done.wait(&g_mutex);
        if (QFileInfo::exists(file)) return QUrl::fromLocalFile(file);
        g_busy.insert(file);
    }
    QString err;
    dir::TexturePtr t = src->loadTexture(texPath, &err);
    // the caller's size cap (an atlas of layers may be twice as wide: its cells stay within the cap)
    if (t && maxSize > 0) {
        const int cap = maxSize * std::max(1, t->gridColumns / 2);
        if (std::max(t->width, t->height) > cap && t->mips.size() > 1) {
            auto c = QSharedPointer<dir::TextureAsset>::create(*t);
            while (c->mips.size() > 1 && std::max(c->width, c->height) > cap) {
                c->mips.removeFirst();
                c->width = std::max(1, c->width / 2);
                c->height = std::max(1, c->height / 2);
            }
            t = c;
        }
    }
    bool ok = t && writeKtx(*t, file, &err);
    {
        QMutexLocker lock(&g_mutex);
        g_busy.remove(file);
        g_done.wakeAll();
    }
    if (!ok) {
        if (error) *error = err;
        return {};
    }
    return QUrl::fromLocalFile(file);
}

dir::TexturePtr TextureStore::readKtx(const QString &file, QString *error)
{
    using F = dir::TextureAsset;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) { if (error) *error = QStringLiteral("cannot read %1").arg(file); return {}; }
    const QByteArray all = f.readAll();
    static const char kId[12] = {'\xAB', 'K', 'T', 'X', ' ', '1', '1', '\xBB', '\r', '\n', '\x1A', '\n'};
    if (all.size() < 64 || std::memcmp(all.constData(), kId, 12) != 0) { if (error) *error = QStringLiteral("not a KTX file"); return {}; }
    auto u32 = [&](qsizetype at) { quint32 v = 0; if (at + 4 <= all.size()) std::memcpy(&v, all.constData() + at, 4); return v; };
    auto t = QSharedPointer<F>::create();
    const quint32 internal = u32(28);
    t->width = int(u32(36));
    t->height = std::max(1, int(u32(40)));
    const quint32 levels = u32(56), kv = u32(60);
    switch (internal) {
    case DXT1: case SRGB_DXT1: t->format = F::BC1; t->srgb = internal == SRGB_DXT1; break;
    case DXT3: case SRGB_DXT3: t->format = F::BC2; t->srgb = internal == SRGB_DXT3; break;
    case DXT5: case SRGB_DXT5: t->format = F::BC3; t->srgb = internal == SRGB_DXT5; break;
    case RGTC1: case SIGNED_RGTC1: t->format = F::BC4; t->signedNorm = internal == SIGNED_RGTC1; break;
    case RGTC2: case SIGNED_RGTC2: t->format = F::BC5; t->signedNorm = internal == SIGNED_RGTC2; break;
    case BPTC_UF: case BPTC_SF: t->format = F::BC6H; t->signedNorm = internal == BPTC_SF; break;
    case BPTC: case SRGB_BPTC: t->format = F::BC7; t->srgb = internal == SRGB_BPTC; break;
    case GL_RGBA8: case GL_SRGB8_ALPHA8: t->format = F::RGBA8; t->srgb = internal == GL_SRGB8_ALPHA8; break;
    case GL_R8: t->format = F::R8; break;
    case GL_RG8: t->format = F::RG8; break;
    case GL_RGBA16F: t->format = F::RGBA16F; break;
    default: if (error) *error = QStringLiteral("unknown KTX format %1").arg(internal, 0, 16); return {};
    }
    for (qsizetype p = 64; p + 4 <= 64 + qsizetype(kv);) {
        const quint32 n = u32(p);
        const QByteArray pair = all.mid(p + 4, n);
        if (pair.startsWith(QByteArrayLiteral("DirectorGrid"))) {
            const QList<QByteArray> v = pair.mid(13).split(' ');
            t->gridColumns = std::max(1, v.value(0).trimmed().toInt());
            t->gridRows = std::max(1, QByteArray(v.value(1)).replace('\0', "").trimmed().toInt());
        }
        p += 4 + ((n + 3) & ~3u);
    }
    qsizetype at = 64 + kv;
    for (quint32 l = 0; l < levels; ++l) {
        const quint32 n = u32(at);
        at += 4;
        if (at + n > all.size()) break;
        t->mips << all.mid(at, n);
        at += (n + 3) & ~3u;
    }
    t->path = file;
    if (t->mips.isEmpty()) { if (error) *error = QStringLiteral("empty KTX"); return {}; }
    return t;
}

QUrl TextureStore::placeholder(const QString &kind)
{
    const QString file = cacheRoot() + QStringLiteral("/placeholder_%1.png").arg(kind);
    if (!QFileInfo::exists(file)) {
        QDir().mkpath(cacheRoot());
        QImage img(4, 4, QImage::Format_RGBA8888);
        if (kind == QLatin1String("normal")) img.fill(QColor(128, 128, 255, 153));      // flat normal, roughness 0.6
        else if (kind == QLatin1String("black")) img.fill(QColor(0, 0, 0, 255));
        else if (kind == QLatin1String("packed")) img.fill(QColor(255, 0, 255, 255));    // alpha 1, no translucency, AO 1
        else img.fill(QColor(255, 255, 255, 255));
        img.save(file);
    }
    return QUrl::fromLocalFile(file);
}

QUrl TextureStore::studioSky()
{
    const QString file = cacheRoot() + QStringLiteral("/studio_sky_v1.hdr");
    if (!QFileInfo::exists(file)) {
        QDir().mkpath(cacheRoot());
        const int w = 256, h = 128;
        QByteArray out = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 128 +X 256\n";
        auto rgbe = [](float r, float g, float b) {
            const float m = std::max(r, std::max(g, b));
            if (m < 1e-32f) return QByteArray(4, char(0));
            int e;
            const float scale = std::frexp(m, &e) * 256.0f / m;
            QByteArray px(4, char(0));
            px[0] = char(uchar(r * scale));
            px[1] = char(uchar(g * scale));
            px[2] = char(uchar(b * scale));
            px[3] = char(uchar(e + 128));
            return px;
        };
        for (int y = 0; y < h; ++y) {
            const float v = 1.0f - (y + 0.5f) / h;                // 1 = straight up, 0 = straight down
            const float el = (v - 0.5f) * 3.14159f;               // elevation
            for (int x = 0; x < w; ++x) {
                const float az = (x + 0.5f) / w * 6.28318f;
                float r, g, b;
                if (el > 0) {                                     // sky: cool zenith, bright horizon
                    const float t = std::pow(std::sin(el), 0.6f);
                    r = 1.05f * (1 - t) + 0.45f * t;
                    g = 1.05f * (1 - t) + 0.55f * t;
                    b = 1.10f * (1 - t) + 0.75f * t;
                    // a broad soft "window" for key-light reflections
                    const float d = std::max(0.f, std::cos(az - 0.9f)) * std::max(0.f, std::sin(el * 1.3f));
                    r += 2.2f * std::pow(d, 6.f); g += 2.1f * std::pow(d, 6.f); b += 2.0f * std::pow(d, 6.f);
                } else {                                          // ground: warm grey
                    const float t = std::min(1.f, -el * 2.5f);
                    r = 0.42f * (1 - t) + 0.16f * t;
                    g = 0.40f * (1 - t) + 0.15f * t;
                    b = 0.37f * (1 - t) + 0.14f * t;
                }
                out += rgbe(r, g, b);
            }
        }
        QSaveFile f(file);
        if (f.open(QIODevice::WriteOnly)) { f.write(out); f.commit(); }
    }
    return QUrl::fromLocalFile(file);
}
