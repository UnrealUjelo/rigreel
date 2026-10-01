// The rig pass of the standalone runtime: what is kept true on top of the animation every frame. Walk paths place
// their characters (path.lua), heads turn towards what they look at (lookat.lua), hands and feet hold on or stay
// planted by two-bone IK (constraint.lua, ik.lua), props ride on joints (extras.lua attachments). Plus the ops
// that edit them and the viewport pen (clicks on the floor add path points or place a character).
#include "Maths.h"
#include "StageRuntime.h"

#include <QRegularExpression>
#include <cmath>

using namespace film;

namespace {
double num(const QJsonObject &c, const char *k, double def = 0) { const QJsonValue v = c.value(QLatin1String(k)); return v.isDouble() ? v.toDouble() : def; }
qint64 addr(const QJsonObject &c) { return qint64(c.value(QStringLiteral("addr")).toDouble()); }
QVector3D v3(const QJsonValue &v, const QVector3D &def = {}) { return fm::vec3(v, def); }
QString who(const Actor &a) { return a.displayName.isEmpty() ? a.name : a.displayName; }

// limb chains: upper bone, middle bone, effector
QStringList chainBones(const QString &chain)
{
    if (chain == QLatin1String("L_Hand")) return {QStringLiteral("L_UpperArm"), QStringLiteral("L_Forearm"), QStringLiteral("L_Hand")};
    if (chain == QLatin1String("R_Hand")) return {QStringLiteral("R_UpperArm"), QStringLiteral("R_Forearm"), QStringLiteral("R_Hand")};
    if (chain == QLatin1String("L_Foot")) return {QStringLiteral("L_Thigh"), QStringLiteral("L_Shin"), QStringLiteral("L_Foot")};
    if (chain == QLatin1String("R_Foot")) return {QStringLiteral("R_Thigh"), QStringLiteral("R_Shin"), QStringLiteral("R_Foot")};
    return {};
}

// model-space positions and rotations of a pose (relative to the actor)
struct Rig {
    QVector<QVector3D> pos;
    QVector<QQuaternion> rot;
};
Rig rigOf(const dir::Skeleton &sk, const Pose &pose)
{
    Rig r;
    const int n = int(sk.bones.size());
    r.pos.resize(n);
    r.rot.resize(n);
    const QVector<QMatrix4x4> g = globalMatrices(sk, pose);
    for (int i = 0; i < n; ++i) {
        r.pos[i] = g[i].column(3).toVector3D();
        const int p = sk.bones[i].parent;
        const QQuaternion local = i < pose.size() ? pose[i].r : sk.bones[i].rotation;
        r.rot[i] = (p >= 0 && p < i ? r.rot[p] * local : local).normalized();
    }
    return r;
}

QMatrix4x4 worldOf(const ActorFrame &f)
{
    QMatrix4x4 m;
    m.translate(f.pos);
    m.rotate(f.rot);
    m.scale(f.scale);
    return m;
}

QQuaternion clampTurn(const QQuaternion &q, float maxDeg)
{
    QVector3D axis;
    float angle = 0;
    q.getAxisAndAngle(&axis, &angle);
    if (angle > 180) { angle = 360 - angle; axis = -axis; }
    return angle > maxDeg ? QQuaternion::fromAxisAndAngle(axis, maxDeg) : q;
}

// ik.lua: aim the chain at the goal, bend the middle joint by the law of cosines (keeping the current bend plane),
// re-aim; the effector keeps its orientation. Goal in model space; the result blends in by weight.
bool twoBoneIk(const dir::Skeleton &sk, Pose &pose, const QString &chain, const QVector3D &goal, float weight)
{
    const QStringList names = chainBones(chain);
    if (names.size() != 3) return false;
    const int ia = findJoint(sk, names[0]), ib = findJoint(sk, names[1]), ic = findJoint(sk, names[2]);
    if (ia < 0 || ib < 0 || ic < 0 || ic >= pose.size() || sk.bones[ib].parent != ia || sk.bones[ic].parent != ib) return false;
    const Rig r = rigOf(sk, pose);
    QVector3D a = r.pos[ia], b = r.pos[ib], c = r.pos[ic];
    QQuaternion qa = r.rot[ia], qb = r.rot[ib];
    const QQuaternion qc = r.rot[ic];
    const int pa = sk.bones[ia].parent;
    const QQuaternion qp = pa >= 0 ? r.rot[pa] : QQuaternion();
    const float l1 = (b - a).length(), l2 = (c - b).length();
    if (l1 < 1e-4f || l2 < 1e-4f) return false;
    const QVector3D toT = goal - a;
    const float d = std::clamp(toT.length(), 0.02f, (l1 + l2) * 0.999f);
    QVector3D n = QVector3D::crossProduct(b - a, c - b);
    if (n.length() < 1e-5f) n = QVector3D::crossProduct(toT, QVector3D(0, 1, 0));
    if (n.length() < 1e-5f) n = QVector3D::crossProduct(toT, QVector3D(1, 0, 0));
    n.normalize();
    // 1) the chain points at the goal
    const QQuaternion q1 = QQuaternion::rotationTo(c - a, toT);
    qa = q1 * qa;
    qb = q1 * qb;
    b = a + q1.rotatedVector(b - a);
    c = a + q1.rotatedVector(c - a);
    n = q1.rotatedVector(n);
    // 2) the middle joint bends to the angle that makes the reach (try both ways round the plane normal)
    const float want = std::acos(std::clamp((l1 * l1 + l2 * l2 - d * d) / (2 * l1 * l2), -1.f, 1.f));
    const float have = std::acos(std::clamp(QVector3D::dotProduct((a - b).normalized(), (c - b).normalized()), -1.f, 1.f));
    const float deg = (want - have) * float(180 / fm::kPi);
    QQuaternion q2 = QQuaternion::fromAxisAndAngle(n, deg);
    const QVector3D c1 = b + q2.rotatedVector(c - b);
    const QQuaternion q2b = QQuaternion::fromAxisAndAngle(n, -deg);
    const QVector3D c2 = b + q2b.rotatedVector(c - b);
    if (std::abs((c2 - a).length() - d) < std::abs((c1 - a).length() - d)) { q2 = q2b; c = c2; }
    else c = c1;
    qb = q2 * qb;
    // 3) re-aim so the effector lands on the goal
    const QQuaternion q3 = QQuaternion::rotationTo(c - a, toT);
    qa = q3 * qa;
    qb = q3 * qb;
    const float w = std::clamp(weight, 0.f, 1.f);
    pose[ia].r = fm::slerp(pose[ia].r, (qp.inverted() * qa).normalized(), w);
    pose[ib].r = fm::slerp(pose[ib].r, (qa.inverted() * qb).normalized(), w);
    pose[ic].r = fm::slerp(pose[ic].r, (qb.inverted() * qc).normalized(), w);
    return true;
}
} // namespace

