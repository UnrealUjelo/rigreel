#include "Mot.h"
#include "Binary.h"
#include "Hash.h"

#include <QRegularExpression>
#include <QSet>
#include <cmath>

namespace re {

using dir::AnimationClip;
using dir::AnimationSet;
using dir::BoneTrack;

namespace {
constexpr quint32 kMot = 0x20746F6D;     // "mot "
constexpr quint32 kMlst = 0x74736C6D;    // "mlst"

struct Unpack { QVector4D max, min; };

inline float bits(quint64 v, int shift, int n) { return float((v >> shift) & ((1ull << n) - 1)) / float((1ull << n) - 1); }
inline float n16(quint16 v) { return v / 65535.f; }

QQuaternion fromXyz(float x, float y, float z)
{
    const float w2 = 1.f - (x * x + y * y + z * z);
    return QQuaternion(w2 > 0 ? std::sqrt(w2) : 0.f, x, y, z);
}

// Translation / scale keys (the engine's LoadVector3* decoders)
bool readVec(Reader &r, quint32 compression, int version, const Unpack &u, QVector3D *out)
{
    const QVector4D &mx = u.max, &mn = u.min;
    const bool old = version <= 65;
    switch (compression) {
    case 0x00000: *out = r.vec3(); return true;
    case 0x20000: {
        const quint16 v = r.u16();
        const float x = bits(v, 0, 5), y = bits(v, 5, 5), z = bits(v, 10, 5);
        *out = old ? QVector3D(mx.x() * x + mn.x(), mx.y() * y + mn.z(), mx.y() * z + mn.z())
                   : QVector3D(mx.x() * x + mx.w(), mx.y() * y + mn.x(), mx.z() * z + mn.y());
        return true;
    }
    case 0x24000: { const float v = mx.x() * n16(r.u16()) + mn.x(); *out = QVector3D(v, v, v); return true; }
    case 0x44000: { const float v = mx.x() * r.f32() + mn.x(); *out = QVector3D(v, v, v); return true; }
    case 0x40000: case 0x30000: {
        if (compression == 0x30000 && !old) break;
        const quint32 v = r.u32();
        const float x = bits(v, 0, 10), y = bits(v, 10, 10), z = bits(v, 20, 10);
        *out = old ? QVector3D(mx.x() * x + mn.x(), mx.y() * y + mn.y(), mx.z() * z + mn.z())
                   : QVector3D(mx.x() * x + mx.w(), mx.y() * y + mn.x(), mx.z() * z + mn.y());
        return true;
    }
    case 0x70000: {
        const quint64 v = r.u64();
        *out = QVector3D(mx.x() * bits(v, 0, 21) + mn.x(), mx.y() * bits(v, 21, 21) + mn.y(), mx.z() * bits(v, 42, 21) + mn.z());
        return true;
    }
    case 0x80000: {
        const quint64 v = r.u64();
        *out = QVector3D(mx.x() * bits(v, 0, 21) + mx.w(), mx.y() * bits(v, 21, 21) + mn.x(), mx.z() * bits(v, 42, 21) + mn.y());
        return true;
    }
    case 0x21000: *out = QVector3D(mx.x() * n16(r.u16()) + mx.y(), mx.z(), mx.w()); return true;
    case 0x22000: *out = QVector3D(mx.y(), mx.x() * n16(r.u16()) + mx.z(), mx.w()); return true;
    case 0x23000: *out = QVector3D(mx.y(), mx.z(), mx.x() * n16(r.u16()) + mx.w()); return true;
    default: break;
    }
    if ((compression == 0x31000 && old) || (compression == 0x41000 && !old)) { *out = QVector3D(r.f32(), mx.y(), mx.z()); return true; }
    if ((compression == 0x32000 && old) || (compression == 0x42000 && !old)) { *out = QVector3D(mx.x(), r.f32(), mx.z()); return true; }
    if ((compression == 0x33000 && old) || (compression == 0x43000 && !old)) { *out = QVector3D(mx.x(), mx.y(), r.f32()); return true; }
    return false;
}

// Rotation keys (LoadQuaternions*)
bool readQuat(Reader &r, quint32 compression, int version, const Unpack &u, QQuaternion *out)
{
    const QVector4D &mx = u.max, &mn = u.min;
    auto lerp3 = [&](float x, float y, float z) {
        return fromXyz(mx.x() * x + mn.x(), mx.y() * y + mn.y(), mx.z() * z + mn.z());
    };
    switch (compression) {
    case 0x00000: { const float x = r.f32(), y = r.f32(), z = r.f32(), w = r.f32(); *out = QQuaternion(w, x, y, z); return true; }
    case 0xB0000: case 0xC0000: { const float x = r.f32(), y = r.f32(), z = r.f32(); *out = fromXyz(x, y, z); return true; }
    case 0x20000: { const quint16 v = r.u16(); *out = lerp3(bits(v, 0, 5), bits(v, 5, 5), bits(v, 10, 5)); return true; }
    case 0x21000: *out = fromXyz(mx.x() * n16(r.u16()) + mx.y(), 0, 0); return true;
    case 0x22000: *out = fromXyz(0, mx.x() * n16(r.u16()) + mx.y(), 0); return true;
    case 0x23000: *out = fromXyz(0, 0, mx.x() * n16(r.u16()) + mx.y()); return true;
    case 0x30000:
        if (version >= 78) { const float x = r.u8() / 255.f, y = r.u8() / 255.f, z = r.u8() / 255.f; *out = lerp3(x, y, z); }
        else { const quint32 v = r.u32(); *out = lerp3(bits(v, 0, 10), bits(v, 10, 10), bits(v, 20, 10)); }
        return true;
    case 0x31000: case 0x41000: *out = fromXyz(r.f32(), 0, 0); return true;
    case 0x32000: case 0x42000: *out = fromXyz(0, r.f32(), 0); return true;
    case 0x33000: case 0x43000: *out = fromXyz(0, 0, r.f32()); return true;
    case 0x40000: { const quint32 v = r.u32(); *out = lerp3(bits(v, 0, 10), bits(v, 10, 10), bits(v, 20, 10)); return true; }
    case 0x50000:
        if (version <= 65) { const float x = n16(r.u16()), y = n16(r.u16()), z = n16(r.u16()); *out = lerp3(x, y, z); }
        else {
            quint64 v = 0;
            for (int i = 0; i < 5; ++i) v = (v << 8) | r.u8();
            *out = lerp3(bits(v, 0, 13), bits(v, 13, 13), bits(v, 26, 13));
        }
        return true;
    case 0x60000: { const float x = n16(r.u16()), y = n16(r.u16()), z = n16(r.u16()); *out = lerp3(x, y, z); return true; }
    case 0x70000:
        if (version <= 65) { const quint64 v = r.u64(); *out = lerp3(bits(v, 0, 21), bits(v, 21, 21), bits(v, 42, 21)); }
        else {
            quint64 v = 0;
            for (int i = 0; i < 7; ++i) v = (v << 8) | r.u8();
            *out = lerp3(bits(v, 0, 18), bits(v, 18, 18), bits(v, 36, 18));
        }
        return true;
    case 0x80000:
        if (version >= 78) { const quint64 v = r.u64(); *out = lerp3(bits(v, 0, 21), bits(v, 21, 21), bits(v, 42, 21)); return true; }
        break;
    default: break;
    }
    return false;
}

// one .mot at `base` inside `data`
// `shared` keeps the first bone list of a motion list for the mots that carry none
bool parseMotAt(const QByteArray &data, qint64 base, AnimationClip *clip, AnimationClip *shared)
{
    Reader r(data);
    const int version = int(r.at<quint32>(base));
    if (r.at<quint32>(base + 4) != kMot) return false;
    const qint64 boneHdrPtr = r.at<qint64>(base + 16);
    const qint64 clipHdrOff = r.at<qint64>(base + 24);
    const qint64 nameOff = version >= 456 ? r.at<qint64>(base + 88) : r.at<qint64>(base + 88);
    clip->name = r.wstringAt(base + nameOff);
    clip->frames = r.at<float>(base + 96);
    const int boneCount = r.at<qint16>(base + 112);
    const int clipCount = r.at<qint16>(base + 114);
    const int fps = r.at<qint16>(base + 118);
    clip->fps = fps > 0 && fps <= 240 ? float(fps) : 60.f;

    // bone list (names + rest pose); shared by every mot of a list when a mot has none
    if (boneHdrPtr) {
        const qint64 hdr = r.at<qint64>(base + boneHdrPtr);
        const qint64 count = r.at<qint64>(base + boneHdrPtr + 8);
        if (hdr && count == boneCount) {
            QVector<quint32> hashes(boneCount);
            for (int i = 0; i < boneCount; ++i) hashes[i] = r.at<quint32>(base + hdr + qint64(i) * 80 + 68);
            for (int i = 0; i < boneCount; ++i) {
                const qint64 at = base + hdr + qint64(i) * 80;
                const QString name = r.wstringAt(base + r.at<qint64>(at));
                const QVector3D t(r.at<float>(at + 32), r.at<float>(at + 36), r.at<float>(at + 40));
                const QQuaternion q(r.at<float>(at + 60), r.at<float>(at + 48), r.at<float>(at + 52), r.at<float>(at + 56));
                const quint32 h = hashes[i];
                clip->boneNames.insert(h, name);
                clip->restPose.insert(h, {t, q});
                // the parent is a pointer to its header in the same list (0 = top of this skeleton)
                const qint64 parent = r.at<qint64>(at + 8);
                const qint64 pi = parent ? (parent - hdr) / 80 : -1;
                clip->restParent.insert(h, pi >= 0 && pi < boneCount && (parent - hdr) % 80 == 0 ? hashes[int(pi)] : 0u);
            }
            if (shared && shared->boneNames.isEmpty()) { shared->boneNames = clip->boneNames; shared->restPose = clip->restPose; shared->restParent = clip->restParent; }
        }
    }
    if (clip->boneNames.isEmpty() && shared) { clip->boneNames = shared->boneNames; clip->restPose = shared->restPose; clip->restParent = shared->restParent; }

    const int clipHdrSize = version == 65 ? 24 : version == 43 ? 16 : 12;
    for (int i = 0; i < clipCount; ++i) {
        const qint64 at = base + clipHdrOff + qint64(i) * clipHdrSize;
        const quint16 trackFlags = r.at<quint16>(at + 2);
        const quint32 hash = r.at<quint32>(at + 4);
        qint64 trackHdr;
        if (version == 65) trackHdr = r.at<qint64>(at + 16);
        else if (version == 43) trackHdr = r.at<qint64>(at + 8);
        else trackHdr = r.at<quint32>(at + 8);
        BoneTrack track;
        track.boneHash = hash;
        track.boneName = clip->boneNames.value(hash);
        Reader tr(data);
        tr.seek(base + trackHdr);
        for (int t = 0; t < 3; ++t) {
            if (!(trackFlags & (1 << t))) continue;
            const quint32 flags = tr.u32();
            const quint32 keyCount = tr.u32();
            qint64 frameIdx, frameData, unpackOff;
            if (version >= 78) { frameIdx = tr.u32(); frameData = tr.u32(); unpackOff = tr.u32(); }
            else { tr.u32(); tr.f32(); frameIdx = tr.i64(); frameData = tr.i64(); unpackOff = tr.i64(); }
            if (keyCount > 100000) return false;
            const quint32 keyFormat = flags >> 20;
            const quint32 compression = flags & 0xFF000;
            QVector<float> times;
            times.resize(qsizetype(keyCount));
            Reader kr(data);
            kr.seek(base + frameIdx);
            for (quint32 k = 0; k < keyCount; ++k)
                times[int(k)] = !frameIdx ? 0.f : keyFormat == 5 ? float(kr.u32()) : keyFormat == 2 ? float(kr.u8()) : float(kr.u16());
            Unpack u;
            if (unpackOff) {
                Reader ur(data);
                ur.seek(base + unpackOff);
                u.max = ur.vec4();
                u.min = ur.vec4();
            }
            Reader vr(data);
            vr.seek(base + frameData);
            if (t == 1) {
                track.rotation.times = times;
                for (quint32 k = 0; k < keyCount; ++k) {
                    QQuaternion q;
                    if (!readQuat(vr, compression, version, u, &q)) { track.rotation = {}; break; }
                    track.rotation.values << q.normalized();
                }
            } else {
                auto &curve = t == 0 ? track.translation : track.scale;
                curve.times = times;
                for (quint32 k = 0; k < keyCount; ++k) {
                    QVector3D v;
                    if (!readVec(vr, compression, version, u, &v)) { curve = {}; break; }
                    curve.values << v;
                }
            }
        }
        if (!track.translation.isEmpty() || !track.rotation.isEmpty() || !track.scale.isEmpty()) clip->tracks << track;
    }
    return true;
}
} // namespace

QSharedPointer<AnimationSet> parseMotlist(const QByteArray &data, QString *error)
{
    Reader r(data);
    if (r.at<quint32>(4) != kMlst) { if (error) *error = QStringLiteral("not a motion list"); return {}; }
    const int version = int(r.at<quint32>(0));
    const qint64 pointers = r.at<qint64>(16);
    const qint64 ids = r.at<qint64>(24);
    auto set = QSharedPointer<AnimationSet>::create();
    set->name = r.wstringAt(r.at<qint64>(32));
    const int count = int(r.at<quint32>(48));
    AnimationClip shared;
    QSet<qint64> seen;
    QVector<int> slot;                                  // per clip: its index in the list (and in the id table)
    for (int i = 0; i < count && i < 100000; ++i) {
        const qint64 mot = r.at<qint64>(pointers + qint64(i) * 8);
        if (!mot || seen.contains(mot) || r.at<quint32>(mot + 4) != kMot) continue;
        seen.insert(mot);
        AnimationClip clip;
        if (!parseMotAt(data, mot, &clip, &shared)) continue;
        set->clips << clip;
        slot << i;
    }
    if (set->clips.isEmpty()) { if (error) *error = QStringLiteral("no motions in the list"); return {}; }
    // motion ids: the id table's entry size and field offset differ between list versions (RE4 663: 72 bytes, id at
    // +8). Clip names carry their id ("pl00_0160_Gazing_Idle" is 160), so the layout whose ids match the most names
    // wins; the known RE4 layout is the fallback.
    if (ids) {
        static const QRegularExpression numRx(QStringLiteral("_(\\d{3,5})(?=_|$)"));
        QVector<QSet<int>> named(set->clips.size());
        for (int k = 0; k < set->clips.size(); ++k)
            for (auto it = numRx.globalMatch(set->clips[k].name); it.hasNext();) named[k].insert(it.next().captured(1).toInt());
        int bestStride = version >= 600 ? 72 : version >= 484 ? 24 : 0, bestOff = version >= 600 ? 8 : 0, bestScore = 0;
        for (int stride : {4, 8, 12, 16, 20, 24, 32, 40, 48, 56, 64, 72, 80})
            for (int off = 0; off + 2 <= stride; off += 2) {
                int score = 0;
                for (int k = 0; k < set->clips.size(); ++k)
                    if (named[k].contains(int(r.at<quint16>(ids + qint64(slot[k]) * stride + off)))) ++score;
                if (score > bestScore) { bestScore = score; bestStride = stride; bestOff = off; }
            }
        if (bestStride)
            for (int k = 0; k < set->clips.size(); ++k) set->clips[k].id = r.at<quint16>(ids + qint64(slot[k]) * bestStride + bestOff);
    }
    return set;
}

QSharedPointer<AnimationSet> parseMot(const QByteArray &data, QString *error)
{
    auto set = QSharedPointer<AnimationSet>::create();
    AnimationClip clip;
    if (!parseMotAt(data, 0, &clip, nullptr)) { if (error) *error = QStringLiteral("not a motion"); return {}; }
    set->name = clip.name;
    set->clips << clip;
    return set;
}

} // namespace re
