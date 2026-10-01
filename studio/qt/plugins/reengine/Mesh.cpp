#include "Mesh.h"
#include "Binary.h"
#include "Hash.h"

#include <QFloat16>
#include <QMap>
#include <algorithm>
#include <cmath>

namespace re {

using dir::MeshAsset;
using V = MeshVersion;

MeshVersion meshVersion(quint32 iv, quint32 fv)
{
    struct P { quint32 i, f; V v; };
    static const P pairs[] = {
        {352921600, 32, V::RE7}, {386270720, 1808282334, V::DMC5}, {386270720, 1808312334, V::DMC5},
        {386270720, 1902042334, V::DMC5}, {21011200, 1902042334, V::DMC5}, {21041600, 2109108288, V::RE_RT},
        {21041600, 220128762, V::RE_RT}, {21041600, 2109148288, V::RE_RT}, {21061800, 2109148288, V::RE_RT},
        {21091000, 2109148288, V::RE_RT}, {2020091500, 2101050001, V::RE8}, {220822879, 221108797, V::RE4},
        {220705151, 230110883, V::SF6}, {230403828, 230110883, V::SF6}, {230517984, 231011879, V::DD2_OLD},
        {230517984, 240423143, V::DD2}, {230727984, 240306278, V::KUNITSUGAMI}, {240704828, 240827123, V::ONIMUSHA},
        {240827123, 240827123, V::ONIMUSHA}, {240704828, 241111606, V::MHWILDS}, {250203152, 250604100, V::PRAGMATA},
        {250203152, 251215606, V::PRAGMATA}, {250707828, 250925211, V::PRAGMATA}, {250707828, 251121828, V::PRAGMATA},
        {250904410, 250925211, V::RE9},
    };
    for (const P &p : pairs)
        if (p.i == iv && p.f == fv) return p.v;
    // same internal version with an unknown file version: trust the internal one
    for (const P &p : pairs)
        if (p.i == iv) return p.v;
    return V::Unknown;
}

namespace {
constexpr quint32 kMeshMagic = 0x4853454D, kMplyMagic = 0x594C504D;

struct Header {
    quint32 magic = 0, version = 0, fileSize = 0;
    V v = V::Unknown;
    int nameCount = 0;
    qint64 lods = 0, bones = 0, mesh = 0, materialIndices = 0, boneIndices = 0, nameOffsets = 0, streamingInfo = 0;
};

bool readHeader(Reader &r, quint32 fileVersion, Header *h, QString *error)
{
    h->magic = r.u32();
    if (h->magic != kMeshMagic && h->magic != kMplyMagic) { *error = QStringLiteral("not a mesh"); return false; }
    h->version = r.u32();
    h->fileSize = r.u32();
    r.u32();                                   // lod hash
    h->v = meshVersion(h->version, fileVersion);
    if (h->v == V::Unknown) { *error = QStringLiteral("mesh version %1/%2 is not supported").arg(h->version).arg(fileVersion); return false; }
    if (h->magic == kMplyMagic) { *error = QStringLiteral("meshlet (MPLY) meshes are not supported yet"); return false; }
    auto readLodOffsets = [&]() {
        h->lods = r.i64(); r.i64(); r.i64(); r.i64(); r.i64();   // lods, shadow, occluder, normal recalc, blend shapes
        h->mesh = r.i64(); r.i64();                              // mesh, mesh group
    };
    auto readCommon = [&]() {
        r.i64(); r.i64();                                        // floats, bounds
        h->bones = r.i64();
        h->materialIndices = r.i64();
        h->boneIndices = r.i64();
        r.i64();                                                 // blend shape indices
    };
    if (h->v < V::RE4) {
        r.i16();
        h->nameCount = r.i16();
        r.i32();
        h->lods = r.i64(); r.i64(); r.i64();
        h->bones = r.i64();
        r.i64(); r.i64(); r.i64();
        h->mesh = r.i64();
        r.i64();
        h->materialIndices = r.i64();
        h->boneIndices = r.i64();
        r.i64();
        h->nameOffsets = r.i64();
    } else if (h->v < V::ONIMUSHA) {
        r.u32();
        h->nameCount = r.i16();
        r.i16();
        r.i64();                                                 // buffer headers
        readLodOffsets();
        readCommon();
        if (h->v < V::DD2_OLD) { h->streamingInfo = r.i64(); h->nameOffsets = r.i64(); }
        else { h->nameOffsets = r.i64(); r.i64(); h->streamingInfo = r.i64(); }
    } else {
        r.i32();
        h->nameCount = r.i16();
        r.u32();
        r.i16();
        r.i32(); r.i32(); r.i32();
        r.i64();                                                 // vertices
        readLodOffsets();
        readCommon();
        h->nameOffsets = r.i64();
        h->streamingInfo = r.i64();
    }
    return r.ok();
}

struct Element { int type = 0, size = 0; qint64 offset = 0; };

struct Buffer {
    qint64 vbOffset = 0, vbSize = 0, faceOffset = 0;
    QVector<Element> elements;
};

bool readBuffer(Reader &r, V v, Buffer *b)
{
    const qint64 elemOff = r.i64();
    b->vbOffset = r.i64();
    if (v >= V::RE4) {
        r.i64();                                                 // shape key weights
        r.i32();                                                 // total size
        b->vbSize = r.i32();
        b->faceOffset = b->vbOffset + b->vbSize;
    } else {
        b->faceOffset = r.i64();
        if (v == V::RE_RT) r.i64();
        if (v == V::RE8) { b->vbSize = r.u32(); r.u32(); }
        else { r.i32(); r.i32(); b->vbSize = b->faceOffset - b->vbOffset; }
    }
    const int count = r.i16();
    r.i16();
    Reader e(r.data());
    e.seek(elemOff);
    for (int i = 0; i < count; ++i) {
        Element el;
        el.type = e.i16();
        el.size = e.i16();
        el.offset = e.i32();
        b->elements << el;
    }
    return r.ok() && e.ok();
}

struct Submesh { int material = 0, bufferIndex = 0, indexCount = 0, indexOffset = 0, vertexOffset = 0, group = 0; };
} // namespace

static inline float halfToFloat(quint16 h)
{
    qfloat16 f;
    std::memcpy(&f, &h, 2);
    return float(f);
}

QSharedPointer<MeshAsset> parseMesh(const QByteArray &data, quint32 fileVersion, const QByteArray &streaming, QString *error)
{
    Q_UNUSED(streaming);
    QString err;
    Reader r(data);
    Header h;
    if (!readHeader(r, fileVersion, &h, &err)) { if (error) *error = err; return {}; }
    if (!h.mesh || !h.lods) { if (error) *error = QStringLiteral("mesh has no geometry"); return {}; }

    Buffer buf;
    r.seek(h.mesh);
    if (!readBuffer(r, h.v, &buf)) { if (error) *error = QStringLiteral("bad vertex buffer header"); return {}; }

    // ---- LOD 0 layout
    r.seek(h.lods);
    const int lodCount = r.u8();
    const int materialCount = r.u8();
    r.u8();                                                      // uv count
    r.u8();                                                      // skin weight count
    r.i16();                                                     // total mesh count
    const bool int32Faces = r.u8() != 0;
    r.u8();
    if (h.v <= V::DMC5) r.i64();
    r.vec4();                                                    // bounding sphere
    QVector3D bmin, bmax;
    if (h.v >= V::RE4) { bmin = r.vec3(); r.u32(); bmax = r.vec3(); r.u32(); }
    else { const QVector4D a = r.vec4(), b = r.vec4(); bmin = a.toVector3D(); bmax = b.toVector3D(); }
    const qint64 lodTable = r.i64();
    if (lodCount < 1) { if (error) *error = QStringLiteral("mesh has no LOD"); return {}; }
    const qint64 lod0 = r.at<qint64>(lodTable);
    r.seek(lod0);
    const int groupCount = r.u8();
    r.u8();
    if (h.v >= V::SF6) { r.u8(); r.u8(); } else r.u16();
    r.f32();
    const qint64 groupTable = r.i64();
    QVector<Submesh> subs;
    for (int g = 0; g < groupCount; ++g) {
        const qint64 off = r.at<qint64>(groupTable + g * 8);
        if (off <= 0) continue;
        Reader gr(data);
        gr.seek(off);
        const int groupId = gr.u8();
        const int subCount = gr.u8();
        gr.skip(6);
        gr.i32(); gr.i32();                                      // vertex / face counts
        for (int s = 0; s < subCount; ++s) {
            Submesh sm;
            sm.group = groupId;
            if (h.v < V::RE4) { sm.material = gr.u16(); gr.u16(); }
            else {
                sm.material = gr.u8();
                gr.u8();
                if (h.v >= V::SF6) { sm.bufferIndex = gr.u8(); gr.u8(); } else gr.u16();
                if (h.v >= V::ONIMUSHA) gr.i32();
            }
            sm.indexCount = gr.i32();
            sm.indexOffset = gr.i32();
            sm.vertexOffset = gr.i32();
            if (h.v >= V::RE8) { gr.i32(); gr.i32(); }
            if (h.v >= V::DD2) gr.i32();
            if (!gr.ok()) break;
            subs << sm;
        }
    }
    if (subs.isEmpty()) { if (error) *error = QStringLiteral("mesh has no submeshes"); return {}; }
    for (const Submesh &s : subs)
        if (s.bufferIndex != 0) { if (error) *error = QStringLiteral("streamed mesh buffers are not supported yet"); return {}; }

    auto mesh = QSharedPointer<MeshAsset>::create();
    mesh->boundsMin = bmin;
    mesh->boundsMax = bmax;

    // ---- vertex streams
    const uchar *vb = r.ptr(buf.vbOffset);
    if (!r.has(buf.vbOffset, buf.vbSize)) { if (error) *error = QStringLiteral("vertex buffer out of range"); return {}; }
    int vcount = 0;
    for (int i = 0; i < buf.elements.size(); ++i)
        if (buf.elements[i].type == 0) {
            const qint64 end = i + 1 < buf.elements.size() ? buf.elements[i + 1].offset : buf.vbSize;
            vcount = int((end - buf.elements[i].offset) / 12);
        }
    if (vcount <= 0) { if (error) *error = QStringLiteral("mesh has no positions"); return {}; }
    const bool sixInfluences = h.v == V::SF6 || h.v == V::MHWILDS || h.v == V::PRAGMATA;
    for (const Element &el : buf.elements) {
        const uchar *p = vb + el.offset;
        if (el.offset + qint64(el.size) * vcount > buf.vbSize) continue;
        switch (el.type) {
        case 0:
            mesh->positions.resize(vcount);
            for (int i = 0; i < vcount; ++i) std::memcpy(&mesh->positions[i], p + i * 12, 12);
            break;
        case 1:
            mesh->normals.resize(vcount);
            mesh->tangents.resize(vcount);
            for (int i = 0; i < vcount; ++i) {
                const auto *s = reinterpret_cast<const qint8 *>(p + i * 8);
                mesh->normals[i] = QVector3D(s[0], s[1], s[2]).normalized();
                mesh->tangents[i] = QVector4D(QVector3D(s[4], s[5], s[6]).normalized(), s[7] < 0 ? -1.f : 1.f);
            }
            break;
        case 2: case 3: {
            auto &uv = el.type == 2 ? mesh->uv0 : mesh->uv1;
            uv.resize(vcount);
            for (int i = 0; i < vcount; ++i) {
                quint16 a, b;
                std::memcpy(&a, p + i * 4, 2);
                std::memcpy(&b, p + i * 4 + 2, 2);
                uv[i] = QVector2D(halfToFloat(a), halfToFloat(b));
            }
            break;
        }
        case 4: {
            const int n = sixInfluences ? 6 : 8;
            mesh->influences = n;
            mesh->joints.resize(qsizetype(vcount) * n);
            mesh->weights.resize(qsizetype(vcount) * n);
            for (int i = 0; i < vcount; ++i) {
                const uchar *s = p + i * 16;
                if (!sixInfluences) {
                    for (int k = 0; k < 8; ++k) { mesh->joints[i * 8 + k] = s[k]; mesh->weights[i * 8 + k] = s[8 + k] / 255.f; }
                } else {
                    quint32 a, b;
                    std::memcpy(&a, s, 4);
                    std::memcpy(&b, s + 4, 4);
                    const quint16 idx[6] = {quint16(a & 0x3FF), quint16((a >> 10) & 0x3FF), quint16((a >> 20) & 0x3FF),
                                            quint16(b & 0x3FF), quint16((b >> 10) & 0x3FF), quint16((b >> 20) & 0x3FF)};
                    const float scale = 1.f + (s[14] + s[15]) / 255.f;
                    for (int k = 0; k < 6; ++k) { mesh->joints[i * 6 + k] = idx[k]; mesh->weights[i * 6 + k] = s[8 + k] / 255.f * scale; }
                }
            }
            break;
        }
        case 5:
            mesh->colors.resize(vcount);
            for (int i = 0; i < vcount; ++i) std::memcpy(&mesh->colors[i], p + i * 4, 4);
            break;
        default: break;
        }
    }
    if (mesh->positions.isEmpty()) { if (error) *error = QStringLiteral("mesh positions out of range"); return {}; }

    // ---- indices (face indices are local to each submesh's first vertex)
    const int isz = int32Faces ? 4 : 2;
    for (const Submesh &s : subs) {
        dir::MeshPart part;
        part.material = s.material;
        part.group = s.group;
        part.indexOffset = int(mesh->indices.size());
        const qint64 base = buf.faceOffset + qint64(s.indexOffset) * isz;
        if (!r.has(base, qint64(s.indexCount) * isz)) continue;
        QVector3D lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f);
        for (int k = 0; k < s.indexCount; ++k) {
            quint32 idx = int32Faces ? r.at<quint32>(base + qint64(k) * 4) : r.at<quint16>(base + qint64(k) * 2);
            idx += quint32(s.vertexOffset);
            if (idx >= quint32(vcount)) idx = 0;
            mesh->indices << idx;
            const QVector3D &pp = mesh->positions[int(idx)];
            lo = QVector3D(std::min(lo.x(), pp.x()), std::min(lo.y(), pp.y()), std::min(lo.z(), pp.z()));
            hi = QVector3D(std::max(hi.x(), pp.x()), std::max(hi.y(), pp.y()), std::max(hi.z(), pp.z()));
        }
        part.indexCount = int(mesh->indices.size()) - part.indexOffset;
        part.boundsMin = lo;
        part.boundsMax = hi;
        mesh->parts << part;
    }