// ------------------------------------------------------------------ posing
namespace {
// the bones a person poses: no twist / helper / muscle / end bones, no face, hair or cloth
bool posable(const dir::Skeleton &sk, int b, const QVector<bool> &face)
{
    static const QRegularExpression skip(QStringLiteral("twist|help|offset|muscle|null|end$|^_|_s$|scale|adjust|dummy|cloth|hair|skirt|bust|breast|jiggle|"
                                                        "phys|chain|attach|wep|weapon|pole|prop|reference|^root$|knit|coat|jacket|shirt|pants|sleeve|flap|tail|bag|strap|belt|holster|"
                                                        "collar|hood|scarf|tie"),
                                         QRegularExpression::CaseInsensitiveOption);
    return !face[b] && !skip.match(sk.bones[b].name).hasMatch();
}
} // namespace

// the selected character's posable bones in the world: [{n, p: [x, y, z], up: index of the parent in this list}]
QJsonArray StageRuntime::skeletonState() const
{
    const Actor *a = m_doc.actor(m_selActor);
    const ActorFrame *f = a ? frameOf(a->id) : nullptr;
    const PreparedModel *pm = a ? prepared(a->id) : nullptr;
    if (!f || !pm || !pm->model || a->kind == QLatin1String("object")) return {};
    const dir::Skeleton &sk = pm->model->skeleton;
    const int n = int(sk.bones.size());
    QVector<bool> face(n, false);
    for (int i = 0; i < n; ++i) {
        const int p = sk.bones[i].parent;
        face[i] = p >= 0 && p < i && (face[p] || sk.bones[p].name.compare(QLatin1String("Head"), Qt::CaseInsensitive) == 0
                                      || sk.bones[p].name.startsWith(QLatin1String("Facial"), Qt::CaseInsensitive));
    }
    const Rig r = rigOf(sk, f->pose.isEmpty() ? bindPose(sk) : f->pose);
    const QMatrix4x4 w = worldOf(*f);
    QVector<int> index(n, -1);
    QJsonArray out;
    for (int i = 0; i < n; ++i) {
        if (!posable(sk, i, face)) continue;
        int up = sk.bones[i].parent;
        while (up >= 0 && index[up] < 0) up = sk.bones[up].parent;             // the nearest shown ancestor
        index[i] = int(out.size());
        out.append(QJsonObject{{QStringLiteral("n"), sk.bones[i].name}, {QStringLiteral("p"), fm::arr(w.map(r.pos[i]))},
                               {QStringLiteral("up"), up >= 0 ? index[up] : -1}});
    }
    return out;
}

QString StageRuntime::effectorChain(qint64 actorId, const QString &joint) const
{
    const PreparedModel *pm = prepared(actorId);
    if (!pm || !pm->model || joint.isEmpty()) return {};
    const int b = findJoint(pm->model->skeleton, joint);
    for (const QString &chain : {QStringLiteral("L_Hand"), QStringLiteral("R_Hand"), QStringLiteral("L_Foot"), QStringLiteral("R_Foot")})
        if (b >= 0 && findJoint(pm->model->skeleton, chain) == b) return chain;
    return {};
}

// the chain's three bones solved so the effector reaches `goalWorld`, from the pose the drag started on
bool StageRuntime::solveIkPose(qint64 actorId, const Pose &start, const QString &chain, const QVector3D &goalWorld, QMap<QString, QQuaternion> *out) const
{
    const PreparedModel *pm = prepared(actorId);
    const ActorFrame *f = frameOf(actorId);
    if (!pm || !pm->model || !f || start.isEmpty()) return false;
    const dir::Skeleton &sk = pm->model->skeleton;
    Pose pose = start;
    if (!twoBoneIk(sk, pose, chain, worldOf(*f).inverted().map(goalWorld), 1.f)) return false;
    for (const QString &name : chainBones(chain)) {
        const int b = findJoint(sk, name);
        if (b >= 0 && b < pose.size()) out->insert(sk.bones[b].name, pose[b].r);
    }
    return true;
}

