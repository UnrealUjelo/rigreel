#include "ModelPrep.h"
#include "TextureStore.h"

#include <QHash>
#include <algorithm>
#include <functional>
#include <cmath>
#include <cstring>

namespace {
MaterialDesc describe(const dir::MaterialAsset &m, const std::function<QString(const QString &)> &tex)
{
    MaterialDesc d;
    d.name = m.name;
    d.role = m.role;
    d.baseColor = tex(m.textures.value(dir::slot::BaseColor));
    d.normal = tex(m.textures.value(dir::slot::Normal));
    d.packed = tex(m.textures.value(dir::slot::Packed));
    d.emissive = tex(m.textures.value(dir::slot::Emissive));
    d.detail = tex(m.textures.value(dir::slot::Detail));
    d.mask = tex(m.textures.value(dir::slot::Mask));
    d.hasDetail = !d.detail.isEmpty();
    d.hasMask = !d.mask.isEmpty();
    d.hasBase = !d.baseColor.isEmpty();
    d.hasNormal = !d.normal.isEmpty();
    d.hasPacked = !d.packed.isEmpty() && m.packedLayout == QLatin1String("atoc");
    d.hasEmissive = !d.emissive.isEmpty();
    d.baseAlpha = m.baseAlpha == QLatin1String("dielectric") ? 1 : m.baseAlpha == QLatin1String("metal") ? 2 : m.baseAlpha == QLatin1String("alpha") ? 3 : 0;
    d.normalRoughness = m.normalAlpha == QLatin1String("roughness");
    d.normalLayout = m.normalLayout == QLatin1String("nrrc") ? 1 : 0;
    d.baseUv = m.baseUv;
    d.normalUv = m.normalUv;
    auto lin = [](float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); };
    d.tint = QVector4D(lin(m.baseColor.x()) * m.baseIntensity, lin(m.baseColor.y()) * m.baseIntensity, lin(m.baseColor.z()) * m.baseIntensity, m.baseColor.w());
    d.shading = m.shading == QLatin1String("terrain") ? 2 : m.shading == QLatin1String("layered") ? 1 : m.shading == QLatin1String("multiply") ? 3 : 0;
    d.detailTint = QVector4D(lin(m.detailTint.x()), lin(m.detailTint.y()), lin(m.detailTint.z()), 1) * m.detailTint.w();
    d.detailTint.setW(1);
    d.detailUv = m.detailUv;
    d.dirtColor = QVector4D(lin(m.dirtColor.x()), lin(m.dirtColor.y()), lin(m.dirtColor.z()), m.dirtColor.w());
    d.maskUvSet = m.maskUvSet;
    d.groundTile = m.groundTile;
    d.emissiveColor = QVector3D(lin(m.emissiveColor.x()), lin(m.emissiveColor.y()), lin(m.emissiveColor.z()));
    d.emissiveIntensity = m.emissiveIntensity;
    d.roughness = m.roughness;
    d.metalness = m.metalness;
    d.alphaCutoff = m.alphaCutoff;
    d.alphaTest = m.alphaTest;
    d.transparent = m.transparent || m.role == QLatin1String("glass");
    d.twoSided = m.twoSided || m.role == QLatin1String("hair");
    d.castsShadow = m.castsShadow;
    if (!d.hasBase && !d.hasNormal && m.role != QLatin1String("glass") && !m.transparent) d.alphaTest = false;
    return d;
}

inline void put(char *dst, const void *src, int n) { std::memcpy(dst, src, size_t(n)); }
} // namespace

