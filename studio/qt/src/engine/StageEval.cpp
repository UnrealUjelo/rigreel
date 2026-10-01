// The standalone runtime's evaluation: what every actor, camera and the view look like at time t
// (sequence.lua Seq.evaluate, pose.lua Pose.apply, camera evaluation), without the game.
#include "Maths.h"
#include "StageRuntime.h"

#include <algorithm>
#include <cmath>

using namespace film;

namespace {
// Root motion: RE Engine clips animate the skeleton's top bone in model space (a walk's root travels metres); the
// game moves the character by it. Here the travel is measured from each clip's first frame, so a clip starts
// where the character stands and consecutive clips carry it on.
struct Motion {
    QVector3D t;
    QQuaternion r;
};
Motion compose(const Motion &a, const Motion &b) { return {a.t + a.r.rotatedVector(b.t), (a.r * b.r).normalized()}; }
Motion inverse(const Motion &a)
{
    const QQuaternion ir = a.r.conjugated();
    return {ir.rotatedVector(-a.t), ir};
}
Motion rootOf(const ClipBinding &b, double f)
{
    const Xform x = b.rootAt(float(f));
    return {x.t, x.r};
}

// how far a clip has moved its character from its first played frame to clip frame u (not wrapped: loops go on)
Motion clipTravel(const ClipBinding &b, const AnimClip &c, double u)
{
    const double L = c.endframe > 0 ? c.endframe : double(b.frames());
    if (L <= 0) return {};
    const double u0 = c.offset;
    if (!c.loop) return compose(inverse(rootOf(b, std::clamp(u0, 0.0, L))), rootOf(b, std::clamp(u, 0.0, L)));
    const double w0 = std::fmod(u0, L), w = std::fmod(u, L);
    const int k = int(std::floor(u / L) - std::floor(u0 / L));
    if (k <= 0) return compose(inverse(rootOf(b, w0)), rootOf(b, w));
    const Motion cycle = compose(inverse(rootOf(b, 0)), rootOf(b, L));
    Motion m = compose(inverse(rootOf(b, w0)), rootOf(b, L));
    for (int i = 1; i < std::min(k, 4096); ++i) m = compose(m, cycle);
    return compose(m, compose(inverse(rootOf(b, 0)), rootOf(b, w)));
}

// in place: height only, no travel or turn (previews, idles, Lock root)
Motion inPlace(const Motion &m) { return {QVector3D(0, m.t.y(), 0), QQuaternion()}; }

void setRoot(Pose &pose, int bone, const Motion &m)
{
    if (bone < 0 || bone >= pose.size()) return;
    pose[bone].t = m.t;
    pose[bone].r = m.r;
}
} // namespace