std::optional<QQuaternion> StageRuntime::currentLocal(const Actor &a, const QString &joint) const
{
    const PreparedModel *pm = prepared(a.id);
    const ActorFrame *f = frameOf(a.id);
    if (!pm || !pm->model || joint.isEmpty()) return std::nullopt;
    const int b = findJoint(pm->model->skeleton, joint);
    if (b < 0) return std::nullopt;
    if (a.work.contains(pm->model->skeleton.bones[b].name)) return a.work.value(pm->model->skeleton.bones[b].name);
    if (f && b < f->pose.size()) return f->pose[b].r;
    return pm->model->skeleton.bones[b].rotation;
}

// ------------------------------------------------------------------ queries
bool StageRuntime::jointWorld(const ActorFrame &f, const QString &joint, QVector3D *pos, QQuaternion *rot) const
{
    const PreparedModel *pm = prepared(f.id);
    if (!pm || !pm->model) return false;
    const dir::Skeleton &sk = pm->model->skeleton;
    const int b = findJoint(sk, joint);
    if (b < 0) return false;
    const Rig r = rigOf(sk, f.pose.isEmpty() ? bindPose(sk) : f.pose);
    if (pos) *pos = worldOf(f).map(r.pos[b]);
    if (rot) *rot = (f.rot * r.rot[b]).normalized();
    return true;
}

const ActorFrame *StageRuntime::frameOf(qint64 id) const
{
    for (const ActorFrame &f : m_frames) if (f.id == id) return &f;
    return nullptr;
}

QVector3D StageRuntime::constraintTarget(const Constraint &c, bool *ok) const
{
    *ok = true;
    if (c.point && (c.kind == QLatin1String("plant") || !c.targetActor)) return *c.point;
    if (const ActorFrame *tf = frameOf(c.targetActor)) {
        QVector3D p;
        if (!c.targetJoint.isEmpty() && jointWorld(*tf, c.targetJoint, &p, nullptr)) return p;
        return tf->pos;
    }
    *ok = false;
    return {};
}

// ------------------------------------------------------------------ walk paths
Track *StageRuntime::pathTrack(qint64 actorId)
{
    for (Track &tr : m_doc.seq.tracks) if (tr.kind == QLatin1String("path") && tr.actor == actorId) return &tr;
    return nullptr;
}

// where each walked path puts its character at t (ground-snapped, facing along the curve)
void StageRuntime::placePaths(double t, QHash<qint64, QPair<QVector3D, QQuaternion>> *out)
{
    m_pathLocked.clear();
    for (const Track &tr : std::as_const(m_doc.seq.tracks)) {
        if (tr.kind != QLatin1String("path") || !tr.actor || tr.path.points.size() < 2) continue;
        m_pathLocked.insert(tr.actor);                   // the path moves it: its clips play in place
        if (t < tr.path.start - 0.5) continue;
        const PathCurve c = pathCurve(tr.path.points);
        QVector3D pos, tan;
        if (!pathAt(c, pathDistanceAt(tr.path, c.length, t), &pos, &tan)) continue;
        float gy = 0;
        if (groundBelow(pos + QVector3D(0, 1.5f, 0), 3.f, &gy) && std::abs(gy - pos.y()) < 1.5f) pos.setY(gy);
        out->insert(tr.actor, {pos, fm::fromEulerDeg(0, std::atan2(tan.x(), tan.z()) * 180 / fm::kPi, 0)});
    }
}

// the path's duration and its walk clip follow the points (path.lua sync_clip): the clip's playback speed makes the
// feet match the ground
void StageRuntime::refreshPath(qint64 actorId)
{
    Track *pt = pathTrack(actorId);
    if (!pt) return;
    PathSpec &p = pt->path;
    const PathCurve c = pathCurve(p.points);
    if (!p.durPinned) p.dur = pathDuration(p, c.length);
    grow(p.start + p.dur);
    if (!p.clipId) return;
    for (Track &tr : m_doc.seq.tracks) {
        if (tr.kind != QLatin1String("anim") || tr.actor != actorId) continue;
        for (AnimClip &clip : tr.clips) {
            if (clip.id != p.clipId) continue;
            clip.start = p.start;
            clip.dur = p.dur;
            clip.loop = true;
            if (p.clipSpeedRef > 0.05 && c.length > 0) clip.speed = std::clamp(c.length / (p.dur / 60.0) / p.clipSpeedRef, 0.3, 3.0);
        }
    }
}

// the character's own general motion list; else the same person's other rig (Ada -> Separate Ways); with
// `anyone`, else the first character's that has one (a humanoid walking with Leon's legs)
QString StageRuntime::generalFor(const Actor &a, bool anyone) const
{
    QString person;
    const QJsonArray cast = castOf(a);
    for (const QJsonValue &v : cast) {
        const QJsonObject ch = v.toObject();
        if (ch.value(QStringLiteral("id")).toString() != a.castId) continue;
        const QString g = ch.value(QStringLiteral("general")).toString();
        if (!g.isEmpty()) return g;
        person = ch.value(QStringLiteral("name")).toString().section(QStringLiteral(" ("), 0, 0);
    }
    for (const QJsonValue &v : cast) {
        const QJsonObject ch = v.toObject();
        const QString g = ch.value(QStringLiteral("general")).toString();
        if (g.isEmpty()) continue;
        if (!person.isEmpty() && ch.value(QStringLiteral("name")).toString().section(QStringLiteral(" ("), 0, 0) == person) return g;
    }
    if (!anyone) return {};
    const PreparedModel *pm = prepared(a.id);
    if (!pm || !pm->model || findJoint(pm->model->skeleton, QStringLiteral("L_Thigh")) < 0) return {};
    for (const QJsonValue &v : cast) {
        const QString g = v.toObject().value(QStringLiteral("general")).toString();
        if (!g.isEmpty()) return g;
    }
    return {};
}

