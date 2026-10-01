#include "Pose.h"

#include <algorithm>

Pose bindPose(const dir::Skeleton &sk)
{
    Pose p(sk.bones.size());
    for (int i = 0; i < sk.bones.size(); ++i) p[i] = {sk.bones[i].translation, sk.bones[i].rotation, sk.bones[i].scale};
    return p;
}

int findJoint(const dir::Skeleton &sk, const QString &name)
{
    const int b = sk.find(name);
    if (b >= 0) return b;
    // RE4 / RE8 style -> RE2 / RE3 style (sides lower-case, anatomical names)
    static const QHash<QString, QStringList> aliases = {
        {QStringLiteral("upperarm"), {QStringLiteral("arm_humerus")}}, {QStringLiteral("forearm"), {QStringLiteral("arm_radius")}},
        {QStringLiteral("hand"), {QStringLiteral("arm_wrist")}}, {QStringLiteral("shoulder"), {QStringLiteral("arm_clavicle")}},
        {QStringLiteral("thigh"), {QStringLiteral("leg_femur")}}, {QStringLiteral("shin"), {QStringLiteral("leg_tibia")}},
        {QStringLiteral("foot"), {QStringLiteral("leg_ankle")}}, {QStringLiteral("toe"), {QStringLiteral("leg_ball")}},
        {QStringLiteral("wep"), {QStringLiteral("weapon")}}, {QStringLiteral("hip"), {QStringLiteral("hips")}},
        {QStringLiteral("arm_humerus"), {QStringLiteral("UpperArm")}}, {QStringLiteral("arm_radius"), {QStringLiteral("Forearm")}},
        {QStringLiteral("arm_wrist"), {QStringLiteral("Hand")}}, {QStringLiteral("arm_clavicle"), {QStringLiteral("Shoulder")}},
        {QStringLiteral("leg_femur"), {QStringLiteral("Thigh")}}, {QStringLiteral("leg_tibia"), {QStringLiteral("Shin")}},
        {QStringLiteral("leg_ankle"), {QStringLiteral("Foot")}}, {QStringLiteral("weapon"), {QStringLiteral("Wep")}},
        {QStringLiteral("hips"), {QStringLiteral("Hip")}},
    };
    QString side, rest = name;
    if (name.size() > 2 && name[1] == QLatin1Char('_') && (name[0].toLower() == QLatin1Char('l') || name[0].toLower() == QLatin1Char('r'))) {
        side = name.left(2);
        rest = name.mid(2);
    }
    for (const QString &alt : aliases.value(rest.toLower())) {
        const int i = sk.find(side + alt);
        if (i >= 0) return i;
    }
    return -1;
}

QVector<QMatrix4x4> globalMatrices(const dir::Skeleton &sk, const Pose &local)
{
    QVector<QMatrix4x4> g(sk.bones.size());
    for (int i = 0; i < sk.bones.size(); ++i) {
        const QMatrix4x4 m = i < local.size() ? local[i].matrix() : QMatrix4x4();
        const int p = sk.bones[i].parent;
        g[i] = p >= 0 && p < i ? g[p] * m : m;
    }
    return g;
}