// Animation of an actor at t before pose edits: timeline clips, else a previewed motion, else its idle loop.
Pose StageRuntime::basePose(const Actor &a, double t, bool includeWork) const
{
    const PreparedModel *pm = prepared(a.id);
    if (!pm) return {};
    auto *self = const_cast<StageRuntime *>(this);
    Pose pose = bindPose(pm->model->skeleton);
    bool animated = false;
    // root travel of a base-layer track up to `cur` at time tt: every earlier clip adds what it walked until the next began
    auto travel = [&](const Track &tr, const AnimClip *cur, double tt) {
        QVector<const AnimClip *> order;
        for (const AnimClip &c : tr.clips) order << &c;
        std::stable_sort(order.begin(), order.end(), [](const AnimClip *x, const AnimClip *y) { return x->start < y->start; });
        Motion acc;
        for (int i = 0; i < order.size(); ++i) {
            const AnimClip *c = order[i];
            const ClipBinding *cb = self->binding(a.id, c->path, c->mot);
            double end = c == cur ? tt : c->start + c->dur;
            if (c != cur && i + 1 < order.size()) end = std::min(end, order[i + 1]->start);
            if (cb) acc = compose(acc, clipTravel(*cb, *c, c->offset + (end - c->start) * c->speed));
            if (c == cur) break;
        }
        return a.rootLock || m_pathLocked.contains(a.id) ? inPlace(acc) : acc;
    };
    // timeline clips by layer (layer 0 first); a clip blends in from the previous one over its blend frames
    QVector<const Track *> anims;
    // a driven character shows what the keys do, not its timeline (a take writes the timeline as it goes)
    const bool driven = m_drive.on && m_drive.actor == a.id;
    for (const Track &tr : m_doc.seq.tracks) if (!driven && tr.kind == QLatin1String("anim") && tr.actor == a.id) anims << &tr;
    std::stable_sort(anims.begin(), anims.end(), [](const Track *x, const Track *y) { return x->layer < y->layer; });
    for (const Track *tr : anims) {
        const bool base = tr->layer == 0;
        const AnimClip *clip = clipAt(*tr, t);
        if (clip) {
            const ClipBinding *b = self->binding(a.id, clip->path, clip->mot);
            if (!b) { self->ensureAnimations(clip->path); continue; }
            const Xform keepRoot = b->rootBone() >= 0 && b->rootBone() < pose.size() ? pose[b->rootBone()] : Xform();
            const double since = t - clip->start;
            const AnimClip *prev = nullptr;
            for (const AnimClip &c : tr->clips) if (&c != clip && c.start < clip->start) prev = &c;
            const ClipBinding *pb = prev ? self->binding(a.id, prev->path, prev->mot) : nullptr;
            if (clip->blend > 0 && since < clip->blend && pb && prev->start + prev->dur >= clip->start - clip->blend) {
                const float w = float(since / clip->blend);
                pb->sample(float(clipFrame(*prev, t)), pose);
                b->sample(float(clipFrame(*clip, t)), pose, w);
                if (base) {
                    const Motion from = travel(*tr, prev, t), to = travel(*tr, clip, t);
                    setRoot(pose, b->rootBone(), {from.t + (to.t - from.t) * w, fm::slerp(from.r, to.r, w)});
                }
            } else {
                b->sample(float(clipFrame(*clip, t)), pose);
                if (base) setRoot(pose, b->rootBone(), travel(*tr, clip, t));
            }
            // overlay layers never move the character
            if (!base && b->rootBone() >= 0 && b->rootBone() < pose.size()) pose[b->rootBone()] = keepRoot;
            animated = true;
        } else if (base) {
            // gap after a clip: hold that clip's last frame, where it left the character
            const AnimClip *hold = nullptr;
            for (const AnimClip &c : tr->clips) if (c.start + c.dur <= t && (!hold || c.start >= hold->start)) hold = &c;
            if (hold) {
                if (const ClipBinding *b = self->binding(a.id, hold->path, hold->mot)) {
                    b->sample(float(clipFrame(*hold, hold->start + hold->dur)), pose);
                    setRoot(pose, b->rootBone(), travel(*tr, hold, hold->start + hold->dur));
                    animated = true;
                }
            }
        }
    }
    // without timeline clips: the idle loop, then a clip Play started over it (a face clip keeps the idle body)
    if (!animated && a.idleMot >= 0 && !a.idlePath.isEmpty()) {
        if (const ClipBinding *b = self->binding(a.id, a.idlePath, a.idleMot)) {
            const double f = std::fmod(t, std::max(1.0, double(b->frames())));
            b->sample(float(f), pose);
            setRoot(pose, b->rootBone(), inPlace(compose(inverse(rootOf(*b, 0)), rootOf(*b, f))));
        }
    }
    if (!animated && a.liveMot >= 0) {
        if (const ClipBinding *b = self->binding(a.id, a.livePath, a.liveMot)) {
            const double f = std::fmod(a.livePaused ? a.liveFrame : (m_clock.elapsed() - a.liveStartMs) / 1000.0 * 60.0 * a.liveSpeed, std::max(1.0, double(b->frames())));
            b->sample(float(f), pose);
            setRoot(pose, b->rootBone(), inPlace(compose(inverse(rootOf(*b, 0)), rootOf(*b, f))));
        }
    }
    // the clip under the mouse in the Asset Browser, over whatever the character does (a face clip keeps the body)
    if (includeWork && a.id == m_clipPreviewActor && m_clipPreviewMot >= 0) {
        if (const ClipBinding *b = self->binding(a.id, m_clipPreviewPath, m_clipPreviewMot)) {
            const double f = std::fmod((m_clock.elapsed() - m_clipPreviewStartMs) / 1000.0 * 60.0 * m_clipPreviewSpeed, std::max(1.0, double(b->frames())));
            b->sample(float(f), pose);
            setRoot(pose, b->rootBone(), inPlace(compose(inverse(rootOf(*b, 0)), rootOf(*b, f))));
        }
    }
    // pose layers: keyed pose clips, then the working pose (edits not keyed yet)
    const dir::Skeleton &sk = pm->model->skeleton;
    auto apply = [&](const QMap<QString, QQuaternion> &joints, double w, bool additive) {
        if (w <= 0.0005) return;
        for (auto it = joints.cbegin(); it != joints.cend(); ++it) {
            const int b = sk.find(it.key());
            if (b < 0 || b >= pose.size()) continue;
            const QQuaternion cur = pose[b].r;
            if (additive) pose[b].r = (cur * fm::slerp(QQuaternion(), it.value(), float(w))).normalized();
            else pose[b].r = w >= 0.9995 ? it.value() : fm::slerp(cur, it.value(), float(w));
        }
    };
    for (const Track &tr : m_doc.seq.tracks) {
        if (tr.kind != QLatin1String("pose") || tr.actor != a.id) continue;
        const PoseClip *pc = poseClipAt(tr, t);
        if (!pc) continue;
        const double lt = t - pc->start;
        apply(evalPoseKeys(pc->keys, lt, pc->loop), pc->weight * fadeWeight(lt, pc->dur, pc->fadeIn, pc->fadeOut), pc->mode == QLatin1String("additive"));
    }
    if (includeWork) apply(a.work, a.workWeight, false);
    return pose;
}

