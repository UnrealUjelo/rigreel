#include "StageSet.h"
#include "GpuTexture.h"
#include "SceneActor.h"

#include <QQmlEngine>
#include <QtQuick3D/QQuick3DGeometry>
#include <QtQuick3D/qquick3dinstancing.h>
#include <QtQuick3D/private/qquick3dmaterial_p.h>
#include <QtQuick3D/private/qquick3dmodel_p.h>

#include <cstring>

namespace {
// placements of one mesh piece: the rows of each world matrix, as Qt Quick 3D's instance table wants them
class MatrixInstancing : public QQuick3DInstancing {
public:
    explicit MatrixInstancing(QVector<QMatrix4x4> matrices) : m_matrices(std::move(matrices)) {}

protected:
    QByteArray getInstanceBuffer(int *instanceCount) override
    {
        QByteArray buffer(qsizetype(m_matrices.size()) * qsizetype(sizeof(InstanceTableEntry)), Qt::Uninitialized);
        auto *e = reinterpret_cast<InstanceTableEntry *>(buffer.data());
        for (const QMatrix4x4 &m : std::as_const(m_matrices)) {
            e->row0 = m.row(0);
            e->row1 = m.row(1);
            e->row2 = m.row(2);
            e->color = QVector4D(1, 1, 1, 1);
            e->instanceData = QVector4D();
            ++e;
        }
        if (instanceCount) *instanceCount = int(m_matrices.size());
        return buffer;
    }

private:
    QVector<QMatrix4x4> m_matrices;
};

// world bounds of a local box under a matrix
void worldBounds(const QMatrix4x4 &m, const QVector3D &lo, const QVector3D &hi, QVector3D *outLo, QVector3D *outHi)
{
    QVector3D a(1e9f, 1e9f, 1e9f), b(-1e9f, -1e9f, -1e9f);
    for (int i = 0; i < 8; ++i) {
        const QVector3D c = m.map(QVector3D(i & 1 ? hi.x() : lo.x(), i & 2 ? hi.y() : lo.y(), i & 4 ? hi.z() : lo.z()));
        a = QVector3D(std::min(a.x(), c.x()), std::min(a.y(), c.y()), std::min(a.z(), c.z()));
        b = QVector3D(std::max(b.x(), c.x()), std::max(b.y(), c.y()), std::max(b.z(), c.z()));
    }
    *outLo = a;
    *outHi = b;
}
} // namespace

StageSet::StageSet(QQuick3DNode *parent) : QQuick3DNode(parent) {}
StageSet::~StageSet() = default;