    // ---- names
    QStringList names;
    if (h.nameOffsets)
        for (int i = 0; i < h.nameCount; ++i) names << r.stringAt(r.at<qint64>(h.nameOffsets + i * 8));
    if (h.materialIndices)
        for (int i = 0; i < materialCount; ++i) mesh->materialNames << names.value(r.at<qint16>(h.materialIndices + i * 2));

    // ---- skeleton
    if (h.bones) {
        Reader b(data);
        b.seek(h.bones);
        const int jointCount = b.i32();
        const int remapCount = b.i32();
        b.i32(); b.i32();
        const qint64 hierarchy = b.i64(), local = b.i64();
        b.i64();
        const qint64 invWorld = b.i64();
        for (int i = 0; i < remapCount; ++i) mesh->skinBones << b.i16();
        mesh->skeleton.bones.resize(jointCount);
        for (int i = 0; i < jointCount; ++i) {
            dir::Bone &bone = mesh->skeleton.bones[i];
            bone.parent = b.at<qint16>(hierarchy + i * 16 + 2);
            bone.symmetry = b.at<qint16>(hierarchy + i * 16 + 8);
            if (h.boneIndices) bone.name = names.value(b.at<quint16>(h.boneIndices + i * 2));
            if (bone.name.isEmpty()) bone.name = QStringLiteral("joint_%1").arg(i);
            bone.hash = hashWide(bone.name);
            Reader m(data);
            m.seek(local + i * 64);
            const QMatrix4x4 lm = m.mat4();
            m.seek(invWorld + i * 64);
            bone.inverseBind = m.mat4();
            bone.translation = lm.column(3).toVector3D();
            const QVector3D sx = lm.column(0).toVector3D(), sy = lm.column(1).toVector3D(), sz = lm.column(2).toVector3D();
            bone.scale = QVector3D(sx.length(), sy.length(), sz.length());
            QMatrix3x3 rot;
            for (int c = 0; c < 3; ++c) {
                const QVector3D axis = (c == 0 ? sx : c == 1 ? sy : sz) / std::max(1e-6f, (c == 0 ? bone.scale.x() : c == 1 ? bone.scale.y() : bone.scale.z()));
                rot(0, c) = axis.x(); rot(1, c) = axis.y(); rot(2, c) = axis.z();
            }
            bone.rotation = QQuaternion::fromRotationMatrix(rot).normalized();
        }
        if (mesh->skinBones.isEmpty())
            for (int i = 0; i < jointCount; ++i) mesh->skinBones << i;
    }
    mesh->joints.squeeze();
    return mesh;
}

} // namespace re