PreparedModel prepareModel(const dir::ModelPtr &model, dir::IGameSource *src, const QString &gameId, int maxTexture, TextureShare *share)
{
    PreparedModel pm;
    pm.model = model;
    if (!model) return pm;
    QHash<QString, QString> keys;                          // game path -> cache file ("" = failed)
    auto tex = [&](const QString &path) -> QString {
        if (path.isEmpty()) return {};
        const auto it = keys.constFind(path.toLower());
        if (it != keys.cend()) return *it;
        QString err, key;
        const QUrl u = TextureStore::ensure(src, gameId, path, maxTexture, &err);
        if (u.isValid()) {
            key = u.toLocalFile();
            dir::TexturePtr t;
            if (share) {
                QMutexLocker lock(&share->mutex);
                t = share->textures.value(key);
            }
            if (!t && (t = TextureStore::readKtx(key, &err)) && share) {
                QMutexLocker lock(&share->mutex);
                share->textures.insert(key, t);
            }
            if (t) pm.textures.insert(key, t);
            else key.clear();
        }
        if (key.isEmpty()) pm.warnings << QStringLiteral("%1: %2").arg(path.section(QLatin1Char('/'), -1), err);
        keys.insert(path.toLower(), key);
        return key;
    };

    for (const dir::ModelPiece &piece : model->pieces) {
        const dir::MeshAsset &mesh = *piece.mesh;
        PreparedPiece pp;
        pp.name = piece.name;
        pp.local = piece.local;
        pp.skinned = mesh.influences > 0 && !mesh.skinBones.isEmpty() && piece.parentBone.isEmpty();
        if (!piece.parentBone.isEmpty()) pp.parentBone = model->skeleton.find(piece.parentBone);
        if (pp.skinned) {
            for (int sb : mesh.skinBones) {
                const dir::Bone *mb = sb >= 0 && sb < mesh.skeleton.bones.size() ? &mesh.skeleton.bones[sb] : nullptr;
                const int modelBone = mb ? model->skeleton.findHash(mb->hash) : -1;
                pp.jointBones << std::max(0, modelBone);
                pp.inverseBinds << (mb ? mb->inverseBind : QMatrix4x4());
            }
        }
        for (const dir::MaterialAsset &m : piece.materials) {
            MaterialDesc d = describe(m, tex);
            if (const dir::TexturePtr t = pm.textures.value(d.baseColor)) d.atlasColumns = std::max(1, t->gridColumns);
            pp.materials << d;
        }

        // vertices
        const int n = mesh.vertexCount();
        pp.stride = pp.skinned ? vtx::Skinned : vtx::Rigid;
        pp.vertices.resize(qsizetype(n) * pp.stride);
        char *v = pp.vertices.data();
        const bool hasN = mesh.normals.size() == n, hasT = mesh.tangents.size() == n, hasUv = mesh.uv0.size() == n, hasUv1 = mesh.uv1.size() == n;
        QVector<QPair<float, int>> infl(mesh.influences);
        for (int i = 0; i < n; ++i) {
            char *o = v + qsizetype(i) * pp.stride;
            const QVector3D p = mesh.positions[i];
            const QVector3D nn = hasN ? mesh.normals[i] : QVector3D(0, 1, 0);
            const QVector4D tt = hasT ? mesh.tangents[i] : QVector4D(1, 0, 0, 1);
            const QVector3D t3 = tt.toVector3D();
            const QVector3D b3 = QVector3D::crossProduct(nn, t3) * tt.w();
            const float pos[3] = {p.x(), p.y(), p.z()}, nor[3] = {nn.x(), nn.y(), nn.z()};
            const float tan[3] = {t3.x(), t3.y(), t3.z()}, bin[3] = {b3.x(), b3.y(), b3.z()};
            const QVector2D uv = hasUv ? mesh.uv0[i] : QVector2D(), uv1 = hasUv1 ? mesh.uv1[i] : uv;
            const float u0[2] = {uv.x(), uv.y()}, u1[2] = {uv1.x(), uv1.y()};
            put(o + vtx::Pos, pos, 12);
            put(o + vtx::Normal, nor, 12);
            put(o + vtx::Tangent, tan, 12);
            put(o + vtx::Binormal, bin, 12);
            put(o + vtx::Uv0, u0, 8);
            put(o + vtx::Uv1, u1, 8);
            if (pp.skinned) {
                for (int k = 0; k < mesh.influences; ++k) infl[k] = {mesh.weights[i * mesh.influences + k], mesh.joints[i * mesh.influences + k]};
                std::partial_sort(infl.begin(), infl.begin() + std::min(4, int(infl.size())), infl.end(),
                                  [](const auto &a, const auto &b) { return a.first > b.first; });
                qint32 j[4] = {0, 0, 0, 0};
                float w[4] = {0, 0, 0, 0}, sum = 0;
                for (int k = 0; k < 4 && k < infl.size(); ++k) {
                    j[k] = qBound(0, infl[k].second, int(pp.jointBones.size()) - 1);
                    w[k] = infl[k].first;
                    sum += w[k];
                }
                if (sum > 0) for (float &x : w) x /= sum;
                else w[0] = 1;
                put(o + vtx::Joints, j, 16);
                put(o + vtx::Weights, w, 16);
            }
        }

        // indices: one subset per visible part
        QVector<quint32> idx;
        idx.reserve(mesh.indices.size());
        QVector3D lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f);
        for (const dir::MeshPart &part : mesh.parts) {
            if (piece.hiddenGroups.contains(part.group) || part.indexCount <= 0) continue;
            if (part.material < 0 || part.material >= pp.materials.size()) continue;
            if (piece.materials.value(part.material).hidden) continue;
            PreparedSubset s;
            s.indexOffset = int(idx.size());
            s.indexCount = part.indexCount;
            s.material = part.material;
            s.boundsMin = part.boundsMin;
            s.boundsMax = part.boundsMax;
            for (int k = 0; k < part.indexCount; ++k) idx << mesh.indices[part.indexOffset + k];
            lo = QVector3D(std::min(lo.x(), part.boundsMin.x()), std::min(lo.y(), part.boundsMin.y()), std::min(lo.z(), part.boundsMin.z()));
            hi = QVector3D(std::max(hi.x(), part.boundsMax.x()), std::max(hi.y(), part.boundsMax.y()), std::max(hi.z(), part.boundsMax.z()));
            pp.subsets << s;
        }
        if (pp.subsets.isEmpty()) continue;
        pp.indices = QByteArray(reinterpret_cast<const char *>(idx.constData()), idx.size() * 4);
        pp.boundsMin = lo;
        pp.boundsMax = hi;
        pm.pieces << pp;
    }
    return pm;
}