bool StageSet::build(const PreparedStage &ps, QQmlEngine *engine, QString *error)
{
    if (!ps.stage) { if (error) *error = QStringLiteral("no stage"); return false; }
    m_id = ps.stage->id;
    setObjectName(ps.stage->name);

    // one GPU texture per file for the whole stage
    QHash<QString, GpuTexture *> gpuTextures;
    auto gpu = [&](const QString &key) -> QObject * {
        if (key.isEmpty() || !ps.textures.contains(key)) return nullptr;
        GpuTexture *&g = gpuTextures[key];
        if (!g) {
            g = new GpuTexture(ps.textures.value(key));
            g->setParent(this);
            g->setParentItem(this);
            m_extensions << g;
        }
        return g;
    };

    // placements per model
    QHash<QString, QVector<QMatrix4x4>> placements;
    for (const dir::StageInstance &si : ps.stage->instances)
        if (ps.models.contains(si.model)) placements[si.model] << si.world;

    for (auto it = placements.cbegin(); it != placements.cend(); ++it) {
        const PreparedModel &pm = ps.models[it.key()];
        for (const PreparedPiece &p : pm.pieces) {
            if (p.subsets.isEmpty()) continue;
            QVector<QMatrix4x4> worlds;
            worlds.reserve(it->size());
            for (const QMatrix4x4 &w : *it) worlds << w * p.local;
            auto *model = new QQuick3DModel;
            model->setObjectName(p.name);
            model->setParent(this);
            model->setParentItem(this);
            QQuick3DGeometry *geom = scenekit::createGeometry(p);
            geom->setParent(model);
            geom->setParentItem(model);
            model->setGeometry(geom);
            QVector<QQuick3DMaterial *> mats(p.materials.size(), nullptr);
            QQmlListProperty<QQuick3DMaterial> list = model->materials();
            for (const PreparedSubset &s : p.subsets) {
                if (!mats[s.material]) {
                    QQuick3DMaterial *mat = scenekit::createMaterial(p.materials[s.material], gpu, engine, this, error);
                    if (!mat) return false;
                    mat->setParent(model);
                    mats[s.material] = mat;
                    m_materials << mat;
                }
                list.append(&list, mats[s.material]);
            }
            // world bounds of all placements, so shadows cover them
            QVector3D lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f);
            const int shape = int(m_shapes.size());
            m_shapes.append({p.vertices, p.indices, p.stride, p.boundsMin, p.boundsMax, p.name});
            for (const QMatrix4x4 &w : std::as_const(worlds)) {
                Placement hit;
                hit.shape = shape;
                hit.world = w;
                hit.inverse = w.inverted();
                worldBounds(w, p.boundsMin, p.boundsMax, &hit.lo, &hit.hi);
                lo = QVector3D(std::min(lo.x(), hit.lo.x()), std::min(lo.y(), hit.lo.y()), std::min(lo.z(), hit.lo.z()));
                hi = QVector3D(std::max(hi.x(), hit.hi.x()), std::max(hi.y(), hit.hi.y()), std::max(hi.z(), hit.hi.z()));
                m_hits << hit;
            }
            const QMatrix4x4 &w0 = worlds.first();
            const bool mirrored = QVector3D::dotProduct(QVector3D::crossProduct(w0.column(0).toVector3D(), w0.column(1).toVector3D()), w0.column(2).toVector3D()) < 0;
            if (worlds.size() == 1 && !mirrored) {
                // a single placement is an ordinary node: culled and picked like anything else
                QVector3D t, s;
                const QMatrix4x4 &w = worlds.first();
                t = w.column(3).toVector3D();
                s = QVector3D(w.column(0).toVector3D().length(), w.column(1).toVector3D().length(), w.column(2).toVector3D().length());
                QMatrix3x3 r;
                for (int c = 0; c < 3; ++c) {
                    const QVector3D axis = w.column(c).toVector3D() / std::max(1e-6f, s[c]);
                    r(0, c) = axis.x(); r(1, c) = axis.y(); r(2, c) = axis.z();
                }
                model->setPosition(t);
                model->setRotation(QQuaternion::fromRotationMatrix(r));
                model->setScale(s);
            } else {
                auto *inst = new MatrixInstancing(worlds);
                inst->setParent(model);
                inst->setParentItem(model);
                inst->setShadowBoundsMinimum(lo);
                inst->setShadowBoundsMaximum(hi);
                model->setInstancing(inst);
            }
            m_placements += int(worlds.size());
        }
    }
    return true;
}

QVector<float> StageSet::surfacesAt(float x, float z) const
{
    QVector<float> out;
    float y = 1e5f;
    // walk down surface by surface (each cast starts just under the last hit)
    for (int i = 0; i < 64; ++i) {
        float hit = 0;
        if (!groundBelow(QVector3D(x, y, z), 2e5f, &hit)) break;
        out << hit;
        y = hit - 0.02f;
    }
    return out;
}