ClipBinding::ClipBinding(const dir::AnimationClip *clip, const dir::Skeleton &sk, const Retarget &rt) : m_clip(clip)
{
    if (!clip) return;
    const int n = int(sk.bones.size());
    QHash<quint32, int> byHash;
    for (int i = 0; i < n; ++i) byHash.insert(sk.bones[i].hash, i);
    // model-space bind positions, and the face: bones below the head or below a facial deformer (neck skin)
    QVector<QVector3D> bindAt(n);
    QVector<QQuaternion> bindTurn(n);
    QVector<bool> face(n, false);
    for (int i = 0; i < n; ++i) {
        const dir::Bone &b = sk.bones[i];
        const int p = b.parent >= 0 && b.parent < i ? b.parent : -1;
        bindAt[i] = p >= 0 ? bindAt[p] + bindTurn[p].rotatedVector(b.translation) : b.translation;
        bindTurn[i] = p >= 0 ? bindTurn[p] * b.rotation : b.rotation;
        face[i] = p >= 0 && (face[p] || sk.bones[p].name.compare(QLatin1String("Head"), Qt::CaseInsensitive) == 0
                             || sk.bones[p].name.startsWith(QLatin1String("Facial"), Qt::CaseInsensitive));
    }
    const int tracks = int(clip->tracks.size());
    m_bones.reserve(tracks);
    m_moveBy.fill(QVector3D(), tracks);
    m_turnBy.fill(QQuaternion(), tracks);
    for (int k = 0; k < tracks; ++k) {
        const quint32 h = clip->tracks[k].boneHash;
        int b = byHash.value(h, -1);
        const auto rest = clip->restPose.constFind(h);
        // the top bone of a partial skeleton (facial lists start at Spine_2) is stored in model space: the game
        // masks it out, and played locally it would lift the upper body by a metre
        if (b >= 0 && sk.bones[b].parent >= 0 && rest != clip->restPose.cend() && clip->restParent.value(h, 1) == 0
            && (rest->first - bindAt[b]).length() + 0.05f < (rest->first - sk.bones[b].translation).length())
            b = -1;
        m_bones << b;
        if (b < 0 || rt.sameRig || sk.bones[b].parent < 0) continue;
        const int s = rt.source ? rt.source->findHash(h) : -1;
        if (s >= 0) {
            m_moveBy[k] = sk.bones[b].translation - rt.source->bones[s].translation;
            if (face[b]) m_turnBy[k] = (rt.source->bones[s].rotation.inverted() * sk.bones[b].rotation).normalized();
        } else if (rest != clip->restPose.cend()) {
            m_moveBy[k] = sk.bones[b].translation - rest->first;
        }
    }
    for (int k = 0; k < m_bones.size(); ++k)
        if (m_bones[k] >= 0 && sk.bones[m_bones[k]].parent < 0) { m_rootTrack = k; break; }
    // most tracks name bones this skeleton does not have: a clip of another game (RE4 on RE2's Leon ...)
    int bound = 0;
    for (int b : std::as_const(m_bones)) if (b >= 0) ++bound;
    if (rt.source && bound * 2 < tracks) setupCross(sk, *rt.source);
}

namespace {
// index of the last key at or before `f`, and the blend towards the next one
template <typename T> bool locate(const dir::Curve<T> &c, float f, int *i, float *w)
{
    const int n = int(c.times.size());
    if (n == 0 || c.values.size() < n) return false;
    if (n == 1 || f <= c.times.first()) { *i = 0; *w = 0; return true; }
    if (f >= c.times.last()) { *i = n - 1; *w = 0; return true; }
    const auto it = std::upper_bound(c.times.cbegin(), c.times.cend(), f);
    const int k = int(it - c.times.cbegin()) - 1;
    const float t0 = c.times[k], t1 = c.times[k + 1];
    *i = k;
    *w = t1 > t0 ? (f - t0) / (t1 - t0) : 0.f;
    return true;
}
} // namespace

Xform ClipBinding::rootAt(float frame) const
{
    Xform x;
    if (!m_clip || m_rootTrack < 0) return x;
    const dir::BoneTrack &tr = m_clip->tracks[m_rootTrack];
    int i;
    float f;
    if (locate(tr.translation, frame, &i, &f))
        x.t = f > 0 ? tr.translation.values[i] + (tr.translation.values[i + 1] - tr.translation.values[i]) * f : tr.translation.values[i];
    if (locate(tr.rotation, frame, &i, &f))
        x.r = f > 0 ? QQuaternion::slerp(tr.rotation.values[i], tr.rotation.values[i + 1], f) : tr.rotation.values[i];
    if (m_cross) x.t *= m_scale;                        // another game's character walks by its own leg length
    return x;
}