ViewFrame StageRuntime::currentView() const
{
    const Camera *c = (m_workOn || m_activeCam <= 0) ? &m_doc.work : const_cast<Document &>(m_doc).camera(m_activeCam);
    if (!c) c = &m_doc.work;
    ViewFrame v;
    v.pos = c->pos;
    v.rot = c->rot;
    if (c->roll != 0) v.rot = (v.rot * QQuaternion::fromAxisAndAngle(0, 0, 1, float(c->roll))).normalized();
    v.fov = c->fov;
    v.dof = c->dof && c != &m_doc.work && c->dofF.has_value();
    v.dofF = c->dofF.value_or(2.8);
    v.dofFocus = c->dofFocus.value_or(3.0);
    v.label = c->name;
    return v;
}

void StageRuntime::evaluate(double t)
{
    // cameras: keyed moves follow the film while it plays or after a seek (the pilot keeps the stick otherwise)
    if (m_playing || m_camFollow || m_rendering) {
        for (const Track &tr : std::as_const(m_doc.seq.tracks)) {
            if (tr.kind != QLatin1String("cammove")) continue;
            Camera *c = m_doc.camera(tr.cam);
            XKey k;
            if (!c || tr.cam <= 0 || !evalKeys(tr.keys, t, &k)) continue;
            c->pos = k.pos;
            c->rot = k.rot;
            if (k.fov > 0) c->fov = k.fov;
            c->roll = k.roll;
            if (k.dofF) c->dofF = k.dofF;
            if (k.dofFocus) c->dofFocus = k.dofFocus;
            if (!tr.lookatName.isEmpty())
                if (const Actor *a = m_doc.actorByName(tr.lookatName)) c->rot = fm::lookRotation(c->pos, a->pos + QVector3D(0, 1.4f, 0));
        }
        // the Scene Camera and playback follow the cuts (else each shot's camera); the Work Camera never moves
        if (!m_workOn && (m_sceneView || m_playing || m_rendering)) {
            const int fc = m_shotCam > 0 && m_doc.camera(m_shotCam) ? m_shotCam : filmCamera(t);
            if (fc > 0) m_activeCam = fc;
        }
    }

    // animated values (lights, camera focus, look-at weights, visibility ...)
    applyChannels(t);

    // actors (a walk path places its character before the pose, the rig pass adjusts the poses after)
    QHash<qint64, QPair<QVector3D, QQuaternion>> onPath;
    placePaths(t, &onPath);
    m_frames.clear();
    m_frames.reserve(m_doc.actors.size());
    for (const Actor &a : std::as_const(m_doc.actors)) {
        ActorFrame f;
        f.id = a.id;
        f.pos = a.pos;
        f.rot = a.rot;
        f.scale = a.scale;
        f.visible = !a.hidden;
        f.selected = a.id == m_selActor || m_multi.contains(a.id);
        for (const Track &tr : std::as_const(m_doc.seq.tracks)) {
            if (tr.kind != QLatin1String("xform") || tr.actor != a.id || (m_drive.on && m_drive.actor == a.id)) continue;
            XKey k;
            if (evalKeys(tr.keys, t, &k)) {
                // off a key the placement follows the keys; the base values show at a key being edited
                bool atKey = false;
                for (const XKey &x : tr.keys) if (std::abs(x.t - t) < 0.5) atKey = true;
                f.pos = k.pos;
                f.rot = k.rot;
                Q_UNUSED(atKey);
            }
        }
        if (const auto p = onPath.constFind(a.id); p != onPath.cend()) { f.pos = p->first; f.rot = p->second; }
        f.pose = basePose(a, t, true);
        m_frames << f;
    }
    solveRigs(t);
    simulateChains(t);
    m_view = currentView();
    // the hover preview: like a museum case, centred in front of the camera at a distance that fits it, turning
    if (m_previewShown) {
        const float radius = std::max(0.05f, m_previewSize.length() * 0.5f);
        const double vHalf = std::atan(std::tan(std::max(10.0, m_view.fov) * 0.5 * fm::kPi / 180.0) / 1.78);
        const float dist = radius / float(std::sin(vHalf)) * 1.35f;
        const QVector3D fwd = fm::forward(m_view.rot);
        QVector3D back(-fwd.x(), 0, -fwd.z());
        if (back.length() < 0.01f) back = QVector3D(0, 0, 1);
        const float yaw = float(std::atan2(back.x(), back.z()) * 180 / fm::kPi);
        const float spin = float((m_clock.elapsed() - m_previewStartMs) / 1000.0 * 30.0);
        ActorFrame f;
        f.id = kPreviewId;
        f.rot = QQuaternion::fromAxisAndAngle(0, 1, 0, yaw + spin);
        f.pos = m_view.pos + fwd * dist - f.rot.rotatedVector(m_previewCenter);
        m_frames << f;
    }
}