// put a walk / jog loop of the character's locomotion list on its first anim layer, spanning the path
void StageRuntime::attachPathClip(qint64 actorId, const QString &gait)
{
    const Actor *a = m_doc.actor(actorId);
    if (!a) return;
    const QString list = generalFor(*a, true);
    if (list.isEmpty()) { log(QStringLiteral("warn"), QStringLiteral("path: no walking clips for %1").arg(who(*a))); return; }
    ensureAnimations(list, [this, actorId, gait, list]() {
        const dir::AnimationSetPtr set = animations(list);
        if (!set || !pathTrack(actorId)) return;
        static const QStringList walk{QStringLiteral("walk_f_loop"), QStringLiteral("walk_front_loop"), QStringLiteral("walk_loop")};
        static const QStringList jog{QStringLiteral("jog_loop_vera"), QStringLiteral("jog_loop"), QStringLiteral("run_loop"), QStringLiteral("jog_f_loop"),
                                     QStringLiteral("jog_straight_loop")};
        const dir::AnimationClip *pick = nullptr;
        for (const QString &pat : gait == QLatin1String("jog") ? jog : walk) {
            for (const dir::AnimationClip &clip : set->clips) {
                const QString n = clip.name.toLower();
                if (n.contains(pat) && !n.contains(QLatin1String("stairs")) && !n.contains(QLatin1String("curve"))) { pick = &clip; break; }
            }
            if (pick) break;
        }
        if (!pick) { log(QStringLiteral("warn"), QStringLiteral("path: no %1 clip in %2").arg(gait, list.section(QLatin1Char('/'), -1))); return; }
        Track &anim = track(QStringLiteral("anim"), actorId, 0);      // may grow the track list: find the path after
        PathSpec &p = pathTrack(actorId)->path;
        if (p.clipId) anim.clips.removeIf([&](const AnimClip &c) { return c.id == p.clipId; });
        AnimClip clip;
        clip.id = newId();
        clip.start = p.start;
        clip.dur = p.dur;
        clip.path = list;
        clip.mot = pick->id >= 0 ? pick->id : int(pick - set->clips.constData());
        clip.name = pick->name;
        clip.endframe = pick->frames;
        clip.loop = true;
        clip.blend = 10;
        anim.clips << clip;
        std::stable_sort(anim.clips.begin(), anim.clips.end(), [](const AnimClip &x, const AnimClip &y) { return x.start < y.start; });
        p.clipId = clip.id;
        p.clipName = pick->name;
        p.gait = gait;
        // the clip's own ground speed, from its root travel over one loop
        const double fallback = gait == QLatin1String("jog") ? 3.1 : 1.35;
        p.clipSpeedRef = fallback;
        if (const ClipBinding *b = binding(actorId, list, clip.mot); b && b->frames() > 1) {
            QVector3D d = b->rootAt(b->frames()).t - b->rootAt(0).t;
            d.setY(0);
            const double perSecond = d.length() / (b->frames() / 60.0);
            if (perSecond > 0.3) p.clipSpeedRef = perSecond;
        }
        if (!p.speedPinned) p.speed = p.clipSpeedRef;
        refreshPath(actorId);
        markData();
        if (!m_playing) evaluate(m_t);
        publish();
    });
}