// Möller-Trumbore along any ray (clicks in the picture): world bounds first (slab test), then the triangles in the
// piece's own space, where the ray keeps its parameter
bool StageSet::rayHit(const QVector3D &origin, const QVector3D &dir, float *tOut, QString *name) const
{
    float best = 1e30f;
    int bestShape = -1;
    for (const Placement &h : m_hits) {
        float t0 = 0, t1 = best;
        bool miss = false;
        for (int k = 0; k < 3 && !miss; ++k) {
            const float o = origin[k], d = dir[k];
            if (std::abs(d) < 1e-9f) { if (o < h.lo[k] || o > h.hi[k]) miss = true; continue; }
            float a = (h.lo[k] - o) / d, b = (h.hi[k] - o) / d;
            if (a > b) std::swap(a, b);
            t0 = std::max(t0, a);
            t1 = std::min(t1, b);
            if (t0 > t1) miss = true;
        }
        if (miss) continue;
        const Shape &s = m_shapes[h.shape];
        const QVector3D o = h.inverse.map(origin), d = h.inverse.mapVector(dir);
        const char *vb = s.vertices.constData();
        const quint32 *ib = reinterpret_cast<const quint32 *>(s.indices.constData());
        const qsizetype tris = s.indices.size() / qsizetype(sizeof(quint32) * 3);
        const qsizetype nverts = s.stride ? s.vertices.size() / s.stride : 0;
        auto vert = [&](quint32 i) {
            float v[3];
            std::memcpy(v, vb + qsizetype(i) * s.stride, sizeof v);
            return QVector3D(v[0], v[1], v[2]);
        };
        for (qsizetype t = 0; t < tris; ++t) {
            const quint32 i0 = ib[t * 3], i1 = ib[t * 3 + 1], i2 = ib[t * 3 + 2];
            if (i0 >= nverts || i1 >= nverts || i2 >= nverts) continue;
            const QVector3D a = vert(i0), e1 = vert(i1) - a, e2 = vert(i2) - a;
            const QVector3D p = QVector3D::crossProduct(d, e2);
            const float det = QVector3D::dotProduct(e1, p);
            if (std::abs(det) < 1e-12f) continue;
            const float inv = 1.f / det;
            const QVector3D tv = o - a;
            const float u = QVector3D::dotProduct(tv, p) * inv;
            if (u < 0.f || u > 1.f) continue;
            const QVector3D q = QVector3D::crossProduct(tv, e1);
            const float v = QVector3D::dotProduct(d, q) * inv;
            if (v < 0.f || u + v > 1.f) continue;
            const float dist = QVector3D::dotProduct(e2, q) * inv;
            if (dist > 1e-4f && dist < best) { best = dist; bestShape = h.shape; }
        }
    }
    if (best >= 1e30f) return false;
    if (tOut) *tOut = best;
    if (name && bestShape >= 0) *name = m_shapes[bestShape].name;
    return true;
}

// Möller-Trumbore against the placed pieces under a vertical ray
bool StageSet::groundBelow(const QVector3D &from, float maxDrop, float *y) const
{
    float best = -1e9f;
    bool found = false;
    for (const Placement &h : m_hits) {
        if (from.x() < h.lo.x() || from.x() > h.hi.x() || from.z() < h.lo.z() || from.z() > h.hi.z()) continue;
        if (h.lo.y() > from.y() || h.hi.y() < from.y() - maxDrop || h.hi.y() < best) continue;
        const Shape &s = m_shapes[h.shape];
        const QVector3D o = h.inverse.map(from), d = h.inverse.mapVector(QVector3D(0, -1, 0));
        const char *vb = s.vertices.constData();
        const quint32 *ib = reinterpret_cast<const quint32 *>(s.indices.constData());
        const qsizetype tris = s.indices.size() / qsizetype(sizeof(quint32) * 3);
        auto vert = [&](quint32 i) {
            float v[3];
            std::memcpy(v, vb + qsizetype(i) * s.stride, sizeof v);
            return QVector3D(v[0], v[1], v[2]);
        };
        const qsizetype nverts = s.stride ? s.vertices.size() / s.stride : 0;
        for (qsizetype t = 0; t < tris; ++t) {
            const quint32 i0 = ib[t * 3], i1 = ib[t * 3 + 1], i2 = ib[t * 3 + 2];
            if (i0 >= nverts || i1 >= nverts || i2 >= nverts) continue;
            const QVector3D a = vert(i0), e1 = vert(i1) - a, e2 = vert(i2) - a;
            const QVector3D p = QVector3D::crossProduct(d, e2);
            const float det = QVector3D::dotProduct(e1, p);
            if (std::abs(det) < 1e-9f) continue;
            const float inv = 1.f / det;
            const QVector3D tv = o - a;
            const float u = QVector3D::dotProduct(tv, p) * inv;
            if (u < 0.f || u > 1.f) continue;
            const QVector3D q = QVector3D::crossProduct(tv, e1);
            const float v = QVector3D::dotProduct(d, q) * inv;
            if (v < 0.f || u + v > 1.f) continue;
            const float dist = QVector3D::dotProduct(e2, q) * inv;
            if (dist < 0.f) continue;
            const float hitY = h.world.map(o + d * dist).y();
            if (hitY <= from.y() + 1e-3f && hitY >= from.y() - maxDrop && hitY > best) { best = hitY; found = true; }
        }
    }
    if (found && y) *y = best;
    return found;
}

void StageSet::setClay(bool on)
{
    for (QObject *m : std::as_const(m_materials)) if (m) m->setProperty("clay", on ? 1.0 : 0.0);
}