// ------------------------------------------------------------------ clips of another game
namespace {
// the body bones every humanoid in these games has, by each game's name for them (RE4 / RE8 style, then RE2 /
// RE3 style); `next` is the bone the chain goes on to (its direction lines the rest poses up)
struct Canon { QByteArray key; QStringList names; QByteArray next; };
const QVector<Canon> &canon()
{
    static const QVector<Canon> list = [] {
        QVector<Canon> out = {
            {"root", {QStringLiteral("root")}, {}},
            {"pelvis", {QStringLiteral("COG"), QStringLiteral("Hip"), QStringLiteral("Pelvis")}, "spine0"},
            {"spine0", {QStringLiteral("Spine_0"), QStringLiteral("Spine")}, "spine1"},
            {"spine1", {QStringLiteral("Spine_1")}, "spine2"},
            {"spine2", {QStringLiteral("Spine_2")}, "neck0"},
            {"neck0", {QStringLiteral("Neck_0"), QStringLiteral("Neck")}, "neck1"},
            {"neck1", {QStringLiteral("Neck_1")}, "head"},
            {"head", {QStringLiteral("Head")}, {}},
        };
        // one per side: "X_" is the RE4 spelling (L_ / R_), "x_" the RE2 one (l_ / r_)
        const QVector<Canon> side = {
            {"clavicle", {QStringLiteral("X_Shoulder"), QStringLiteral("x_arm_clavicle"), QStringLiteral("X_Clavicle")}, "upperarm"},
            {"upperarm", {QStringLiteral("X_UpperArm"), QStringLiteral("x_arm_humerus")}, "forearm"},
            {"forearm", {QStringLiteral("X_Forearm"), QStringLiteral("x_arm_radius")}, "hand"},
            {"hand", {QStringLiteral("X_Hand"), QStringLiteral("x_arm_wrist")}, "middle1"},
            {"thumb1", {QStringLiteral("X_Thumb1"), QStringLiteral("x_hand_thumb_0")}, "thumb2"},
            {"thumb2", {QStringLiteral("X_Thumb2"), QStringLiteral("x_hand_thumb_1")}, "thumb3"},
            {"thumb3", {QStringLiteral("X_Thumb3"), QStringLiteral("x_hand_thumb_2")}, {}},
            {"index1", {QStringLiteral("X_IndexF1"), QStringLiteral("x_hand_index_0")}, "index2"},
            {"index2", {QStringLiteral("X_IndexF2"), QStringLiteral("x_hand_index_1")}, "index3"},
            {"index3", {QStringLiteral("X_IndexF3"), QStringLiteral("x_hand_index_2")}, {}},
            {"middle1", {QStringLiteral("X_MiddleF1"), QStringLiteral("x_hand_middle_0")}, "middle2"},
            {"middle2", {QStringLiteral("X_MiddleF2"), QStringLiteral("x_hand_middle_1")}, "middle3"},
            {"middle3", {QStringLiteral("X_MiddleF3"), QStringLiteral("x_hand_middle_2")}, {}},
            {"ring1", {QStringLiteral("X_RingF1"), QStringLiteral("x_hand_ring_1")}, "ring2"},
            {"ring2", {QStringLiteral("X_RingF2"), QStringLiteral("x_hand_ring_2")}, "ring3"},
            {"ring3", {QStringLiteral("X_RingF3"), QStringLiteral("x_hand_ring_3")}, {}},
            {"pinky1", {QStringLiteral("X_PinkyF1"), QStringLiteral("x_hand_little_1")}, "pinky2"},
            {"pinky2", {QStringLiteral("X_PinkyF2"), QStringLiteral("x_hand_little_2")}, "pinky3"},
            {"pinky3", {QStringLiteral("X_PinkyF3"), QStringLiteral("x_hand_little_3")}, {}},
            {"thigh", {QStringLiteral("X_Thigh"), QStringLiteral("x_leg_femur")}, "shin"},
            {"shin", {QStringLiteral("X_Shin"), QStringLiteral("x_leg_tibia")}, "foot"},
            {"foot", {QStringLiteral("X_Foot"), QStringLiteral("x_leg_ankle")}, "toe"},
            {"toe", {QStringLiteral("X_Toe"), QStringLiteral("x_leg_ball")}, {}},
            {"weapon", {QStringLiteral("X_Wep"), QStringLiteral("x_weapon")}, {}},
        };
        for (const QString s : {QStringLiteral("L"), QStringLiteral("R")})
            for (const Canon &c : side) {
                QStringList names;
                for (const QString &v : c.names)
                    names << (v.startsWith(QLatin1String("X_")) ? s + v.mid(1) : v.startsWith(QLatin1String("x_")) ? s.toLower() + v.mid(1) : v);
                const QByteArray pre = s.toLatin1() + '_';
                out.push_back({pre + c.key, names, c.next.isEmpty() ? QByteArray() : pre + c.next});
            }
        return out;
    }();
    return list;
}

int findAny(const dir::Skeleton &sk, const QStringList &names)
{
    for (const QString &n : names) if (const int b = sk.find(n); b >= 0) return b;
    return -1;
}

void globalsOf(const dir::Skeleton &sk, const Pose &pose, QVector<QQuaternion> *rot, QVector<QVector3D> *pos)
{
    const int n = int(sk.bones.size());
    rot->resize(n);
    pos->resize(n);
    for (int i = 0; i < n; ++i) {
        const int p = sk.bones[i].parent;
        const Xform &x = pose[i];
        if (p >= 0 && p < i) { (*rot)[i] = ((*rot)[p] * x.r).normalized(); (*pos)[i] = (*pos)[p] + (*rot)[p].rotatedVector(x.t); }
        else { (*rot)[i] = x.r; (*pos)[i] = x.t; }
    }
}
} // namespace