// ------------------------------------------------------------------ the pass
void StageRuntime::solveRigs(double t)
{
    QHash<qint64, int> at;
    for (int i = 0; i < m_frames.size(); ++i) at.insert(m_frames[i].id, i);
    auto frame = [&](qint64 id) -> ActorFrame * { const int i = at.value(id, -1); return i >= 0 ? &m_frames[i] : nullptr; };
    auto skeleton = [&](qint64 id) -> const dir::Skeleton * { const PreparedModel *pm = prepared(id); return pm && pm->model ? &pm->model->skeleton : nullptr; };
    auto active = [t](const Constraint &c) { return c.enabled && (!c.range || (t >= c.range->first && t <= c.range->second)); };

    // heads: the Look at settings, or a look constraint in its frames
    QHash<qint64, LookAt> looks;
    for (const Actor &a : std::as_const(m_doc.actors)) if (a.lookat.enabled && a.lookat.weight > 0.001) looks.insert(a.id, a.lookat);
    for (const Constraint &c : std::as_const(m_doc.constraints)) {
        if (c.kind != QLatin1String("look") || !active(c)) continue;
        const Actor *a = m_doc.actor(c.actor);
        bool ok = false;
        const QVector3D p = constraintTarget(c, &ok);
        if (!a || !ok) continue;
        LookAt l = a->lookat;
        l.enabled = true;
        l.kind = QStringLiteral("point");
        l.point = p + c.offset;
        l.weight = c.weight;
        looks.insert(c.actor, l);
    }
    for (auto it = looks.cbegin(); it != looks.cend(); ++it) {
        ActorFrame *f = frame(it.key());
        const dir::Skeleton *sk = skeleton(it.key());
        if (!f || !sk || f->pose.isEmpty()) continue;
        const LookAt &l = it.value();
        QVector3D target;
        if (l.kind == QLatin1String("point")) target = l.point;
        else if (l.kind == QLatin1String("actor")) {
            const ActorFrame *tf = frame(l.target);
            if (!tf) continue;
            if (!jointWorld(*tf, QStringLiteral("Head"), &target, nullptr)) target = tf->pos + QVector3D(0, 1.55f, 0);
        } else target = currentView().pos;
        const int head = findJoint(*sk, QStringLiteral("Head"));
        if (head < 0) continue;
        const int neck = findJoint(*sk, QStringLiteral("Neck_1"));
        const Rig r = rigOf(*sk, f->pose);
        const QVector3D dir = worldOf(*f).inverted().map(target) - r.pos[head];
        if (dir.length() < 0.05f) continue;
        const QQuaternion goal = clampTurn(QQuaternion::rotationTo(QVector3D(0, 0, l.flip ? -1 : 1), dir.normalized()), float(l.maxDeg));
        // smoothing follows film time while playing or rendering (deterministic), snaps when scrubbing
        auto state = m_lookCur.find(it.key());
        const double dt = state != m_lookCur.end() ? t - state->first : -1;
        QQuaternion cur = goal;
        if (state != m_lookCur.end() && (m_playing || m_rendering) && dt > 0 && dt <= 3) {
            const double alpha = 1 - std::pow(1 - std::clamp(l.smooth, 0.01, 1.0), dt);
            cur = fm::slerp(state->second, goal, float(alpha));
        }
        m_lookCur.insert(it.key(), {t, cur});
        const QQuaternion d = fm::slerp(QQuaternion(), cur, float(std::clamp(l.weight, 0.0, 1.0)));
        const QQuaternion headNew = (d * r.rot[head]).normalized();
        if (neck >= 0 && sk->bones[head].parent == neck) {
            const int np = sk->bones[neck].parent;
            const QQuaternion neckNew = (fm::slerp(QQuaternion(), d, float(std::clamp(l.neckShare, 0.0, 1.0))) * r.rot[neck]).normalized();
            f->pose[neck].r = ((np >= 0 ? r.rot[np] : QQuaternion()).inverted() * neckNew).normalized();
            f->pose[head].r = (neckNew.inverted() * headNew).normalized();
        } else {
            const int hp = sk->bones[head].parent;
            f->pose[head].r = ((hp >= 0 ? r.rot[hp] : QQuaternion()).inverted() * headNew).normalized();
        }
    }

    // hands and feet: held on something or planted
    for (const Constraint &c : std::as_const(m_doc.constraints)) {
        if (c.kind == QLatin1String("look") || !active(c)) continue;
        ActorFrame *f = frame(c.actor);
        const dir::Skeleton *sk = skeleton(c.actor);
        if (!f || !sk || f->pose.isEmpty()) continue;
        bool ok = false;
        const QVector3D goal = constraintTarget(c, &ok);
        if (!ok) continue;
        twoBoneIk(*sk, f->pose, c.chain, worldOf(*f).inverted().map(goal + c.offset), float(c.weight));
    }

    // props riding on joints (twice: a prop can ride on a prop)
    for (int pass = 0; pass < 2; ++pass)
        for (const Actor &a : std::as_const(m_doc.actors)) {
            if (!a.attached) continue;
            ActorFrame *f = frame(a.id);
            const ActorFrame *host = frame(a.attached->actor);
            QVector3D jp;
            QQuaternion jr;
            if (!f || !host || !jointWorld(*host, a.attached->joint, &jp, &jr)) continue;
            f->pos = jp + jr.rotatedVector(a.attached->pos);
            f->rot = (jr * a.attached->rot).normalized();
        }
}

// ------------------------------------------------------------------ state
QJsonArray StageRuntime::constraintsState() const
{
    QJsonArray out;
    for (const Constraint &c : m_doc.constraints) {
        QJsonObject o{{QStringLiteral("id"), c.id}, {QStringLiteral("kind"), c.kind}, {QStringLiteral("name"), c.name}, {QStringLiteral("actor"), double(c.actor)},
                      {QStringLiteral("chain"), c.chain}, {QStringLiteral("offset"), fm::arr(c.offset)}, {QStringLiteral("weight"), c.weight},
                      {QStringLiteral("enabled"), c.enabled}};
        if (const Actor *a = m_doc.actor(c.actor)) o.insert(QStringLiteral("actor_name"), who(*a));
        if (c.targetActor) {
            o.insert(QStringLiteral("target_actor"), double(c.targetActor));
            if (const Actor *t = m_doc.actor(c.targetActor)) o.insert(QStringLiteral("target_name"), who(*t));
        } else if (c.point) o.insert(QStringLiteral("target_name"), c.kind == QLatin1String("plant") ? QStringLiteral("where it was") : QStringLiteral("a fixed point"));
        if (!c.targetJoint.isEmpty()) o.insert(QStringLiteral("target_joint"), c.targetJoint);
        if (c.range) o.insert(QStringLiteral("range"), QJsonArray{c.range->first, c.range->second});
        out.append(o);
    }
    return out;
}

