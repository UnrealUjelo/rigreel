#include "Mdf.h"
#include "Binary.h"

#include <algorithm>

namespace re {

using dir::MaterialAsset;

namespace {
constexpr quint32 kMdfMagic = 0x0046444D;   // "MDF\0"

// RE texture slots -> neutral slots. The packing of each RE map is fixed by its name.
void mapTexture(MaterialAsset &m, const QString &slotName, const QString &path)
{
    const QString s = slotName.toLower();
    const QString p = path.toLower();
    auto set = [&](const QString &slot) { if (!m.textures.contains(slot)) m.textures.insert(slot, path); };
    if (s.startsWith(QLatin1String("basedielectricmap")) || s == QLatin1String("basemap") || s.startsWith(QLatin1String("basemetalmap"))
        || s == QLatin1String("albedomap") || s == QLatin1String("basealphamap") || s.startsWith(QLatin1String("basecolormap"))
        || s == QLatin1String("baseshiftmap")) {                      // hair: strand shading, coloured by BaseColor
        if (!m.textures.contains(dir::slot::BaseColor)) {
            m.textures.insert(dir::slot::BaseColor, path);
            m.rawTextures.insert(QStringLiteral("#baseSlot"), slotName);
            if (s.startsWith(QLatin1String("basedielectric")) || p.contains(QLatin1String("_albd"))) m.baseAlpha = QStringLiteral("dielectric");
            else if (s.startsWith(QLatin1String("basemetal")) || p.contains(QLatin1String("_albm"))) m.baseAlpha = QStringLiteral("metal");
            else if (s.contains(QLatin1String("alpha")) || p.contains(QLatin1String("_alba"))) m.baseAlpha = QStringLiteral("alpha");
        }
    } else if (s.startsWith(QLatin1String("normalroughness")) || s == QLatin1String("normalmap") || s.startsWith(QLatin1String("normalmap"))) {
        if (!m.textures.contains(dir::slot::Normal)) {
            m.textures.insert(dir::slot::Normal, path);
            if (s.contains(QLatin1String("cavity")) || p.contains(QLatin1String("_nrrc"))) m.normalLayout = QStringLiteral("nrrc");
            else if (s.contains(QLatin1String("roughness")) || p.contains(QLatin1String("_nrmr"))) m.normalAlpha = QStringLiteral("roughness");
            m.rawTextures.insert(QStringLiteral("#normalSlot"), slotName);
        }
    } else if (s.startsWith(QLatin1String("alphatranslucentocclusion")) || p.contains(QLatin1String("_atoc")) || p.contains(QLatin1String("_atos"))) {
        if (!m.textures.contains(dir::slot::Packed)) {
            m.textures.insert(dir::slot::Packed, path);
            m.packedLayout = QStringLiteral("atoc");
        }
    } else if (s.startsWith(QLatin1String("emissive"))) {
        set(dir::slot::Emissive);
    }
}

QString roleOf(const MaterialAsset &m)
{
    const QString s = (m.shader + QLatin1Char(' ') + m.name).toLower();
    if (s.contains(QLatin1String("hair")) || s.contains(QLatin1String("eyelash")) || s.contains(QLatin1String("brow"))) return QStringLiteral("hair");
    if (s.contains(QLatin1String("eye")) || s.contains(QLatin1String("cornea")) || s.contains(QLatin1String("iris"))) return QStringLiteral("eye");
    if (s.contains(QLatin1String("skin")) || s.contains(QLatin1String("face")) || s.contains(QLatin1String("head"))) return QStringLiteral("skin");
    if (s.contains(QLatin1String("glass")) || s.contains(QLatin1String("lens"))) return QStringLiteral("glass");
    if (s.contains(QLatin1String("cloth")) || s.contains(QLatin1String("fabric"))) return QStringLiteral("cloth");
    return QStringLiteral("standard");
}
} // namespace

QVector<MaterialAsset> parseMdf(const QByteArray &data, int version, QString *error)
{
    QVector<MaterialAsset> out;
    Reader r(data);
    if (r.u32() != kMdfMagic) { if (error) *error = QStringLiteral("not a material file"); return out; }
    r.i16();
    const int count = r.i16();
    r.i64();
    r.align(16);
    // v51 has two incompatible layouts; the standard one is used by the games we read
    const qint64 headerStart = r.pos();
    int stride = 0;
    {
        Reader probe(data);
        probe.seek(headerStart);
        const qint64 before = probe.pos();
        probe.i64(); probe.u32();
        if (version == 6) probe.u64();
        probe.i32(); probe.i32(); probe.i32();
        if (version >= 19) { probe.i32(); probe.i32(); }
        if (version >= 31) probe.u32();
        probe.i32();
        if (version >= 31) { probe.u64(); if (version >= 51) probe.u64(); probe.u32(); } else probe.u32();
        probe.i64(); probe.i64();
        if (version >= 19) probe.i64();
        probe.i64(); probe.i64();
        if (version >= 31) probe.i64();
        stride = int(probe.pos() - before);
    }
    for (int i = 0; i < count; ++i) {
        Reader h(data);
        h.seek(headerStart + qint64(i) * stride);
        MaterialAsset m;
        m.name = h.wstringAt(h.i64());
        h.u32();
        if (version == 6) h.u64();
        const int paramsSize = h.i32();
        Q_UNUSED(paramsSize);
        const int paramCount = h.i32();
        const int texCount = h.i32();
        if (version >= 19) { h.i32(); h.i32(); }
        if (version >= 31) h.u32();
        h.i32();                                                  // shader type
        quint64 flags;
        if (version >= 31) { flags = h.u64(); if (version >= 51) h.u64(); h.u32(); } else flags = h.u32();
        const qint64 paramHeaders = h.i64(), texHeaders = h.i64();
        if (version >= 19) h.i64();
        const qint64 params = h.i64();
        m.shader = h.wstringAt(h.i64());
        if (!h.ok()) break;

        m.twoSided = (flags & 0x1) || (flags & (1ull << 8)) || (flags & (1ull << 9));
        m.alphaTest = (flags & 0x2) || (flags & (1ull << 25)) || (flags & (1ull << 26));
        m.castsShadow = !(flags & 0x4);
        const bool emissive = flags & 0x10;

        const int texHeaderSize = version >= 13 ? 32 : 24;
        for (int t = 0; t < texCount; ++t) {
            const qint64 at = texHeaders + qint64(t) * texHeaderSize;
            const QString slotName = h.wstringAt(h.at<qint64>(at));
            const QString path = h.wstringAt(h.at<qint64>(at + 16));
            if (slotName.isEmpty()) continue;
            m.rawTextures.insert(slotName, path);
            if (!path.isEmpty() && !path.contains(QLatin1String("systems/rendering/"), Qt::CaseInsensitive)) mapTexture(m, slotName, path);
        }
        for (int p = 0; p < paramCount; ++p) {
            const qint64 at = paramHeaders + qint64(p) * (version >= 13 ? 24 : 24);
            const QString name = h.wstringAt(h.at<qint64>(at));
            int rel, comps;
            if (version >= 31) { rel = h.at<qint32>(at + 16); comps = int(h.at<quint32>(at + 20) & 0xFFFF); }
            else if (version >= 13) { rel = h.at<qint32>(at + 16); comps = h.at<qint16>(at + 20); }
            else { comps = h.at<qint32>(at + 16); rel = h.at<qint32>(at + 20); }
            comps = qBound(1, comps, 4);
            QVector4D v;
            for (int c = 0; c < comps; ++c) v[c] = h.at<float>(params + rel + c * 4);
            m.params.insert(name, v);
        }
        // common RE parameters
        for (const char *key : {"BaseColor", "BaseColorFactor", "BaseColor_Tint", "Albedo"}) {
            const auto it = m.params.constFind(QLatin1String(key));
            if (it != m.params.cend() && it->toVector3D().lengthSquared() > 0) { m.baseColor = QVector4D(it->toVector3D(), 1); break; }
        }
        if (m.params.contains(QStringLiteral("AlphaTestThreshold"))) m.alphaCutoff = m.params.value(QStringLiteral("AlphaTestThreshold")).x();
        if (m.params.contains(QStringLiteral("Roughness"))) m.roughness = m.params.value(QStringLiteral("Roughness")).x();
        if (m.params.contains(QStringLiteral("Metallic"))) m.metalness = m.params.value(QStringLiteral("Metallic")).x();
        if (emissive) {
            for (const char *key : {"Emissive_Color", "EmissiveColor"})
                if (m.params.contains(QLatin1String(key))) m.emissiveColor = m.params.value(QLatin1String(key)).toVector3D();
            for (const char *key : {"Emissive_Intensity", "EmissiveIntensity"})
                if (m.params.contains(QLatin1String(key))) m.emissiveIntensity = m.params.value(QLatin1String(key)).x();
        }
        m.role = roleOf(m);
        // texture tiling: layered world materials tile their "...Base" maps by UV_Tiling (x UV_Tiling_Offset);
        // decals and simple surfaces use BasicMap_Tiling; object-specific maps stay 1:1
        {
            auto tiling = [&](const char *scalar, const char *vec) {
                const float k = m.params.contains(QLatin1String(scalar)) ? m.params.value(QLatin1String(scalar)).x() : 1.f;
                const QVector4D o = m.params.value(QLatin1String(vec), QVector4D(1, 1, 0, 0));
                return QVector4D(k * (o.x() == 0 ? 1 : o.x()), k * (o.y() == 0 ? 1 : o.y()), o.z(), o.w());
            };
            const QString baseSlot = m.rawTextures.take(QStringLiteral("#baseSlot")).toLower();
            const QString normalSlot = m.rawTextures.take(QStringLiteral("#normalSlot")).toLower();
            if (m.params.contains(QStringLiteral("BasicMap_Tiling"))) m.baseUv = m.normalUv = tiling("BasicMap_Tiling", "BasicMap_Tiling_Offset");
            if (baseSlot.endsWith(QLatin1String("base")) && m.params.contains(QStringLiteral("UV_Tiling"))) m.baseUv = tiling("UV_Tiling", "UV_Tiling_Offset");
            if (normalSlot.endsWith(QLatin1String("base")) && m.params.contains(QStringLiteral("UV_Tiling"))) m.normalUv = tiling("UV_Tiling", "UV_Tiling_Offset");
        }
        // surfaces the renderer treats specially
        const QString sh = m.shader.toLower();
        auto raw = [&](const char *slot) { return m.rawTextures.value(QLatin1String(slot)); };
        auto param = [&](const char *name, const QVector4D &def) { return m.params.value(QLatin1String(name), def); };
        // world surfaces: the colour map's alpha is not metalness (wood fences carry 0, plaster 255); alpha-tested
        // ones cut out by it when no packed map holds the alpha
        if (sh.startsWith(QLatin1String("env_"))) {
            if (m.alphaTest && !m.textures.contains(dir::slot::Packed)) m.baseAlpha = QStringLiteral("alpha");
            else m.baseAlpha.clear();
        }
        if (sh.contains(QLatin1String("groundshader"))) {
            // terrain: an atlas of ground layers (ALBD + NRRC texture arrays) picked per spot by a map-wide splat map on UV0
            m.shading = QStringLiteral("terrain");
            if (!raw("ALBD").isEmpty()) m.textures.insert(dir::slot::BaseColor, raw("ALBD"));
            if (!raw("NormalRoughnessOcclusionMap").isEmpty()) {
                m.textures.insert(dir::slot::Normal, raw("NormalRoughnessOcclusionMap"));
                m.normalLayout = QStringLiteral("nrrc");
            }
            if (!raw("InputSplatMap").isEmpty()) m.textures.insert(dir::slot::Mask, raw("InputSplatMap"));
            m.baseAlpha.clear();                          // the layers keep height in alpha
            m.groundTile = std::max(0.5f, param("Tiling", QVector4D(5, 0, 0, 0)).x());
            m.baseUv = m.normalUv = QVector4D(1, 1, 0, 0);
        } else if (sh.startsWith(QLatin1String("env_")) && sh.contains(QLatin1String("layer")) && sh.contains(QLatin1String("dirt"))) {
            // layered world surface: base (+ a tiled second layer by LYMO red) + dirt colours by LYMO blue, occlusion LYMO alpha
            m.shading = QStringLiteral("layered");
            if (!raw("LayerMaskOcclusionMap").isEmpty()) m.textures.insert(dir::slot::Mask, raw("LayerMaskOcclusionMap"));
            if (!raw("BaseDielectricMap1").isEmpty()) {
                m.textures.insert(dir::slot::Detail, raw("BaseDielectricMap1"));
                const QVector4D c = param("BaseColor1", QVector4D(1, 1, 1, 1));
                m.detailTint = QVector4D(c.x(), c.y(), c.z(), param("MaterialColor_Intensity1", QVector4D(1, 0, 0, 0)).x());
                const float k = param("UV_Tiling1", QVector4D(1, 0, 0, 0)).x();
                const QVector4D o = param("UV_Tiling_Offset1", QVector4D(1, 1, 0, 0));
                const float sx = o.x() == 0 ? 1 : o.x(), sy = o.y() == 0 ? sx : o.y();
                m.detailUv = QVector4D(k * sx, k * sy, o.z(), o.w());
            }
            m.baseIntensity = param("MaterialColor_Intensity", QVector4D(1, 0, 0, 0)).x();
            if (param("Dirt_Enable", QVector4D(0, 0, 0, 0)).x() > 0.5f) {
                const QVector4D d1 = param("DirtColor1", QVector4D(0.3f, 0.25f, 0.2f, 1)), d2 = param("DirtColor2", d1);
                const float mix = std::clamp(param("DirtColorControl", QVector4D(0.5f, 0, 0, 0)).x(), 0.f, 1.f);
                const QVector4D d = d1 + (d2 - d1) * mix;
                m.dirtColor = QVector4D(d.x(), d.y(), d.z(), std::clamp(param("DirtMask_Brightness", QVector4D(0.5f, 0, 0, 0)).x(), 0.f, 1.f));
            }
            m.maskUvSet = param("LayerMask_Use_SecondaryUV", QVector4D(0, 0, 0, 0)).x() > 0.5f ? 1 : 0;
        }
        if (sh.contains(QLatin1String("stitch"))) m.hidden = true;                 // thread decals: need their mask shader
        // cornea / tear film over the eyeball: a reflection-only layer on a plain white base, drawn opaque it whites out the eyes
        if (sh.contains(QLatin1String("reflectivetransparent")) && m.textures.value(QStringLiteral("baseColor")).contains(QLatin1String("null"), Qt::CaseInsensitive))
            m.hidden = true;
        // decals lie on other surfaces and blend in by their alpha (ATOS red)
        if (sh.contains(QLatin1String("decal"))) { m.transparent = true; m.castsShadow = false; }
        if (sh.contains(QLatin1String("transparent")) || m.role == QLatin1String("glass")) {
            m.transparent = true;
            if (m.params.contains(QStringLiteral("Alpha"))) m.baseColor.setW(m.params.value(QStringLiteral("Alpha")).x());
        }
        // eyelid occlusion shells: multiplied over the eyes (white, occlusion in alpha), never drawn as colour
        if (sh.contains(QLatin1String("character_ao"))) { m.shading = QStringLiteral("multiply"); m.castsShadow = false; }
        if (m.role == QLatin1String("hair")) {
            m.baseAlpha.clear();
            // strands darken towards OcclusionColor by the occlusion of the hair volume (ATOC blue, on UV1)
            if (param("OcclusionToAlbedo", QVector4D(0, 0, 0, 0)).x() > 0) {
                const QVector4D oc = param("OcclusionColor", QVector4D(0.1f, 0.09f, 0.085f, 1));
                m.dirtColor = QVector4D(oc.x(), oc.y(), oc.z(), std::clamp(param("OcclusionToAlbedo", QVector4D(1, 0, 0, 0)).x(), 0.f, 1.f));
            }
            m.maskUvSet = param("Occlusion_UseSecondaryUV", QVector4D(0, 0, 0, 0)).x() > 0.5f ? 1 : 0;
        }
        out << m;
    }
    if (out.isEmpty() && error) *error = QStringLiteral("no materials");
    return out;
}

} // namespace re