void ClipBinding::setupCross(const dir::Skeleton &sk, const dir::Skeleton &src)
{
    m_cross = true;
    m_src = src;
    m_srcBind = bindPose(src);
    QHash<quint32, int> byHash;
    for (int i = 0; i < src.bones.size(); ++i) byHash.insert(src.bones[i].hash, i);
    m_srcTrack.clear();
    for (const dir::BoneTrack &t : m_clip->tracks) m_srcTrack << byHash.value(t.boneHash, -1);
    QVector<QQuaternion> sRot, tRot;
    globalsOf(src, m_srcBind, &sRot, &m_srcBindPos);
    globalsOf(sk, bindPose(sk), &tRot, &m_tgtBindPos);
    const int n = int(sk.bones.size());
    m_tgtParent.resize(n);
    for (int t = 0; t < n; ++t) m_tgtParent[t] = sk.bones[t].parent >= 0 && sk.bones[t].parent < t ? sk.bones[t].parent : -1;
    m_map.fill(-1, n);
    m_corr.fill(QQuaternion(), n);
    // the canonical body bones, then any bone both skeletons spell the same way
    QHash<QByteArray, QPair<int, int>> byKey;
    for (const Canon &c : canon()) {
        const int t = findAny(sk, c.names), s = findAny(src, c.names);
        if (t >= 0 && s >= 0 && m_map[t] < 0) { m_map[t] = s; byKey.insert(c.key, {t, s}); }
    }
    for (int t = 0; t < n; ++t)
        if (m_map[t] < 0) if (const int s = src.find(sk.bones[t].name); s >= 0) m_map[t] = s;
    // rest stances: each target rest bone turned to point where the source's rest bone points (arm angles differ)
    QVector<QQuaternion> stance(n);
    for (const Canon &c : canon()) {
        if (c.next.isEmpty() || !byKey.contains(c.key) || !byKey.contains(c.next)) continue;
        const QPair<int, int> a = byKey.value(c.key), b = byKey.value(c.next);
        const QVector3D dt = m_tgtBindPos[b.first] - m_tgtBindPos[a.first], ds = m_srcBindPos[b.second] - m_srcBindPos[a.second];
        if (dt.length() > 1e-4f && ds.length() > 1e-4f) stance[a.first] = QQuaternion::rotationTo(dt, ds);
    }
    for (int t = 0; t < n; ++t)
        if (const int s = m_map[t]; s >= 0) m_corr[t] = (sRot[s].inverted() * stance[t] * tRot[t]).normalized();
    const QPair<int, int> pelvis = byKey.value("pelvis", {-1, -1});
    m_pelvisT = pelvis.first;
    m_pelvisS = pelvis.second;
    if (m_pelvisT >= 0 && m_pelvisS >= 0 && m_srcBindPos[m_pelvisS].y() > 0.1f && m_tgtBindPos[m_pelvisT].y() > 0.1f)
        m_scale = m_tgtBindPos[m_pelvisT].y() / m_srcBindPos[m_pelvisS].y();
    // the root: the target's top bone; the clip's travel is read through rootAt (scaled)
    m_rootT = -1;
    for (int t = 0; t < n; ++t) if (sk.bones[t].parent < 0) { m_rootT = t; break; }
    if (m_rootT >= 0) m_tgtRootBind = bindPose(sk)[m_rootT];
    m_rootTrack = -1;
    for (int k = 0; k < m_srcTrack.size(); ++k)
        if (m_srcTrack[k] >= 0 && src.bones[m_srcTrack[k]].parent < 0) { m_rootTrack = k; break; }
}

int ClipBinding::mappedBones() const
{
    int n = 0;
    for (int s : m_map) if (s >= 0) ++n;
    return n;
}