// ------------------------------------------------------------------ ops
void StageRuntime::registerRigOps()
{
    auto &o = m_ops;
    registerDriveOps();
    registerImportOps();
    registerGizmoOps();
    m_readonly << QStringLiteral("record") << QStringLiteral("path_finish") << QStringLiteral("pen_cancel") << QStringLiteral("set_speed") << QStringLiteral("set_frame")
               << QStringLiteral("set_paused") << QStringLiteral("cast_place");

    // ---- props on joints (the prop is the selection or addr, the character is `actor`)
    o[QStringLiteral("attach")] = [this](const QJsonObject &c) {
        Actor *obj = argActor(c);
        const qint64 host = qint64(c.value(QStringLiteral("actor")).toDouble());
        if (!obj || !host || host == obj->id || !m_doc.actor(host)) return;
        Attachment at{host, c.value(QStringLiteral("joint")).toString(QStringLiteral("R_Wep")), {}, {}};
        const ActorFrame *hf = frameOf(host), *of = frameOf(obj->id);
        QVector3D jp;
        QQuaternion jr;
        if (hf && !jointWorld(*hf, at.joint, &jp, &jr)) { log(QStringLiteral("warn"), QStringLiteral("attach: no joint %1").arg(at.joint)); return; }
        if (c.value(QStringLiteral("keep")).toBool() && hf && of) {
            // offsets that keep the prop where it is now, relative to the joint
            at.pos = jr.inverted().rotatedVector(of->pos - jp);
            at.rot = (jr.inverted() * of->rot).normalized();
        } else {
            at.pos = v3(c.value(QStringLiteral("pos")));
            if (c.contains(QStringLiteral("euler"))) { const QVector3D e = v3(c.value(QStringLiteral("euler"))); at.rot = fm::fromEulerDeg(e.x(), e.y(), e.z()); }
        }
        obj->attached = at;
    };
    o[QStringLiteral("detach")] = [this](const QJsonObject &c) {
        Actor *obj = argActor(c);
        if (!obj || !obj->attached) return;
        if (const ActorFrame *f = frameOf(obj->id)) { obj->pos = f->pos; obj->rot = f->rot; }   // it stays where it is
        obj->attached.reset();
    };
    o[QStringLiteral("attach_offset")] = [this](const QJsonObject &c) {
        Actor *obj = argActor(c);
        if (!obj || !obj->attached) return;
        if (c.contains(QStringLiteral("pos"))) obj->attached->pos = v3(c.value(QStringLiteral("pos")));
        if (c.contains(QStringLiteral("euler"))) { const QVector3D e = v3(c.value(QStringLiteral("euler"))); obj->attached->rot = fm::fromEulerDeg(e.x(), e.y(), e.z()); }
    };

    // ---- look at
    o[QStringLiteral("lookat_set")] = [this](const QJsonObject &c) {
        Actor *a = argActor(c);
        if (!a) return;
        const QJsonObject f = c.value(QStringLiteral("fields")).toObject();
        LookAt &l = a->lookat;
        if (f.contains(QStringLiteral("enabled"))) l.enabled = f.value(QStringLiteral("enabled")).toBool();
        if (f.contains(QStringLiteral("kind"))) l.kind = f.value(QStringLiteral("kind")).toString();
        if (f.contains(QStringLiteral("target"))) l.target = qint64(f.value(QStringLiteral("target")).toDouble());
        if (f.contains(QStringLiteral("point"))) l.point = v3(f.value(QStringLiteral("point")));
        if (f.contains(QStringLiteral("weight"))) l.weight = std::clamp(f.value(QStringLiteral("weight")).toDouble(), 0.0, 1.0);
        if (f.contains(QStringLiteral("max_deg"))) l.maxDeg = std::clamp(f.value(QStringLiteral("max_deg")).toDouble(), 5.0, 180.0);
        if (f.contains(QStringLiteral("smooth"))) l.smooth = std::clamp(f.value(QStringLiteral("smooth")).toDouble(), 0.01, 1.0);
        if (f.contains(QStringLiteral("neck_share"))) l.neckShare = std::clamp(f.value(QStringLiteral("neck_share")).toDouble(), 0.0, 1.0);
        if (f.contains(QStringLiteral("flip"))) l.flip = f.value(QStringLiteral("flip")).toBool();
        m_lookCur.remove(a->id);
    };

    // ---- constraints
    o[QStringLiteral("constraint_add")] = [this](const QJsonObject &c) {
        Actor *a = argActor(c);
        if (!a) return;
        const QString kind = c.value(QStringLiteral("kind")).toString(QStringLiteral("ik_pin"));
        // "hold with both hands" is two pins on the same target
        const QStringList chains = c.value(QStringLiteral("both")).toBool() ? QStringList{QStringLiteral("L_Hand"), QStringLiteral("R_Hand")}
                                                                          : QStringList{c.value(QStringLiteral("chain")).toString(QStringLiteral("R_Hand"))};
        for (const QString &chain : chains) {
            Constraint k;
            k.id = newId();
            k.kind = kind;
            k.actor = a->id;
            k.chain = chain;
            k.targetActor = qint64(c.value(QStringLiteral("target_actor")).toDouble());
            k.targetJoint = c.value(QStringLiteral("target_joint")).toString();
            if (c.contains(QStringLiteral("point"))) k.point = v3(c.value(QStringLiteral("point")));
            k.offset = v3(c.value(QStringLiteral("offset")));
            k.weight = num(c, "weight", 1);
            if (const QJsonArray r = c.value(QStringLiteral("range")).toArray(); r.size() == 2) k.range = std::make_pair(r[0].toDouble(), r[1].toDouble());
            if (kind == QLatin1String("plant")) {
                // planted where the limb is now
                const ActorFrame *f = frameOf(a->id);
                QVector3D p;
                if (!f || !jointWorld(*f, chain, &p, nullptr)) { log(QStringLiteral("warn"), QStringLiteral("plant: %1 has no %2").arg(who(*a), chain)); continue; }
                k.point = p;
                k.targetActor = 0;
            } else if (!k.targetActor && !k.point) {
                log(QStringLiteral("warn"), QStringLiteral("constraint: choose what to hold or look at"));
                continue;
            }
            const Actor *t = m_doc.actor(k.targetActor);
            const QString what = t ? who(*t) : QStringLiteral("a point");
            k.name = c.value(QStringLiteral("name")).toString(kind == QLatin1String("plant") ? QStringLiteral("Plant %1").arg(chain)
                                                             : kind == QLatin1String("look") ? QStringLiteral("Look at %1").arg(what)
                                                                                              : QStringLiteral("Hold %1 on %2").arg(chain, what));
            m_doc.constraints << k;
        }
    };
    o[QStringLiteral("constraint_update")] = [this](const QJsonObject &c) {
        Constraint *k = m_doc.constraint(c.value(QStringLiteral("id")).toInt());
        if (!k) return;
        const QJsonObject f = c.value(QStringLiteral("fields")).toObject();
        if (f.contains(QStringLiteral("enabled"))) k->enabled = f.value(QStringLiteral("enabled")).toBool();
        if (f.contains(QStringLiteral("weight"))) k->weight = std::clamp(f.value(QStringLiteral("weight")).toDouble(), 0.0, 1.0);
        if (f.contains(QStringLiteral("offset"))) k->offset = v3(f.value(QStringLiteral("offset")));
        if (f.contains(QStringLiteral("chain"))) k->chain = f.value(QStringLiteral("chain")).toString();
        if (f.contains(QStringLiteral("name"))) k->name = f.value(QStringLiteral("name")).toString();
        if (f.contains(QStringLiteral("target_actor"))) { k->targetActor = qint64(f.value(QStringLiteral("target_actor")).toDouble()); k->point.reset(); }
        if (f.contains(QStringLiteral("target_joint"))) k->targetJoint = f.value(QStringLiteral("target_joint")).toString();
        if (f.value(QStringLiteral("clear_range")).toBool()) k->range.reset();
    };
    o[QStringLiteral("constraint_remove")] = [this](const QJsonObject &c) {
        const int id = c.value(QStringLiteral("id")).toInt();
        m_doc.constraints.removeIf([id](const Constraint &k) { return k.id == id; });
    };
    o[QStringLiteral("constraint_range")] = [this](const QJsonObject &c) {
        Constraint *k = m_doc.constraint(c.value(QStringLiteral("id")).toInt());
        if (!k) return;
        if (c.value(QStringLiteral("clear")).toBool() || !m_doc.seq.range) k->range.reset();
        else k->range = m_doc.seq.range;
    };

    // ---- walk paths: draw points on the floor, the character walks them with its locomotion clips
    o[QStringLiteral("path_new")] = [this](const QJsonObject &c) {
        Actor *a = argActor(c);
        if (!a) return;
        m_selActor = a->id;
        Track *pt = pathTrack(a->id);
        if (!pt) {
            Track tr;
            tr.kind = QStringLiteral("path");
            tr.actor = a->id;
            tr.actorName = a->name;
            tr.name = who(*a) + QStringLiteral(" path");
            tr.path.start = std::floor(m_t);
            m_doc.seq.tracks << tr;
            pt = &m_doc.seq.tracks.last();
        }
        if (c.value(QStringLiteral("clear")).toBool(true) && pt->path.points.isEmpty()) {
            const ActorFrame *f = frameOf(a->id);
            pt->path.points << (f ? f->pos : a->pos);        // the path starts where the character stands
        }
        if (c.contains(QStringLiteral("start"))) pt->path.start = std::max(0.0, std::floor(num(c, "start")));
        const QString gait = c.value(QStringLiteral("gait")).toString(pt->path.gait);
        const bool hasClip = pt->path.clipId != 0;
        m_pen = QStringLiteral("path");
        m_penActor = a->id;
        if (!hasClip) attachPathClip(a->id, gait);
        else refreshPath(a->id);
    };
    o[QStringLiteral("path_point")] = [this](const QJsonObject &c) {
        Track *tr = trackAt(c.value(QStringLiteral("track")).toInt());
        if (!tr || tr->kind != QLatin1String("path")) return;
        QVector3D p = v3(c.value(QStringLiteral("pos")));
        float gy = 0;
        if (groundBelow(p + QVector3D(0, 1.f, 0), 3.f, &gy)) p.setY(gy);
        tr->path.points << p;
        refreshPath(tr->actor);
    };
    o[QStringLiteral("path_pop")] = [this](const QJsonObject &c) {
        Track *tr = trackAt(c.value(QStringLiteral("track")).toInt());
        if (!tr || tr->kind != QLatin1String("path") || tr->path.points.isEmpty()) return;
        tr->path.points.removeLast();
        refreshPath(tr->actor);
    };
    o[QStringLiteral("path_points")] = [this](const QJsonObject &c) {
        Track *tr = trackAt(c.value(QStringLiteral("track")).toInt());
        if (!tr || tr->kind != QLatin1String("path")) return;
        tr->path.points.clear();
        for (const QJsonValue &v : c.value(QStringLiteral("points")).toArray()) tr->path.points << v3(v);
        refreshPath(tr->actor);
    };
    o[QStringLiteral("path_update")] = [this](const QJsonObject &c) {
        Track *tr = trackAt(c.value(QStringLiteral("track")).toInt());
        if (!tr || tr->kind != QLatin1String("path")) return;
        PathSpec &p = tr->path;
        const qint64 actor = tr->actor;
        const QJsonObject f = c.value(QStringLiteral("fields")).toObject();
        if (f.contains(QStringLiteral("start"))) p.start = std::max(0.0, std::floor(f.value(QStringLiteral("start")).toDouble()));
        if (f.contains(QStringLiteral("speed"))) { p.speed = std::max(0.1, f.value(QStringLiteral("speed")).toDouble(1.35)); p.speedPinned = true; p.durPinned = false; }
        if (f.contains(QStringLiteral("dur"))) { p.dur = std::max(1.0, std::floor(f.value(QStringLiteral("dur")).toDouble())); p.durPinned = true; }
        if (f.contains(QStringLiteral("ease"))) p.ease = std::clamp(f.value(QStringLiteral("ease")).toDouble(0.08), 0.0, 0.45);
        if (f.value(QStringLiteral("reverse")).toBool()) std::reverse(p.points.begin(), p.points.end());
        const QString gait = f.value(QStringLiteral("gait")).toString();
        if (!gait.isEmpty()) { p.speedPinned = false; attachPathClip(actor, gait); }
        refreshPath(actor);
    };
    o[QStringLiteral("path_finish")] = [this](const QJsonObject &) { if (m_pen == QLatin1String("path")) m_pen.clear(); };
    o[QStringLiteral("path_remove")] = [this](const QJsonObject &c) {
        const int idx = c.value(QStringLiteral("track")).toInt();
        Track *tr = trackAt(idx);
        if (!tr || tr->kind != QLatin1String("path")) return;
        const qint64 actor = tr->actor;
        const int clipId = tr->path.clipId;
        m_doc.seq.tracks.removeAt(idx - 1);
        if (clipId)
            for (Track &x : m_doc.seq.tracks)
                if (x.kind == QLatin1String("anim") && x.actor == actor) x.clips.removeIf([clipId](const AnimClip &k) { return k.id == clipId; });
        if (m_penActor == actor) m_pen.clear();
        m_selClip = {};
    };

    // ---- the viewport pen: Place (Asset Browser pin) and path points
    o[QStringLiteral("cast_place")] = [this](const QJsonObject &c) { m_penSpec = c; m_pen = QStringLiteral("place"); m_penActor = 0; };
    o[QStringLiteral("pen_cancel")] = [this](const QJsonObject &) { m_pen.clear(); };
    o[QStringLiteral("pen_click")] = [this](const QJsonObject &c) {
        const QVector3D p = v3(c.value(QStringLiteral("pos")));
        if (m_pen == QLatin1String("path")) {
            for (int i = 0; i < m_doc.seq.tracks.size(); ++i)
                if (m_doc.seq.tracks[i].kind == QLatin1String("path") && m_doc.seq.tracks[i].actor == m_penActor) {
                    m_ops.value(QStringLiteral("path_point"))(QJsonObject{{QStringLiteral("track"), i + 1}, {QStringLiteral("pos"), fm::arr(p)}});
                    return;
                }
            m_pen.clear();
        } else if (m_pen == QLatin1String("place")) {
            // stand there, facing the camera
            QJsonObject spec = m_penSpec;
            const QVector3D d = currentView().pos - p;
            spec.insert(QStringLiteral("pos"), fm::arr(p));
            spec.insert(QStringLiteral("rot"), fm::arr(fm::fromEulerDeg(0, std::atan2(d.x(), d.z()) * 180 / fm::kPi, 0)));
            m_pen.clear();
            m_ops.value(QStringLiteral("spawn_cast"))(spec);
        }
    };

    // ---- the clip Play started: speed, frame, pause
    auto liveFrame = [this](const Actor &a) { return a.livePaused ? a.liveFrame : (m_clock.elapsed() - a.liveStartMs) / 1000.0 * 60.0 * a.liveSpeed; };
    o[QStringLiteral("set_speed")] = [this, liveFrame](const QJsonObject &c) {
        Actor *a = argActor(c);
        if (!a) return;
        const double f = liveFrame(*a), speed = num(c, "value", 1);
        a->liveSpeed = std::abs(speed) > 0.01 ? speed : 0.01;
        a->liveStartMs = m_clock.elapsed() - qint64(f / 60.0 / a->liveSpeed * 1000.0);
    };
    o[QStringLiteral("set_frame")] = [this](const QJsonObject &c) {
        Actor *a = argActor(c);
        if (!a) return;
        const double f = std::max(0.0, num(c, "value"));
        a->liveFrame = f;
        a->liveStartMs = m_clock.elapsed() - qint64(f / 60.0 / (std::abs(a->liveSpeed) > 0.01 ? a->liveSpeed : 1.0) * 1000.0);
    };
    o[QStringLiteral("set_paused")] = [this, liveFrame](const QJsonObject &c) {
        Actor *a = argActor(c);
        if (!a) return;
        const bool on = c.value(QStringLiteral("value")).toBool();
        if (on == a->livePaused) return;
        if (on) a->liveFrame = liveFrame(*a);
        else a->liveStartMs = m_clock.elapsed() - qint64(a->liveFrame / 60.0 / (std::abs(a->liveSpeed) > 0.01 ? a->liveSpeed : 1.0) * 1000.0);
        a->livePaused = on;
    };
}