void ClipBinding::sampleCross(float frame, Pose &pose, float weight, const QVector<float> *mask) const
{
    // the clip on its own skeleton
    Pose sp = m_srcBind;
    for (int k = 0; k < m_srcTrack.size(); ++k) {
        const int b = m_srcTrack[k];
        if (b < 0) continue;
        const dir::BoneTrack &tr = m_clip->tracks[k];
        // a partial skeleton's top (facial lists start at Spine_2) is stored in model space: it stays at rest
        if (m_src.bones[b].parent >= 0 && m_clip->restPose.contains(tr.boneHash) && m_clip->restParent.value(tr.boneHash, 1) == 0) continue;
        int i;
        float f;
        if (locate(tr.translation, frame, &i, &f))
            sp[b].t = f > 0 ? tr.translation.values[i] + (tr.translation.values[i + 1] - tr.translation.values[i]) * f : tr.translation.values[i];
        if (locate(tr.rotation, frame, &i, &f))
            sp[b].r = f > 0 ? QQuaternion::slerp(tr.rotation.values[i], tr.rotation.values[i + 1], f) : tr.rotation.values[i];
    }
    // relative to the character: the top bones stay at rest (the clip's travel reaches the target's root through
    // rootAt, once)
    for (int i = 0; i < m_src.bones.size() && i < sp.size(); ++i) if (m_src.bones[i].parent < 0) sp[i] = m_srcBind[i];
    QVector<QQuaternion> sRot;
    QVector<QVector3D> sPos;
    globalsOf(m_src, sp, &sRot, &sPos);
    // the target, parents first: matched bones take the source bone's world rotation change
    const int n = std::min(int(pose.size()), int(m_map.size()));
    QVector<QQuaternion> tRot(n);
    QVector<QVector3D> tPos(n);
    for (int b = 0; b < n; ++b) {
        Xform x = pose[b];
        const int s = m_map[b], parent = m_tgtParent[b];
        float w = weight;
        if (mask && b < mask->size()) w *= (*mask)[b];
        const QQuaternion pr = parent >= 0 ? tRot[parent] : QQuaternion();
        const QVector3D pp = parent >= 0 ? tPos[parent] : QVector3D();
        if (s >= 0 && w > 0 && b != m_rootT) {
            const QQuaternion local = (pr.inverted() * (sRot[s] * m_corr[b])).normalized();
            x.r = w >= 1.f ? local : QQuaternion::slerp(x.r, local, w);
            if (b == m_pelvisT && m_pelvisS >= 0) {
                // the hips move as the source's do, scaled to this character's leg length
                const QVector3D world = m_tgtBindPos[b] + (sPos[m_pelvisS] - m_srcBindPos[m_pelvisS]) * m_scale;
                const QVector3D t = pr.inverted().rotatedVector(world - pp);
                x.t = w >= 1.f ? t : x.t + (t - x.t) * w;
            }
        }
        pose[b] = x;
        // the root counts at rest too: its travel is set afterwards and carries everything below it
        const Xform &own = b == m_rootT ? m_tgtRootBind : x;
        tRot[b] = parent >= 0 ? (pr * own.r).normalized() : own.r;
        tPos[b] = parent >= 0 ? pp + pr.rotatedVector(own.t) : own.t;
    }
}

void ClipBinding::sample(float frame, Pose &pose, float weight, const QVector<float> *mask) const
{
    if (!m_clip) return;
    if (m_cross) { sampleCross(frame, pose, weight, mask); return; }
    for (int k = 0; k < m_bones.size(); ++k) {
        const int b = m_bones[k];
        if (b < 0 || b >= pose.size()) continue;
        float w = weight;
        if (mask && b < mask->size()) w *= (*mask)[b];
        if (w <= 0.f) continue;
        const dir::BoneTrack &tr = m_clip->tracks[k];
        Xform x = pose[b];
        int i;
        float f;
        if (locate(tr.translation, frame, &i, &f)) {
            const QVector3D v = (f > 0 ? tr.translation.values[i] + (tr.translation.values[i + 1] - tr.translation.values[i]) * f : tr.translation.values[i]) + m_moveBy[k];
            x.t = w >= 1.f ? v : x.t + (v - x.t) * w;
        }
        if (locate(tr.rotation, frame, &i, &f)) {
            const QQuaternion q = (f > 0 ? QQuaternion::slerp(tr.rotation.values[i], tr.rotation.values[i + 1], f) : tr.rotation.values[i]) * m_turnBy[k];
            x.r = w >= 1.f ? q : QQuaternion::slerp(x.r, q, w);
        }
        if (locate(tr.scale, frame, &i, &f)) {
            const QVector3D v = f > 0 ? tr.scale.values[i] + (tr.scale.values[i + 1] - tr.scale.values[i]) * f : tr.scale.values[i];
            x.s = w >= 1.f ? v : x.s + (v - x.s) * w;
        }
        pose[b] = x;
    }
}
