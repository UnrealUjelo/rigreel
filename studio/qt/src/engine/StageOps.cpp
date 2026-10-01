// The standalone runtime's commands: bridge.lua's ops, same names and arguments, on the editor's own film.
#include "GameLibrary.h"
#include "Maths.h"
#include "StageRuntime.h"
#include "StageScene.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

using namespace film;

namespace {
double num(const QJsonObject &c, const char *k, double def = 0) { const QJsonValue v = c.value(QLatin1String(k)); return v.isDouble() ? v.toDouble() : def; }
qint64 addr(const QJsonObject &c) { return qint64(c.value(QStringLiteral("addr")).toDouble()); }
QVector3D v3(const QJsonValue &v, const QVector3D &def = {}) { return fm::vec3(v, def); }
template <typename T> void sortByStart(QVector<T> &v) { std::stable_sort(v.begin(), v.end(), [](const T &a, const T &b) { return a.start < b.start; }); }
void sortKeys(QVector<XKey> &k) { std::stable_sort(k.begin(), k.end(), [](const XKey &a, const XKey &b) { return a.t < b.t; }); }
void sortPoseKeys(QVector<PoseKey> &k) { std::stable_sort(k.begin(), k.end(), [](const PoseKey &a, const PoseKey &b) { return a.t < b.t; }); }
QString mirrorName(const QString &n)
{
    if (n.startsWith(QLatin1String("L_"))) return QStringLiteral("R_") + n.mid(2);
    if (n.startsWith(QLatin1String("R_"))) return QStringLiteral("L_") + n.mid(2);
    if (n.startsWith(QLatin1String("l_"))) return QStringLiteral("r_") + n.mid(2);      // RE2 / RE3 spelling
    if (n.startsWith(QLatin1String("r_"))) return QStringLiteral("l_") + n.mid(2);
    return {};
}
} // namespace

// ------------------------------------------------------------------ helpers
Actor *StageRuntime::selectedActor() { return m_doc.actor(m_selActor); }

Actor *StageRuntime::argActor(const QJsonObject &c)
{
    const qint64 id = addr(c);
    return id ? m_doc.actor(id) : selectedActor();
}

Track *StageRuntime::trackAt(int idx) { return idx >= 1 && idx <= m_doc.seq.tracks.size() ? &m_doc.seq.tracks[idx - 1] : nullptr; }

Track &StageRuntime::track(const QString &kind, qint64 actor, int layer, bool *created)
{
    for (Track &tr : m_doc.seq.tracks)
        if (tr.kind == kind && tr.actor == actor && ((kind != QLatin1String("anim") && kind != QLatin1String("pose")) || tr.layer == layer)) { if (created) *created = false; return tr; }
    const Actor *a = m_doc.actor(actor);
    Track tr;
    tr.kind = kind;
    tr.actor = actor;
    tr.actorName = a ? a->name : QString();
    tr.layer = layer;
    const QString who = a ? (a->displayName.isEmpty() ? a->name : a->displayName) : QStringLiteral("?");
    tr.name = kind == QLatin1String("pose") ? who + QStringLiteral(" pose") : kind == QLatin1String("xform") ? who + QStringLiteral(" move") : who;
    m_doc.seq.tracks << tr;
    if (created) *created = true;
    return m_doc.seq.tracks.last();
}

Track &StageRuntime::cammoveTrack(int cam)
{
    for (Track &tr : m_doc.seq.tracks) if (tr.kind == QLatin1String("cammove") && tr.cam == cam) return tr;
    Track tr;
    tr.kind = QStringLiteral("cammove");
    tr.cam = cam;
    const Camera *c = m_doc.camera(cam);
    tr.name = (c ? c->name : QStringLiteral("Camera %1").arg(cam)) + QStringLiteral(" moves");
    m_doc.seq.tracks << tr;
    return m_doc.seq.tracks.last();
}

Track &StageRuntime::cameraTrack()
{
    for (Track &tr : m_doc.seq.tracks) if (tr.kind == QLatin1String("camera")) return tr;
    Track tr;
    tr.kind = QStringLiteral("camera");
    tr.name = QStringLiteral("Camera");
    m_doc.seq.tracks << tr;
    return m_doc.seq.tracks.last();
}

void StageRuntime::grow(double end)
{
    if (end > m_doc.seq.length) m_doc.seq.length = std::ceil(end);
}

QVector3D StageRuntime::spawnPoint(double distance, QQuaternion *facing) const
{
    const ViewFrame v = currentView();
    QVector3D f = fm::forward(v.rot);
    f.setY(0);
    if (f.length() < 0.01f) f = QVector3D(0, 0, -1);
    f.normalize();
    QVector3D p = v.pos + f * float(distance);
    // on a map: stand on whatever is below the camera's height there (a floor, a street, not the roof above)
    float ground = 0;
    if (groundBelow(QVector3D(p.x(), v.pos.y() + 0.3f, p.z()), 60.f, &ground) || groundBelow(QVector3D(p.x(), v.pos.y() + 40.f, p.z()), 120.f, &ground))
        p.setY(ground);
    else
        p.setY(m_doc.stageId.isEmpty() ? 0.f : v.pos.y() - 1.6f);
    if (facing) *facing = fm::fromEulerDeg(0, std::atan2(-f.x(), -f.z()) * 180 / fm::kPi, 0);
    return p;
}

void StageRuntime::keyTransform(Actor &a, double t, const QVector3D *pos, const QQuaternion *rot)
{
    QVector3D p = a.pos;
    QQuaternion r = a.rot;
    for (const ActorFrame &f : std::as_const(m_frames)) if (f.id == a.id) { p = f.pos; r = f.rot; }
    if (pos) p = *pos;
    if (rot) r = *rot;
    Track &tr = track(QStringLiteral("xform"), a.id, 0);
    for (XKey &k : tr.keys)
        if (std::abs(k.t - t) < 0.5) { k.pos = p; k.rot = r; return; }
    XKey k;
    k.t = t;
    k.pos = p;
    k.rot = r;
    tr.keys << k;
    sortKeys(tr.keys);
    grow(t + 1);
}

void StageRuntime::keyCamera(int cam, double t)
{
    if (cam <= 0) { log(QStringLiteral("info"), QStringLiteral("the work camera is never keyed - press C to make a scene camera from the view")); return; }
    Camera *c = m_doc.camera(cam);
    if (!c) return;
    Track &tr = cammoveTrack(cam);
    XKey *k = nullptr;
    for (XKey &x : tr.keys) if (std::abs(x.t - t) < 0.5) k = &x;
    if (!k) { tr.keys << XKey{}; k = &tr.keys.last(); k->t = t; }
    k->pos = c->pos;
    k->rot = c->rot;
    k->fov = c->fov;
    k->roll = c->roll;
    k->dofF = c->dofF;
    k->dofFocus = c->dofFocus;
    sortKeys(tr.keys);
    grow(t + 1);
    log(QStringLiteral("info"), QStringLiteral("camera %1 keyed @ %2").arg(cam).arg(int(t)));
}

// pose key at absolute time t (sequence.lua Seq.keyframe)
void StageRuntime::keyframePose(Actor &a, double t, const QMap<QString, QQuaternion> &joints)
{
    Track &tr = track(QStringLiteral("pose"), a.id, 0);
    PoseClip *clip = nullptr;
    for (PoseClip &c : tr.poses) if (t >= c.start && t <= c.start + c.dur) clip = &c;
    if (!clip) {
        PoseClip *prev = nullptr;
        for (PoseClip &c : tr.poses) if (c.start <= t) prev = &c;
        if (prev && t - (prev->start + prev->dur) < 120) { clip = prev; clip->dur = t - clip->start + 1; }
        else {
            PoseClip c;
            c.id = newId();
            c.start = t;
            c.dur = 1;
            tr.poses << c;
            sortByStart(tr.poses);
            for (PoseClip &x : tr.poses) if (x.id == c.id) clip = &x;
        }
    }
    const double lt = t - clip->start;
    PoseKey *key = nullptr;
    for (PoseKey &k : clip->keys) if (std::abs(k.t - lt) < 0.5) key = &k;
    if (key) for (auto it = joints.cbegin(); it != joints.cend(); ++it) key->joints.insert(it.key(), it.value());
    else { PoseKey k; k.t = lt; k.joints = joints; clip->keys << k; sortPoseKeys(clip->keys); }
    if (lt + 1 > clip->dur) clip->dur = lt + 1;
    grow(clip->start + clip->dur);
}

// extras.lua X.commit_pose_edit: a key, or a hold over the time selection with falloff (Motion Editor)
void StageRuntime::commitPoseEdit(Actor &a)
{
    if (a.work.isEmpty()) return;
    const double t = std::floor(m_t);
    const Sequence &s = m_doc.seq;
    if (!(m_motionEdit && s.range)) { keyframePose(a, t, a.work); return; }
    if (m_relativeEdits) { commitRelativeEdit(a); return; }
    const double a0 = s.range->first, b0 = s.range->second;
    const double fin = s.falloff ? s.falloff->first : 0, fout = s.falloff ? s.falloff->second : 0;
    // what the joints do underneath the edit, so it fades back into the motion
    const Pose under = basePose(a, t, false);
    QMap<QString, QQuaternion> ends;
    if (const PreparedModel *pm = prepared(a.id))
        for (auto it = a.work.cbegin(); it != a.work.cend(); ++it) {
            const int b = pm->model->skeleton.find(it.key());
            if (b >= 0 && b < under.size()) ends.insert(it.key(), under[b].r);
        }
    const Track *tr = nullptr;
    for (const Track &x : std::as_const(m_doc.seq.tracks)) if (x.kind == QLatin1String("pose") && x.actor == a.id) tr = &x;
    auto existing = [&](double at) {
        if (!tr) return false;
        for (const PoseClip &c : tr->poses) if (at >= c.start && at <= c.start + c.dur) return true;
        return false;
    };
    if (fin > 0 || fout > 0) {
        if (t < a0 - fin || t > b0 + fout) { keyframePose(a, t, a.work); return; }
        const double lo = std::max(0.0, a0 - std::max(1.0, fin)), hi = b0 + std::max(1.0, fout);
        if (!ends.isEmpty()) {
            if (!existing(lo)) keyframePose(a, lo, ends);
            if (!existing(hi)) keyframePose(a, hi, ends);
        }
        keyframePose(a, a0, a.work);
        keyframePose(a, b0, a.work);
        return;
    }
    if (t <= a0 || t >= b0) { keyframePose(a, t, a.work); return; }
    if (!ends.isEmpty()) {
        if (!existing(a0)) keyframePose(a, a0, ends);
        if (!existing(b0)) keyframePose(a, b0, ends);
    }
    keyframePose(a, t, a.work);
}

// SFM's Motion Editor: the difference between the edit and the motion at this frame is added to the motion all
// through the time selection (fading in and out over the falloff), so the bones keep moving underneath: an
// additive pose clip on the character's offset layer (pose layer 1)
void StageRuntime::commitRelativeEdit(Actor &a)
{
    const PreparedModel *pm = prepared(a.id);
    const Sequence &s = m_doc.seq;
    if (!pm || !s.range) return;
    const double t = std::floor(m_t);
    const Pose under = basePose(a, t, false);                 // everything below the edit, earlier offsets included
    QMap<QString, QQuaternion> delta;
    for (auto it = a.work.cbegin(); it != a.work.cend(); ++it) {
        const int b = pm->model->skeleton.find(it.key());
        if (b >= 0 && b < under.size()) delta.insert(it.key(), (under[b].r.conjugated() * it.value()).normalized());
    }
    if (delta.isEmpty()) return;
    const double a0 = s.range->first, b0 = s.range->second;
    const double fin = s.falloff ? s.falloff->first : 0, fout = s.falloff ? s.falloff->second : 0;
    const double lo = std::max(0.0, a0 - fin), hi = b0 + fout;
    bool made = false;
    Track &tr = track(QStringLiteral("pose"), a.id, 1, &made);
    if (made) tr.name = (a.displayName.isEmpty() ? a.name : a.displayName) + QStringLiteral(" offsets");
    PoseClip *clip = nullptr;
    for (PoseClip &c : tr.poses)
        if (c.mode == QLatin1String("additive") && std::abs(c.start - lo) < 0.5 && std::abs(c.start + c.dur - (hi + 1)) < 0.5) clip = &c;
    if (!clip) {
        PoseClip c;
        c.id = newId();
        c.start = lo;
        c.dur = hi - lo + 1;
        c.mode = QStringLiteral("additive");
        c.fadeIn = a0 - lo;
        c.fadeOut = hi - b0;
        c.keys << PoseKey{0, {}, {}};
        tr.poses << c;
        sortByStart(tr.poses);
        for (PoseClip &x : tr.poses) if (x.id == c.id) clip = &x;
    }
    for (PoseKey &k : clip->keys)
        for (auto it = delta.cbegin(); it != delta.cend(); ++it)
            k.joints.insert(it.key(), (k.joints.value(it.key(), QQuaternion()) * it.value()).normalized());
    grow(clip->start + clip->dur);
    for (auto it = delta.cbegin(); it != delta.cend(); ++it) a.work.remove(it.key());   // the offset shows it now
}

// unkeyed pose edits are dropped when the playhead moves (Blender behaviour without auto-key)
void StageRuntime::dropUnkeyedEdits()
{
    for (const Track &tr : std::as_const(m_doc.seq.tracks))
        if (tr.kind == QLatin1String("pose"))
            if (Actor *a = m_doc.actor(tr.actor)) { a->work.clear(); }
}

double StageRuntime::filmLength() const
{
    double n = 0;
    for (const Shot &sh : m_doc.seq.shots) n += std::max(1.0, sh.b - sh.a);
    return n;
}

bool StageRuntime::filmAt(double filmT, int *shot, double *sceneT) const
{
    double at = 0;
    const auto &shots = m_doc.seq.shots;
    for (int i = 0; i < shots.size(); ++i) {
        const double d = std::max(1.0, shots[i].b - shots[i].a);
        if (filmT < at + d) { *shot = i; *sceneT = shots[i].a + (filmT - at); return true; }
        at += d;
    }
    return false;
}

int StageRuntime::filmCamera(double t) const
{
    for (const Track &tr : m_doc.seq.tracks)
        if (tr.kind == QLatin1String("camera"))
            if (const Cut *c = cutAt(tr, t); c && c->cam >= 1 && c->cam <= m_doc.cameras.size()) return c->cam;
    for (const Shot &sh : m_doc.seq.shots)
        if (t >= sh.a && t < sh.b && sh.cam >= 1 && sh.cam <= m_doc.cameras.size()) return sh.cam;
    return 0;
}

void StageRuntime::setActiveCamera(int i)
{
    if (i >= 1 && i <= m_doc.cameras.size()) { m_activeCam = i; m_workOn = false; }
    else { m_activeCam = 0; m_workOn = true; }
}

// dope sheet items {track, id?, t}: xform / cammove keys on the track, pose keys inside a clip
QVector<const XKey *> StageRuntime::resolveKeys(const QJsonArray &items, QVector<XKey *> *mut)
{
    QVector<const XKey *> out;
    for (const QJsonValue &v : items) {
        const QJsonObject it = v.toObject();
        Track *tr = trackAt(it.value(QStringLiteral("track")).toInt());
        if (!tr || tr->keys.isEmpty()) continue;
        const double t = it.value(QStringLiteral("t")).toDouble();
        for (XKey &k : tr->keys)
            if (std::abs(k.t - t) < 0.5) { out << &k; if (mut) *mut << &k; }
    }
    return out;
}

void StageRuntime::spawnModel(const QString &modelId, const QString &name, const QString &kind, const QJsonObject &extra)
{
    Actor a;
    a.id = m_doc.nextActor++;
    a.kind = kind;
    a.model = modelId;
    QString base = name.isEmpty() ? modelId.section(QLatin1Char('/'), -1) : name;
    QString unique = base;
    for (int n = 2; m_doc.actorByName(unique); ++n) unique = QStringLiteral("%1 %2").arg(base).arg(n);
    a.name = unique;
    a.displayName = unique;
    a.castId = extra.value(QStringLiteral("id")).toString();
    a.castTree = extra.value(QStringLiteral("tree")).toString();
    a.castCode = extra.value(QStringLiteral("code")).toString();
    a.preset = extra.value(QStringLiteral("preset")).toString();
    QQuaternion face;
    a.pos = extra.contains(QStringLiteral("pos")) ? v3(extra.value(QStringLiteral("pos"))) : spawnPoint(kind == QLatin1String("object") ? 2.0 : 2.5, &face);
    a.rot = extra.contains(QStringLiteral("rot")) ? fm::quat(extra.value(QStringLiteral("rot"))) : face;
    QString general = extra.value(QStringLiteral("general")).toString();
    const qint64 id = a.id;
    m_doc.actors << a;
    if (general.isEmpty() && kind == QLatin1String("character")) general = generalFor(m_doc.actors.last(), false);   // Ada: her Separate Ways idle
    m_selActor = id;
    m_multi.clear();
    ensureModel(id);
    // characters stand in their idle loop until the timeline gives them something to do
    if (!general.isEmpty() && !extra.value(QStringLiteral("no_idle")).toBool()) {
        ensureAnimations(general, [this, id, general]() {
            Actor *x = m_doc.actor(id);
            const dir::AnimationSetPtr set = animations(general);
            if (!x || !set) return;
            const dir::AnimationClip *pick = nullptr;
            for (const QString &want : {QStringLiteral("stand_loop"), QStringLiteral("idle_loop"), QStringLiteral("_idle"), QStringLiteral("stand"), QStringLiteral("wait")})
                for (const dir::AnimationClip &c : set->clips)
                    if (!pick && c.name.contains(want, Qt::CaseInsensitive) && c.frames > 30) pick = &c;
            if (pick) { x->idlePath = general; x->idleMot = pick->id; }
            if (!m_playing) evaluate(m_t);
            publish();
        });
    }
    markData();
}

void StageRuntime::removeActor(qint64 id)
{
    for (int i = 0; i < m_doc.actors.size(); ++i)
        if (m_doc.actors[i].id == id) { m_doc.actors.removeAt(i); break; }
    // its tracks go with it (undo brings both back)
    const auto gone = std::remove_if(m_doc.seq.tracks.begin(), m_doc.seq.tracks.end(), [id](const Track &tr) {
        return tr.actor == id && (tr.kind == QLatin1String("anim") || tr.kind == QLatin1String("pose") || tr.kind == QLatin1String("xform") || tr.kind == QLatin1String("path"));
    });
    if (gone != m_doc.seq.tracks.end()) { m_doc.seq.tracks.erase(gone, m_doc.seq.tracks.end()); m_selClip = {}; }
    const QString prefix = QString::number(id) + QLatin1Char('|');
    for (auto it = m_bindings.begin(); it != m_bindings.end();) it = it.key().startsWith(prefix) ? m_bindings.erase(it) : std::next(it);
    if (m_clipPreviewActor == id) { m_clipPreviewActor = 0; m_clipPreviewMot = -1; }
    // what held on to it lets go: constraints, props riding on it (they stay where they are), heads looking at it
    m_doc.constraints.removeIf([id](const Constraint &c) { return c.actor == id || c.targetActor == id; });
    for (Actor &x : m_doc.actors) {
        if (x.attached && x.attached->actor == id) {
            if (const ActorFrame *f = frameOf(x.id)) { x.pos = f->pos; x.rot = f->rot; }
            x.attached.reset();
        }
        if (x.lookat.target == id) { x.lookat.target = 0; if (x.lookat.kind == QLatin1String("actor")) x.lookat.enabled = false; }
    }
    if (m_penActor == id) m_pen.clear();
    if (m_drive.on && m_drive.actor == id) driveStop();
    m_lookCur.remove(id);
    m_models.remove(id);
    if (m_scene) m_scene->removeActor(id);
    if (m_selActor == id) m_selActor = 0;
    m_multi.remove(id);
    markData();
}

void StageRuntime::navigate(const QVector3D &pos, const QQuaternion &rot)
{
    Camera *c = m_workOn || m_activeCam <= 0 ? &m_doc.work : m_doc.camera(m_activeCam);
    if (!c) return;
    c->pos = pos;
    c->rot = rot;
    if (c != &m_doc.work) m_camFollow = false;           // the pilot has the stick
    evaluate(m_t);
    if (m_scene) m_scene->sync(*this);
    static qint64 last = 0;
    if (m_clock.elapsed() - last > 80) { last = m_clock.elapsed(); publish(); }
}

void StageRuntime::pick(qint64 actorId, bool add)
{
    QJsonObject c{{QStringLiteral("addr"), double(actorId)}, {QStringLiteral("add"), add}};
    if (actorId == kPreviewId) return;                    // the hover preview is not part of the film
    if (!actorId) send(QStringLiteral("select_clear"), {});
    else send(QStringLiteral("select_actor"), c);
}

// ------------------------------------------------------------------ the ops
void StageRuntime::registerOps()
{
    auto &o = m_ops;
    m_readonly = {QStringLiteral("ping"), QStringLiteral("select_actor"), QStringLiteral("select_clear"), QStringLiteral("select_clip"),
                  QStringLiteral("cam_select"), QStringLiteral("cam_live"), QStringLiteral("cam_work"), QStringLiteral("view_set"), QStringLiteral("cam_scene"),
                  QStringLiteral("cam_fly"), QStringLiteral("cam_stop"), QStringLiteral("light_select"), QStringLiteral("seq_play"),
                  QStringLiteral("seq_pause"), QStringLiteral("seq_toggle"), QStringLiteral("seq_stop"), QStringLiteral("seq_seek"),
                  QStringLiteral("seq_speed"), QStringLiteral("film_play"), QStringLiteral("film_stop"), QStringLiteral("catalog_search"), QStringLiteral("mesh_search"), QStringLiteral("load_motlist"),
                  QStringLiteral("play"), QStringLiteral("autokey"), QStringLiteral("gizmo"), QStringLiteral("motion_edit"), QStringLiteral("undo"),
                  QStringLiteral("redo"), QStringLiteral("project_save"), QStringLiteral("project_load"), QStringLiteral("copy_keys"),
                  QStringLiteral("select_joint"), QStringLiteral("eval"), QStringLiteral("refresh"), QStringLiteral("hud"), QStringLiteral("show_cameras"),
                  QStringLiteral("overlay_skeleton"), QStringLiteral("overlay_onion"), QStringLiteral("want_objects"), QStringLiteral("cast_preview"),
                  QStringLiteral("cast_preview_clear"), QStringLiteral("mesh_preview"), QStringLiteral("mesh_preview_clear"), QStringLiteral("save_pose"),
                  QStringLiteral("render_clock_begin"), QStringLiteral("render_clock_step"), QStringLiteral("render_clock_end"), QStringLiteral("project_new"),
                  QStringLiteral("play_preview"), QStringLiteral("play_preview_end"), QStringLiteral("play_stop")};
    for (const char *noop : {"ping", "refresh", "hud", "want_objects",
                             "seq_release", "release_all", "release_actor", "game_freeze", "hide_weapons", "set_puppet", "set_paused"})
        o.insert(QLatin1String(noop), [](const QJsonObject &) {});

    // ---- transport
    o[QStringLiteral("seq_play")] = [this](const QJsonObject &) {
        dropUnkeyedEdits();
        const Sequence &s = m_doc.seq;
        const double a = s.range ? s.range->first : 0, b = s.range ? std::min(s.range->second, s.length) : s.length;
        if (m_t >= b || m_t < a) m_t = a;
        m_camFollow = true;
        m_playing = true;
        m_lastTick = m_clock.elapsed();
    };
    o[QStringLiteral("seq_pause")] = [this](const QJsonObject &) { m_filmPlay = false; m_shotCam = -1; m_playing = false; };
    o[QStringLiteral("seq_toggle")] = [this](const QJsonObject &) { if (m_playing) m_playing = false; else m_ops.value(QStringLiteral("seq_play"))({}); };
    o[QStringLiteral("seq_stop")] = [this](const QJsonObject &) { m_filmPlay = false; m_shotCam = -1; dropUnkeyedEdits(); m_playing = false; m_t = 0; };
    o[QStringLiteral("seq_seek")] = [this](const QJsonObject &c) { m_filmPlay = false; m_shotCam = -1;
        m_playing = false;
        dropUnkeyedEdits();
        m_camFollow = true;
        m_t = std::clamp(num(c, "t"), 0.0, m_doc.seq.length);
    };
    o[QStringLiteral("seq_speed")] = [this](const QJsonObject &c) { m_speed = std::clamp(num(c, "value", 1), 0.1, 4.0); };
    o[QStringLiteral("seq_set")] = [this](const QJsonObject &c) {
        if (c.contains(QStringLiteral("length"))) m_doc.seq.length = std::max(60.0, std::floor(num(c, "length")));
        if (c.contains(QStringLiteral("loop"))) m_doc.seq.loop = c.value(QStringLiteral("loop")).toBool();
        if (c.contains(QStringLiteral("name"))) m_doc.seq.name = c.value(QStringLiteral("name")).toString();
        if (c.contains(QStringLiteral("fps"))) m_doc.seq.fps = std::clamp(num(c, "fps", 60), 12.0, 240.0);
    };
    o[QStringLiteral("seq_range")] = [this](const QJsonObject &c) {
        Sequence &s = m_doc.seq;
        if (c.value(QStringLiteral("clear")).toBool()) { s.range.reset(); s.falloff.reset(); return; }
        if (c.contains(QStringLiteral("a")) || c.contains(QStringLiteral("b"))) {
            auto r = s.range.value_or(std::make_pair(0.0, s.length));
            if (c.contains(QStringLiteral("a"))) r.first = std::max(0.0, std::floor(num(c, "a")));
            if (c.contains(QStringLiteral("b"))) r.second = std::max(0.0, std::floor(num(c, "b")));
            if (r.second <= r.first) r.second = r.first + 1;
            s.range = r;
        }
        if (c.contains(QStringLiteral("fin")) || c.contains(QStringLiteral("fout"))) {
            if (!s.range) s.range = std::make_pair(std::floor(m_t), std::floor(m_t) + 1);
            auto f = s.falloff.value_or(std::make_pair(0.0, 0.0));
            if (c.contains(QStringLiteral("fin"))) f.first = std::max(0.0, std::round(num(c, "fin")));
            if (c.contains(QStringLiteral("fout"))) f.second = std::max(0.0, std::round(num(c, "fout")));
            if (f.first > 0 || f.second > 0) s.falloff = f;
            else s.falloff.reset();
        }
    };
    o[QStringLiteral("seq_audio")] = [this](const QJsonObject &c) {
        QJsonObject &a = m_doc.seq.audio;
        if (c.value(QStringLiteral("clear")).toBool()) { a = {}; return; }
        if (a.isEmpty()) a = {{QStringLiteral("offset"), 0}, {QStringLiteral("volume"), 1}};
        if (c.contains(QStringLiteral("path"))) {
            a.insert(QStringLiteral("path"), c.value(QStringLiteral("path")));
            a.insert(QStringLiteral("name"), c.value(QStringLiteral("name")).toString(c.value(QStringLiteral("path")).toString().section(QLatin1Char('/'), -1)));
        }
        for (const char *k : {"offset", "volume", "duration"})
            if (c.contains(QLatin1String(k))) a.insert(QLatin1String(k), c.value(QLatin1String(k)));
    };
    o[QStringLiteral("render_clock_begin")] = [this](const QJsonObject &) { renderBegin(); };
    o[QStringLiteral("render_clock_step")] = [this](const QJsonObject &c) { renderStep(num(c, "t", m_t)); };
    o[QStringLiteral("render_clock_end")] = [this](const QJsonObject &) { renderEnd(); };

    // ---- tracks and clips
    o[QStringLiteral("add_track")] = [this](const QJsonObject &c) {
        const QString kind = c.value(QStringLiteral("kind")).toString();
        if (kind == QLatin1String("camera")) { cameraTrack(); return; }
        Actor *a = argActor(c);
        if (a && (kind == QLatin1String("anim") || kind == QLatin1String("pose") || kind == QLatin1String("xform"))) track(kind, a->id, c.value(QStringLiteral("layer")).toInt());
    };
    o[QStringLiteral("remove_track")] = [this](const QJsonObject &c) {
        const int idx = c.value(QStringLiteral("idx")).toInt();
        if (idx >= 1 && idx <= m_doc.seq.tracks.size()) m_doc.seq.tracks.removeAt(idx - 1);
        m_bindings.clear();
    };
    o[QStringLiteral("add_clip")] = [this](const QJsonObject &c) {
        Actor *a = argActor(c);
        if (!a) return;
        const QString path = c.contains(QStringLiteral("path")) ? c.value(QStringLiteral("path")).toString() : pathForBank(c.value(QStringLiteral("bank")).toInt());
        if (path.isEmpty()) return;
        Track &tr = track(QStringLiteral("anim"), a->id, c.value(QStringLiteral("layer")).toInt());
        AnimClip clip;
        clip.id = newId();
        clip.start = std::floor(num(c, "start", m_t));
        clip.path = path;
        clip.mot = c.value(QStringLiteral("mot")).toInt(-1);
        clip.name = c.value(QStringLiteral("name")).toString();
        clip.endframe = num(c, "endframe");
        if (const dir::AnimationSetPtr set = animations(path))
            for (const dir::AnimationClip &m : set->clips)
                if (m.id == clip.mot) { if (clip.name.isEmpty()) clip.name = m.name; if (clip.endframe <= 0) clip.endframe = m.frames; }
        clip.blend = num(c, "blend", 10);
        clip.speed = num(c, "speed", 1);
        clip.dur = std::max(1.0, c.contains(QStringLiteral("dur")) ? num(c, "dur") : (clip.endframe > 0 ? clip.endframe / std::abs(clip.speed) : 120.0));
        tr.clips << clip;
        sortByStart(tr.clips);
        grow(clip.start + clip.dur);
        a->liveMot = -1;                                   // the timeline takes over from the preview
        ensureAnimations(path);
    };
    o[QStringLiteral("update_clip")] = [this](const QJsonObject &c) {
        Track *tr = trackAt(c.value(QStringLiteral("track")).toInt());
        if (!tr) return;
        const int id = c.value(QStringLiteral("id")).toInt();
        const QJsonObject f = c.value(QStringLiteral("fields")).toObject();
        for (AnimClip &cl : tr->clips) {
            if (cl.id != id) continue;
            if (f.contains(QStringLiteral("start"))) cl.start = std::max(0.0, std::round(f.value(QStringLiteral("start")).toDouble()));
            if (f.contains(QStringLiteral("dur"))) cl.dur = std::max(1.0, f.value(QStringLiteral("dur")).toDouble());
            if (f.contains(QStringLiteral("offset"))) cl.offset = f.value(QStringLiteral("offset")).toDouble();
            if (f.contains(QStringLiteral("speed"))) cl.speed = f.value(QStringLiteral("speed")).toDouble(1);
            if (f.contains(QStringLiteral("blend"))) cl.blend = f.value(QStringLiteral("blend")).toDouble();
            if (f.contains(QStringLiteral("loop"))) cl.loop = f.value(QStringLiteral("loop")).toBool();
            grow(cl.start + cl.dur);
        }
        for (PoseClip &cl : tr->poses) {
            if (cl.id != id) continue;
            if (f.contains(QStringLiteral("start"))) cl.start = std::max(0.0, std::round(f.value(QStringLiteral("start")).toDouble()));
            if (f.contains(QStringLiteral("dur"))) cl.dur = std::max(1.0, f.value(QStringLiteral("dur")).toDouble());
            if (f.contains(QStringLiteral("weight"))) cl.weight = f.value(QStringLiteral("weight")).toDouble(1);
            if (f.contains(QStringLiteral("fade_in"))) cl.fadeIn = f.value(QStringLiteral("fade_in")).toDouble();
            if (f.contains(QStringLiteral("fade_out"))) cl.fadeOut = f.value(QStringLiteral("fade_out")).toDouble();
            if (f.contains(QStringLiteral("loop"))) cl.loop = f.value(QStringLiteral("loop")).toBool();
            if (f.contains(QStringLiteral("mode"))) cl.mode = f.value(QStringLiteral("mode")).toString();
            grow(cl.start + cl.dur);
        }
        sortByStart(tr->clips);
        sortByStart(tr->poses);
    };
    o[QStringLiteral("remove_clip")] = [this](const QJsonObject &c) {
        Track *tr = trackAt(c.value(QStringLiteral("track")).toInt());
        const int id = c.value(QStringLiteral("id")).toInt();
        if (tr) {
            tr->clips.erase(std::remove_if(tr->clips.begin(), tr->clips.end(), [id](const AnimClip &x) { return x.id == id; }), tr->clips.end());
            tr->poses.erase(std::remove_if(tr->poses.begin(), tr->poses.end(), [id](const PoseClip &x) { return x.id == id; }), tr->poses.end());
        }
        if (m_selClip.value(QStringLiteral("id")).toInt() == id) m_selClip = {};
    };
    o[QStringLiteral("select_clip")] = [this](const QJsonObject &c) {
        if (c.contains(QStringLiteral("track")) && c.contains(QStringLiteral("id")))
            m_selClip = {{QStringLiteral("track"), c.value(QStringLiteral("track"))}, {QStringLiteral("id"), c.value(QStringLiteral("id"))}, {QStringLiteral("kind"), c.value(QStringLiteral("kind"))}};
        else m_selClip = {};
    };
    auto withClip = [this](const QJsonObject &c, const std::function<void(Track &, AnimClip *, PoseClip *)> &fn) {
        Track *tr = trackAt(c.value(QStringLiteral("track")).toInt());
        if (!tr) return;
        const int id = c.value(QStringLiteral("id")).toInt();
        for (int i = 0; i < tr->clips.size(); ++i) if (tr->clips[i].id == id) { fn(*tr, &tr->clips[i], nullptr); return; }
        for (int i = 0; i < tr->poses.size(); ++i) if (tr->poses[i].id == id) { fn(*tr, nullptr, &tr->poses[i]); return; }
    };
    o[QStringLiteral("split_clip")] = [this, withClip](const QJsonObject &c) {
        const double t = num(c, "t", m_t);
        withClip(c, [&](Track &tr, AnimClip *ac, PoseClip *pc) {
            if (ac) {
                const double lt = std::round(t - ac->start);
                if (lt <= 0 || lt >= ac->dur) return;
                AnimClip right = *ac;
                right.id = newId();
                right.start = ac->start + lt;
                right.dur = ac->dur - lt;
                right.offset = clipFrame(*ac, ac->start + lt);
                right.blend = 0;
                ac->dur = lt;
                tr.clips << right;
                sortByStart(tr.clips);
            } else {
                const double lt = std::round(t - pc->start);
                if (lt <= 0 || lt >= pc->dur) return;
                PoseClip right = *pc;
                right.id = newId();
                right.start = pc->start + lt;
                right.dur = pc->dur - lt;
                right.keys.clear();
                for (const PoseKey &k : pc->keys) if (k.t >= lt) { PoseKey x = k; x.t -= lt; right.keys << x; }
                pc->keys.erase(std::remove_if(pc->keys.begin(), pc->keys.end(), [lt](const PoseKey &k) { return k.t >= lt; }), pc->keys.end());
                right.fadeIn = 0;
                pc->fadeOut = 0;
                pc->dur = lt;
                tr.poses << right;
                sortByStart(tr.poses);
            }
        });
    };
    o[QStringLiteral("trim_clip")] = [this, withClip](const QJsonObject &c) {
        const double t = num(c, "t", m_t);
        const bool left = c.value(QStringLiteral("side")).toString(QStringLiteral("left")) == QLatin1String("left");
        withClip(c, [&](Track &tr, AnimClip *ac, PoseClip *pc) {
            double &start = ac ? ac->start : pc->start;
            double &dur = ac ? ac->dur : pc->dur;
            const double lt = std::round(t - start);
            if (lt <= 0 || lt >= dur) return;
            if (left) {
                if (ac) ac->offset = clipFrame(*ac, ac->start + lt);
                if (pc) {
                    pc->keys.erase(std::remove_if(pc->keys.begin(), pc->keys.end(), [lt](const PoseKey &k) { return k.t < lt; }), pc->keys.end());
                    for (PoseKey &k : pc->keys) k.t -= lt;
                }
                start += lt;
                dur -= lt;
            } else {
                if (pc) pc->keys.erase(std::remove_if(pc->keys.begin(), pc->keys.end(), [lt](const PoseKey &k) { return k.t > lt; }), pc->keys.end());
                dur = lt;
            }
            sortByStart(tr.clips);
            sortByStart(tr.poses);
        });
    };
    auto copyClipTo = [this, withClip](const QJsonObject &c, bool after) {
        const double t = std::floor(num(c, "t", m_t));
        withClip(c, [&](Track &tr, AnimClip *ac, PoseClip *pc) {
            int nid = newId();
            if (ac) { AnimClip n = *ac; n.id = nid; n.start = after ? ac->start + ac->dur : std::max(0.0, t); tr.clips << n; sortByStart(tr.clips); grow(n.start + n.dur); }
            else { PoseClip n = *pc; n.id = nid; n.start = after ? pc->start + pc->dur : std::max(0.0, t); tr.poses << n; sortByStart(tr.poses); grow(n.start + n.dur); }
            const int idx = int(&tr - m_doc.seq.tracks.data()) + 1;
            m_selClip = {{QStringLiteral("track"), idx}, {QStringLiteral("id"), nid}, {QStringLiteral("kind"), tr.kind}};
        });
    };
    o[QStringLiteral("paste_clip")] = [copyClipTo](const QJsonObject &c) { copyClipTo(c, false); };
    o[QStringLiteral("duplicate_clip")] = [copyClipTo](const QJsonObject &c) { copyClipTo(c, true); };

    // ---- keys
    o[QStringLiteral("key_transform")] = [this](const QJsonObject &c) {
        Actor *a = argActor(c);
        if (!a) return;
        const QVector3D pos = v3(c.value(QStringLiteral("pos")));
        const QVector3D e = v3(c.value(QStringLiteral("euler")));
        const QQuaternion rot = fm::fromEulerDeg(e.x(), e.y(), e.z());
        keyTransform(*a, std::floor(num(c, "t", m_t)), c.contains(QStringLiteral("pos")) ? &pos : nullptr, c.contains(QStringLiteral("euler")) ? &rot : nullptr);
    };
    o[QStringLiteral("remove_xform_key")] = [this](const QJsonObject &c) {
        if (Track *tr = trackAt(c.value(QStringLiteral("track")).toInt())) {
            const double t = num(c, "t");
            tr->keys.erase(std::remove_if(tr->keys.begin(), tr->keys.end(), [t](const XKey &k) { return std::abs(k.t - t) < 0.5; }), tr->keys.end());
        }
    };
    m_ops[QStringLiteral("remove_cam_key")] = m_ops[QStringLiteral("remove_xform_key")];
    o[QStringLiteral("move_key")] = [this](const QJsonObject &c) {
        if (Track *tr = trackAt(c.value(QStringLiteral("track")).toInt())) {
            const double t = num(c, "t"), nt = std::max(0.0, std::round(num(c, "new_t")));
            for (XKey &k : tr->keys) if (std::abs(k.t - t) < 0.5) { k.t = nt; grow(nt + 1); break; }
            sortKeys(tr->keys);
        }
    };
    // several keys at once: {track, id?, t}; pose keys live in clips
    auto forEachKey = [this](const QJsonArray &items, const std::function<void(Track &, XKey *, PoseClip *, PoseKey *)> &fn) {
        QSet<const void *> seen;
        for (const QJsonValue &v : items) {
            const QJsonObject it = v.toObject();
            Track *tr = trackAt(it.value(QStringLiteral("track")).toInt());
            if (!tr) continue;
            const double t = it.value(QStringLiteral("t")).toDouble();
            if (tr->kind == QLatin1String("pose")) {
                const int id = it.value(QStringLiteral("id")).toInt();
                for (PoseClip &c : tr->poses) {
                    if (c.id != id) continue;
                    for (PoseKey &k : c.keys)
                        if (std::abs(c.start + k.t - t) < 0.5 && !seen.contains(&k)) { seen.insert(&k); fn(*tr, nullptr, &c, &k); }
                }
            } else {
                for (XKey &k : tr->keys)
                    if (std::abs(k.t - t) < 0.5 && !seen.contains(&k)) { seen.insert(&k); fn(*tr, &k, nullptr, nullptr); }
            }
        }
    };
    auto finishKeys = [this]() {
        for (Track &tr : m_doc.seq.tracks) {
            sortKeys(tr.keys);
            if (!tr.keys.isEmpty()) grow(tr.keys.last().t + 1);
            for (PoseClip &c : tr.poses) {
                sortPoseKeys(c.keys);
                if (!c.keys.isEmpty() && c.keys.last().t + 1 > c.dur) c.dur = c.keys.last().t + 1;
                grow(c.start + c.dur);
            }
        }
    };
    o[QStringLiteral("move_keys")] = [this, forEachKey, finishKeys](const QJsonObject &c) {
        const double dt = std::round(num(c, "dt"));
        forEachKey(c.value(QStringLiteral("items")).toArray(), [&](Track &, XKey *x, PoseClip *pc, PoseKey *pk) {
            if (x) x->t = std::max(0.0, std::round(x->t + dt));
            else pk->t = std::max(0.0, std::round(pk->t + dt));
            Q_UNUSED(pc);
        });
        finishKeys();
    };
    o[QStringLiteral("scale_keys")] = [this, forEachKey, finishKeys](const QJsonObject &c) {
        const double pivot = num(c, "pivot"), factor = num(c, "factor", 1);
        forEachKey(c.value(QStringLiteral("items")).toArray(), [&](Track &, XKey *x, PoseClip *pc, PoseKey *pk) {
            if (x) x->t = std::max(0.0, std::round(pivot + (x->t - pivot) * factor));
            else { const double at = pc->start + pk->t; pk->t = std::max(0.0, std::round(pivot + (at - pivot) * factor - pc->start)); }
        });
        finishKeys();
    };
    o[QStringLiteral("remove_keys")] = [this, forEachKey](const QJsonObject &c) {
        QSet<const void *> doomed;
        forEachKey(c.value(QStringLiteral("items")).toArray(), [&](Track &, XKey *x, PoseClip *, PoseKey *pk) { doomed.insert(x ? static_cast<const void *>(x) : static_cast<const void *>(pk)); });
        for (Track &tr : m_doc.seq.tracks) {
            tr.keys.erase(std::remove_if(tr.keys.begin(), tr.keys.end(), [&](const XKey &k) { return doomed.contains(&k); }), tr.keys.end());
            for (PoseClip &pc : tr.poses)
                pc.keys.erase(std::remove_if(pc.keys.begin(), pc.keys.end(), [&](const PoseKey &k) { return doomed.contains(&k); }), pc.keys.end());
        }
    };
    o[QStringLiteral("set_ease")] = [this, forEachKey](const QJsonObject &c) {
        QJsonArray items = c.value(QStringLiteral("items")).toArray();
        if (items.isEmpty()) items.append(QJsonObject{{QStringLiteral("track"), c.value(QStringLiteral("track"))}, {QStringLiteral("id"), c.value(QStringLiteral("id"))}, {QStringLiteral("t"), c.value(QStringLiteral("t"))}});
        QString ease = c.value(QStringLiteral("ease")).toString(QStringLiteral("smooth"));
        if (ease == QLatin1String("smooth")) ease.clear();
        forEachKey(items, [&](Track &, XKey *x, PoseClip *, PoseKey *pk) { if (x) x->ease = ease; else pk->ease = ease; });
    };
    o[QStringLiteral("set_key_value")] = [this, forEachKey](const QJsonObject &c) {
        QJsonArray items = c.value(QStringLiteral("items")).toArray();
        if (items.isEmpty()) items.append(QJsonObject{{QStringLiteral("track"), c.value(QStringLiteral("track"))}, {QStringLiteral("id"), c.value(QStringLiteral("id"))}, {QStringLiteral("t"), c.value(QStringLiteral("t"))}});
        const QString field = c.value(QStringLiteral("field")).toString();
        const double value = num(c, "value");
        forEachKey(items, [&](Track &tr, XKey *x, PoseClip *, PoseKey *) {
            if (!x) return;
            if (field == QLatin1String("x")) x->pos.setX(float(value));
            else if (field == QLatin1String("y")) x->pos.setY(float(value));
            else if (field == QLatin1String("z")) x->pos.setZ(float(value));
            else if (field == QLatin1String("fov") && tr.kind == QLatin1String("cammove")) x->fov = value;
            else if (field == QLatin1String("roll") && tr.kind == QLatin1String("cammove")) x->roll = value;
            else if (field == QLatin1String("yaw") && tr.kind == QLatin1String("xform")) x->rot = fm::fromEulerDeg(0, value, 0);
            else if (field == QLatin1String("v") && tr.kind == QLatin1String("channel")) x->v = value;
        });
    };
    o[QStringLiteral("copy_keys")] = [this, forEachKey](const QJsonObject &c) {
        struct Item { double t; QString kind; XKey x; int cam = 0; PoseKey pk; QString target, field; };
        QVector<Item> items;
        forEachKey(c.value(QStringLiteral("items")).toArray(), [&](Track &tr, XKey *x, PoseClip *pc, PoseKey *pk) {
            Item it;
            it.kind = tr.kind;
            it.target = tr.target;
            it.field = tr.field;
            if (x) { it.t = x->t; it.x = *x; it.cam = tr.cam; }
            else { it.t = pc->start + pk->t; it.pk = *pk; }
            items << it;
        });
        if (items.isEmpty()) return;
        double t0 = 1e18;
        for (const Item &i : items) t0 = std::min(t0, i.t);
        QJsonArray xf, cm, po, ch;
        for (const Item &i : items) {
            const double rel = i.t - t0;
            if (i.kind == QLatin1String("channel")) {
                ch.append(QJsonObject{{QStringLiteral("t"), rel}, {QStringLiteral("v"), i.x.v}, {QStringLiteral("ease"), i.x.ease},
                                      {QStringLiteral("target"), i.target}, {QStringLiteral("field"), i.field}});
                continue;
            }
            if (i.kind == QLatin1String("xform")) xf.append(QJsonObject{{QStringLiteral("t"), rel}, {QStringLiteral("pos"), fm::arr(i.x.pos)}, {QStringLiteral("rot"), fm::arr(i.x.rot)}, {QStringLiteral("ease"), i.x.ease}});
            else if (i.kind == QLatin1String("cammove"))
                cm.append(QJsonObject{{QStringLiteral("t"), rel}, {QStringLiteral("pos"), fm::arr(i.x.pos)}, {QStringLiteral("rot"), fm::arr(i.x.rot)}, {QStringLiteral("fov"), i.x.fov},
                                      {QStringLiteral("roll"), i.x.roll}, {QStringLiteral("ease"), i.x.ease}, {QStringLiteral("cam"), i.cam}});
            else {
                QJsonObject j;
                for (auto it = i.pk.joints.cbegin(); it != i.pk.joints.cend(); ++it) j.insert(it.key(), fm::arr(it.value()));
                po.append(QJsonObject{{QStringLiteral("t"), rel}, {QStringLiteral("joints"), j}, {QStringLiteral("ease"), i.pk.ease}});
            }
        }
        m_keyClipboard = {{QStringLiteral("xform"), xf}, {QStringLiteral("cammove"), cm}, {QStringLiteral("pose"), po}, {QStringLiteral("channel"), ch},
                          {QStringLiteral("count"), int(items.size())}};
    };
    o[QStringLiteral("paste_keys")] = [this](const QJsonObject &c) {
        if (m_keyClipboard.isEmpty()) return;
        const double t = std::floor(num(c, "t", m_t));
        if (Actor *a = argActor(c)) {
            for (const QJsonValue &v : m_keyClipboard.value(QStringLiteral("xform")).toArray()) {
                const QJsonObject k = v.toObject();
                const QVector3D p = v3(k.value(QStringLiteral("pos")));
                const QQuaternion r = fm::quat(k.value(QStringLiteral("rot")));
                keyTransform(*a, std::round(t + k.value(QStringLiteral("t")).toDouble()), &p, &r);
            }
            for (const QJsonValue &v : m_keyClipboard.value(QStringLiteral("pose")).toArray()) {
                const QJsonObject k = v.toObject();
                QMap<QString, QQuaternion> j;
                const QJsonObject jo = k.value(QStringLiteral("joints")).toObject();
                for (auto it = jo.begin(); it != jo.end(); ++it) j.insert(it.key(), fm::quat(it.value()));
                keyframePose(*a, std::round(t + k.value(QStringLiteral("t")).toDouble()), j);
            }
        }
        for (const QJsonValue &v : m_keyClipboard.value(QStringLiteral("cammove")).toArray()) {
            const QJsonObject k = v.toObject();
            const int cam = k.value(QStringLiteral("cam")).toInt();
            if (!m_doc.camera(cam) || cam <= 0) continue;
            Track &tr = cammoveTrack(cam);
            XKey x;
            x.t = std::round(t + k.value(QStringLiteral("t")).toDouble());
            x.pos = v3(k.value(QStringLiteral("pos")));
            x.rot = fm::quat(k.value(QStringLiteral("rot")));
            x.fov = k.value(QStringLiteral("fov")).toDouble();
            x.roll = k.value(QStringLiteral("roll")).toDouble();
            x.ease = k.value(QStringLiteral("ease")).toString();
            tr.keys.erase(std::remove_if(tr.keys.begin(), tr.keys.end(), [&](const XKey &y) { return std::abs(y.t - x.t) < 0.5; }), tr.keys.end());
            tr.keys << x;
            sortKeys(tr.keys);
            grow(x.t + 1);
        }
        // channel keys go back to their own values (a light's intensity onto the same light)
        for (const QJsonValue &v : m_keyClipboard.value(QStringLiteral("channel")).toArray()) {
            const QJsonObject k = v.toObject();
            const QString target = k.value(QStringLiteral("target")).toString(), field = k.value(QStringLiteral("field")).toString();
            const double at = std::round(t + k.value(QStringLiteral("t")).toDouble());
            keyChannel(target, field, at, k.value(QStringLiteral("v")).toDouble());
            if (Track *tr = channelTrack(target, field))
                for (XKey &x : tr->keys) if (std::abs(x.t - at) < 0.5) x.ease = k.value(QStringLiteral("ease")).toString();
        }
    };
    o[QStringLiteral("key_selection")] = [this](const QJsonObject &c) {
        const double t = std::floor(num(c, "t", m_t));
        Actor *a = selectedActor();
        const QString target = m_gizmo.value(QStringLiteral("target")).toString();
        if (target == QLatin1String("camera") || (!a && m_selCamera > 0)) {
            const int i = m_activeCam > 0 ? m_activeCam : m_selCamera;
            if (m_doc.camera(i) && i > 0) keyCamera(i, t);
            return;
        }
        if (!a) return;
        if (target == QLatin1String("bone") && !a->work.isEmpty()) keyframePose(*a, t, a->work);
        else keyTransform(*a, t);
    };
    o[QStringLiteral("remove_pose_key")] = [this](const QJsonObject &c) {
        Track *tr = trackAt(c.value(QStringLiteral("track")).toInt());
        if (!tr) return;
        const int id = c.value(QStringLiteral("id")).toInt();
        const double t = num(c, "t");
        for (PoseClip &pc : tr->poses)
            if (pc.id == id) pc.keys.erase(std::remove_if(pc.keys.begin(), pc.keys.end(), [t](const PoseKey &k) { return std::abs(k.t - t) < 0.5; }), pc.keys.end());
    };
    o[QStringLiteral("move_pose_key")] = [this](const QJsonObject &c) {
        Track *tr = trackAt(c.value(QStringLiteral("track")).toInt());
        if (!tr) return;
        const int id = c.value(QStringLiteral("id")).toInt();
        const double t = num(c, "t"), nt = std::max(0.0, std::round(num(c, "new_t")));
        for (PoseClip &pc : tr->poses) {
            if (pc.id != id) continue;
            for (PoseKey &k : pc.keys) if (std::abs(k.t - t) < 0.5) { k.t = nt; if (nt + 1 > pc.dur) pc.dur = nt + 1; break; }
            sortPoseKeys(pc.keys);
        }
    };

    // ---- cameras
    o[QStringLiteral("cam_capture")] = [this](const QJsonObject &c) {
        const ViewFrame v = currentView();
        Camera cam;
        cam.name = c.value(QStringLiteral("name")).toString(QStringLiteral("Cam %1").arg(m_doc.cameras.size() + 1));
        cam.pos = v.pos;
        cam.rot = v.rot;
        cam.fov = v.fov;
        m_doc.cameras << cam;
        m_selCamera = int(m_doc.cameras.size());
        log(QStringLiteral("info"), QStringLiteral("camera %1 made from the view").arg(cam.name));
    };
    o[QStringLiteral("cam_select")] = [this](const QJsonObject &c) { m_selCamera = c.value(QStringLiteral("i")).toInt(); };
    o[QStringLiteral("cam_live")] = [this](const QJsonObject &c) {
        m_camFollow = false;
        m_sceneView = false;
        const int i = c.value(QStringLiteral("i")).toInt();
        setActiveCamera(i);
        if (i > 0) m_selCamera = i;
    };
    // ---- hover previews (the Asset Browser): a model turning in front of the camera
    o[QStringLiteral("cast_preview")] = [this](const QJsonObject &c) {
        const QString id = c.value(QStringLiteral("id")).toString(), preset = c.value(QStringLiteral("preset")).toString();
        for (const QJsonValue &v : std::as_const(m_cast)) {
            const QJsonObject ch = v.toObject();
            if (ch.value(QStringLiteral("id")).toString() != id) continue;
            const QJsonArray presets = ch.value(QStringLiteral("presets")).toArray();
            QJsonObject pick = presets.isEmpty() ? QJsonObject() : presets.first().toObject();
            for (const QJsonValue &p : presets) if (p.toObject().value(QStringLiteral("name")).toString() == preset) pick = p.toObject();
            setPreview(pick.value(QStringLiteral("model")).toString(), ch.value(QStringLiteral("name")).toString());
            return;
        }
    };
    o[QStringLiteral("mesh_preview")] = [this](const QJsonObject &c) {
        const QString mesh = c.value(QStringLiteral("mesh")).toString(), mdf = c.value(QStringLiteral("mdf")).toString();
        const QString game = c.value(QStringLiteral("game")).toString(m_catalogGame);
        if (!mesh.isEmpty()) setPreview(qualify(game, QStringLiteral("mesh:") + mesh + (mdf.isEmpty() ? QString() : QLatin1Char('|') + mdf)), c.value(QStringLiteral("name")).toString());
    };
    o[QStringLiteral("cast_preview_clear")] = [this](const QJsonObject &) { setPreview({}, {}); };
    o[QStringLiteral("mesh_preview_clear")] = o[QStringLiteral("cast_preview_clear")];

    // ---- maps (the Asset Browser's Stages tab)
    o[QStringLiteral("stage_list")] = [](const QJsonObject &) {};      // listed with the game's catalogue
    o[QStringLiteral("stage_go")] = [this](const QJsonObject &c) {
        const int number = c.value(QStringLiteral("stage")).toInt();
        QString id = c.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            for (const dir::CatalogEntry &e : std::as_const(m_stageCatalog))
                if (e.extra.value(QStringLiteral("stage")).toInt() == number) { id = e.id; break; }
        if (id.isEmpty()) { log(QStringLiteral("error"), QStringLiteral("no map %1 in this game").arg(number)); return; }
        m_doc.stageId = qualify(m_catalogGame, id);
        m_doc.stageNumber = number;
        m_doc.stageName = c.value(QStringLiteral("name")).toString(QStringLiteral("st%1").arg(number));
        // the Work Camera goes to the place: a few steps back from it at eye height, looking the way it looked
        const QJsonArray p = c.value(QStringLiteral("pos")).toArray();
        if (p.size() == 3) {
            const QVector3D at(float(p[0].toDouble()), float(p[1].toDouble()), float(p[2].toDouble()));
            QVector3D f = fm::forward(m_doc.work.rot);
            f.setY(0);
            if (f.length() < 0.01f) f = QVector3D(0, 0, -1);
            f.normalize();
            m_doc.work.pos = at - f * 4.5f + QVector3D(0, 1.7f, 0);
            m_doc.work.rot = fm::lookRotation(m_doc.work.pos, at + QVector3D(0, 1.1f, 0));
            m_camFollow = false;
            m_sceneView = false;
            setActiveCamera(0);
            m_stageArrive = at;                               // map heights are approximate: settle on the ground once loaded
            m_stageArriveCenter = false;
        } else {
            m_stageArrive.reset();
            m_stageArriveCenter = true;
        }
        ensureStage();
    };
    o[QStringLiteral("stage_cancel")] = [this](const QJsonObject &) {
        if (m_stageLoading.isEmpty()) return;
        m_doc.stageId = m_stageShown;
        m_doc.stageName = m_stageShownName;
        m_doc.stageNumber = m_stageShownNumber;
        ensureStage();
        if (m_stageShown.isEmpty()) m_stageStatus = {};
    };
    o[QStringLiteral("stage_clear")] = [this](const QJsonObject &) {
        m_doc.stageId.clear();
        m_doc.stageName.clear();
        m_doc.stageNumber = 0;
        ensureStage();
    };
    // put the Work Camera somewhere and aim it: {pos: [x, y, z], target: [x, y, z]} (automation, "go to")
    o[QStringLiteral("view_set")] = [this](const QJsonObject &c) {
        if (c.contains(QStringLiteral("pos"))) m_doc.work.pos = fm::vec3(c.value(QStringLiteral("pos")));
        if (c.contains(QStringLiteral("target"))) m_doc.work.rot = fm::lookRotation(m_doc.work.pos, fm::vec3(c.value(QStringLiteral("target"))));
        m_camFollow = false;
        m_sceneView = false;
        setActiveCamera(0);
    };
    o[QStringLiteral("cam_work")] = [this](const QJsonObject &c) {
        m_camFollow = false;
        m_sceneView = false;
        const bool on = c.value(QStringLiteral("value")).toBool(true);
        if (on && c.value(QStringLiteral("from_view")).toBool()) { const ViewFrame v = currentView(); m_doc.work.pos = v.pos; m_doc.work.rot = v.rot; m_doc.work.fov = v.fov; }
        if (on) setActiveCamera(0);
        else if (m_doc.cameras.size()) setActiveCamera(m_selCamera > 0 ? m_selCamera : 1);
    };
    o[QStringLiteral("cam_scene")] = [this](const QJsonObject &) {
        m_sceneView = true;
        m_camFollow = true;
        const int i = filmCamera(m_t);
        if (i) { setActiveCamera(i); m_selCamera = i; }
        else if (m_selCamera > 0) setActiveCamera(m_selCamera);
        else if (!m_doc.cameras.isEmpty()) setActiveCamera(1);
        else { m_sceneView = false; log(QStringLiteral("info"), QStringLiteral("no scene camera yet - press C to make one from the view")); }
    };
    o[QStringLiteral("cam_fly")] = [this](const QJsonObject &c) { m_fly = c.value(QStringLiteral("value")).toBool(); };
    o[QStringLiteral("cam_stop")] = [this](const QJsonObject &) { setActiveCamera(0); m_sceneView = false; };
    o[QStringLiteral("cam_remove")] = [this](const QJsonObject &c) {
        const int i = c.value(QStringLiteral("i")).toInt();
        if (i < 1 || i > m_doc.cameras.size()) return;
        m_doc.cameras.removeAt(i - 1);
        Sequence &s = m_doc.seq;
        for (int k = int(s.tracks.size()) - 1; k >= 0; --k) {
            Track &tr = s.tracks[k];
            if (tr.kind == QLatin1String("cammove")) { if (tr.cam == i) s.tracks.removeAt(k); else if (tr.cam > i) tr.cam--; }
            else if (tr.kind == QLatin1String("channel") && tr.target.startsWith(QLatin1String("cam:"))) {
                const int ci = tr.target.mid(4).toInt();
                if (ci == i) s.tracks.removeAt(k);
                else if (ci > i) tr.target = QStringLiteral("cam:%1").arg(ci - 1);
            }
            else if (tr.kind == QLatin1String("camera")) {
                tr.cuts.erase(std::remove_if(tr.cuts.begin(), tr.cuts.end(), [i](const Cut &x) { return x.cam == i; }), tr.cuts.end());
                for (Cut &x : tr.cuts) if (x.cam > i) x.cam--;
            }
        }
        for (Shot &sh : s.shots) { if (sh.cam == i) sh.cam = 0; else if (sh.cam > i) sh.cam--; }
        if (m_activeCam == i) setActiveCamera(0);
        else if (m_activeCam > i) m_activeCam--;
        if (m_selCamera == i) m_selCamera = 0;
        else if (m_selCamera > i) m_selCamera--;
    };
    o[QStringLiteral("cam_update")] = [this](const QJsonObject &c) {
        Camera *cam = m_doc.camera(c.value(QStringLiteral("i")).toInt(m_selCamera));
        if (!cam) return;
        const QJsonObject f = c.value(QStringLiteral("fields")).toObject();
        for (auto it = f.begin(); it != f.end(); ++it) {
            const QString k = it.key();
            if (k == QLatin1String("euler")) { const QVector3D e = v3(it.value()); cam->rot = fm::fromEulerDeg(e.x(), e.y(), e.z()); }
            else if (k == QLatin1String("pos")) cam->pos = v3(it.value());
            else if (k == QLatin1String("fov")) cam->fov = std::clamp(it.value().toDouble(50), 1.0, 170.0);
            else if (k == QLatin1String("roll")) cam->roll = it.value().toDouble();
            else if (k == QLatin1String("name")) cam->name = it.value().toString();
            else if (k == QLatin1String("mode")) cam->mode = it.value().toString(QStringLiteral("static"));
            else if (k == QLatin1String("target")) cam->target = qint64(it.value().toDouble());
            else if (k == QLatin1String("shake")) cam->shake = it.value().toObject();
        }
    };
    o[QStringLiteral("cam_recapture")] = [this](const QJsonObject &c) {
        Camera *cam = m_doc.camera(c.value(QStringLiteral("i")).toInt(m_selCamera));
        if (!cam || cam == &m_doc.work) return;
        cam->pos = m_doc.work.pos;
        cam->rot = m_doc.work.rot;
    };
    o[QStringLiteral("key_camera")] = [this](const QJsonObject &c) {
        int i = c.value(QStringLiteral("i")).toInt();
        if (!i) i = m_activeCam > 0 ? m_activeCam : m_selCamera;
        if (i <= 0 || !m_doc.camera(i)) { log(QStringLiteral("info"), QStringLiteral("key camera: select or look through a scene camera first (C makes one from the view)")); return; }
        keyCamera(i, std::floor(num(c, "t", m_t)));
    };
    o[QStringLiteral("cam_dof")] = [this](const QJsonObject &c) { if (Camera *cam = m_doc.camera(c.value(QStringLiteral("i")).toInt(m_selCamera))) cam->dof = c.value(QStringLiteral("value")).toBool(); };
    o[QStringLiteral("cam_dof_params")] = [this](const QJsonObject &c) {
        Camera *cam = m_doc.camera(c.value(QStringLiteral("i")).toInt(m_selCamera));
        if (!cam) return;
        if (c.contains(QStringLiteral("f"))) cam->dofF = c.value(QStringLiteral("f")).isDouble() ? std::optional<double>(c.value(QStringLiteral("f")).toDouble()) : std::nullopt;
        if (c.contains(QStringLiteral("focus"))) cam->dofFocus = c.value(QStringLiteral("focus")).isDouble() ? std::optional<double>(c.value(QStringLiteral("focus")).toDouble()) : std::nullopt;
        cam->dof = true;
    };
    o[QStringLiteral("cam_focus_selection")] = [this](const QJsonObject &c) {
        const int i = c.value(QStringLiteral("i")).toInt(m_selCamera);
        Camera *cam = m_doc.camera(i);
        Actor *a = selectedActor();
        if (!cam || !a) return;
        QVector3D p = a->pos;
        for (const ActorFrame &f : std::as_const(m_frames)) if (f.id == a->id) p = f.pos;
        if (a->kind != QLatin1String("object")) p.setY(p.y() + 1.25f);
        cam->dof = true;
        if (!cam->dofF) cam->dofF = 2.8;
        cam->dofFocus = (p - cam->pos).length();
        if (c.value(QStringLiteral("key")).toBool()) keyCamera(i, std::floor(num(c, "t", m_t)));
    };
    o[QStringLiteral("cam_overlay")] = [this](const QJsonObject &c) {
        const QJsonObject f = c.value(QStringLiteral("fields")).toObject();
        for (auto it = f.begin(); it != f.end(); ++it) m_doc.overlay.insert(it.key(), it.value());
    };
    auto framingCamera = [this](const QString &name, const QVector3D &eyeOffset, double heightFrac, double fov) {
        Actor *a = selectedActor();
        if (!a) return;
        QVector3D p = a->pos;
        QQuaternion r = a->rot;
        for (const ActorFrame &f : std::as_const(m_frames)) if (f.id == a->id) { p = f.pos; r = f.rot; }
        const float h = 1.75f * float(heightFrac);
        Camera cam;
        cam.name = name;
        cam.pos = p + r.rotatedVector(eyeOffset) + QVector3D(0, h, 0);
        cam.rot = fm::lookRotation(cam.pos, p + QVector3D(0, h, 0));
        cam.fov = fov;
        m_doc.cameras << cam;
        m_selCamera = int(m_doc.cameras.size());
        setActiveCamera(m_selCamera);
        m_doc.overlay.insert(QStringLiteral("letterbox"), 2.39);
    };
    o[QStringLiteral("cam_preset")] = [this, framingCamera](const QJsonObject &c) {
        const QString kind = c.value(QStringLiteral("kind")).toString(QStringLiteral("medium"));
        Actor *a = selectedActor();
        const QString who = a ? (a->displayName.isEmpty() ? a->name : a->displayName) : QString();
        if (kind == QLatin1String("closeup")) framingCamera(QStringLiteral("Close-up %1").arg(who), QVector3D(0, 0, 0.9f), 0.93, 30);
        else if (kind == QLatin1String("wide")) framingCamera(QStringLiteral("Wide %1").arg(who), QVector3D(0, 0, 5.5f), 0.55, 55);
        else if (kind == QLatin1String("ots")) framingCamera(QStringLiteral("Over the shoulder %1").arg(who), QVector3D(0.45f, 0, -0.9f), 0.9, 40);
        else if (kind == QLatin1String("low")) framingCamera(QStringLiteral("Low angle %1").arg(who), QVector3D(0, 0, 2.2f), 0.2, 45);
        else framingCamera(QStringLiteral("Medium %1").arg(who), QVector3D(0, 0, 2.0f), 0.8, 40);
    };
    o[QStringLiteral("cam_lookat")] = [this](const QJsonObject &c) { m_ops.value(QStringLiteral("cam_preset"))(QJsonObject{{QStringLiteral("kind"), QStringLiteral("medium")}, {QStringLiteral("addr"), c.value(QStringLiteral("addr"))}}); };
    o[QStringLiteral("cam_orbit")] = o[QStringLiteral("cam_lookat")];
    o[QStringLiteral("cam_follow")] = o[QStringLiteral("cam_lookat")];
    o[QStringLiteral("add_cut")] = [this](const QJsonObject &c) {
        Track &tr = cameraTrack();
        const double start = std::floor(num(c, "t", m_t));
        const int cam = c.value(QStringLiteral("cam")).toInt(m_selCamera);
        if (cam <= 0) return;
        for (Cut &x : tr.cuts) if (std::abs(x.start - start) < 0.5) { x.cam = cam; return; }
        tr.cuts << Cut{newId(), start, cam};
        sortByStart(tr.cuts);
    };
    o[QStringLiteral("update_cut")] = [this](const QJsonObject &c) {
        Track *tr = trackAt(c.value(QStringLiteral("track")).toInt());
        if (!tr || tr->kind != QLatin1String("camera")) return;
        for (Cut &x : tr->cuts)
            if (x.id == c.value(QStringLiteral("id")).toInt()) {
                if (c.contains(QStringLiteral("start"))) x.start = std::max(0.0, std::round(num(c, "start")));
                if (c.contains(QStringLiteral("cam"))) x.cam = c.value(QStringLiteral("cam")).toInt();
            }
        sortByStart(tr->cuts);
    };
    o[QStringLiteral("remove_cut")] = [this](const QJsonObject &c) {
        Track *tr = trackAt(c.value(QStringLiteral("track")).toInt());
        const int id = c.value(QStringLiteral("id")).toInt();
        if (tr) tr->cuts.erase(std::remove_if(tr->cuts.begin(), tr->cuts.end(), [id](const Cut &x) { return x.id == id; }), tr->cuts.end());
    };
    o[QStringLiteral("cammove_lookat")] = [this](const QJsonObject &c) {
        Track *tr = trackAt(c.value(QStringLiteral("track")).toInt());
        if (!tr || tr->kind != QLatin1String("cammove")) return;
        const Actor *a = m_doc.actor(addr(c));
        tr->lookatName = a ? a->name : QString();
    };

    // ---- shots
    o[QStringLiteral("shot_add")] = [this](const QJsonObject &c) {
        Sequence &s = m_doc.seq;
        Shot sh;
        sh.id = newId();
        sh.name = c.value(QStringLiteral("name")).toString(QStringLiteral("Shot %1").arg(s.shots.size() + 1));
        sh.a = std::floor(num(c, "a", s.range ? s.range->first : 0));
        sh.b = std::floor(num(c, "b", s.range ? s.range->second : s.length));
        sh.cam = c.value(QStringLiteral("cam")).toInt(m_activeCam > 0 ? m_activeCam : 0);
        s.shots << sh;                                    // the film's order: new shots come last
    };
    // the film's order: {id, to (index)} or {id, by: -1 | 1}
    o[QStringLiteral("shot_move")] = [this](const QJsonObject &c) {
        auto &v = m_doc.seq.shots;
        const int id = c.value(QStringLiteral("id")).toInt();
        int from = -1;
        for (int i = 0; i < v.size(); ++i) if (v[i].id == id) from = i;
        if (from < 0) return;
        const int to = std::clamp(c.contains(QStringLiteral("to")) ? c.value(QStringLiteral("to")).toInt() : from + c.value(QStringLiteral("by")).toInt(), 0, int(v.size()) - 1);
        if (to != from) v.move(from, to);
    };
    // play the film: the shots in order, each through its own camera {from: film frame}
    o[QStringLiteral("film_play")] = [this](const QJsonObject &c) {
        if (m_doc.seq.shots.isEmpty()) { log(QStringLiteral("warn"), QStringLiteral("the film has no shots yet: make one from a time selection")); return; }
        m_filmT = std::clamp(c.value(QStringLiteral("from")).toDouble(0), 0.0, std::max(0.0, filmLength() - 1));
        int shot = 0;
        double t = 0;
        if (filmAt(m_filmT, &shot, &t)) { m_t = t; m_shotCam = m_doc.seq.shots[shot].cam; }
        dropUnkeyedEdits();
        m_filmPlay = true;
        m_playing = true;
        m_sceneView = true;
        m_camFollow = true;
        m_workOn = false;
    };
    o[QStringLiteral("film_stop")] = [this](const QJsonObject &) { m_filmPlay = false; m_playing = false; m_shotCam = -1; };
    o[QStringLiteral("shot_remove")] = [this](const QJsonObject &c) {
        const int id = c.value(QStringLiteral("id")).toInt();
        auto &v = m_doc.seq.shots;
        v.erase(std::remove_if(v.begin(), v.end(), [id](const Shot &x) { return x.id == id; }), v.end());
    };
    o[QStringLiteral("shot_update")] = [this](const QJsonObject &c) {
        const QJsonObject f = c.value(QStringLiteral("fields")).toObject();
        for (Shot &sh : m_doc.seq.shots) {
            if (sh.id != c.value(QStringLiteral("id")).toInt()) continue;
            for (auto it = f.begin(); it != f.end(); ++it) {
                if (it.key() == QLatin1String("a")) sh.a = std::floor(it.value().toDouble());
                else if (it.key() == QLatin1String("b")) sh.b = std::floor(it.value().toDouble());
                else if (it.key() == QLatin1String("cam")) sh.cam = it.value().toInt();
                else if (it.key() == QLatin1String("name")) sh.name = it.value().toString();
                else sh.extra.insert(it.key(), it.value());
            }
        }
    };
    o[QStringLiteral("shot_go")] = [this](const QJsonObject &c) {
        for (const Shot &sh : std::as_const(m_doc.seq.shots)) {
            if (sh.id != c.value(QStringLiteral("id")).toInt()) continue;
            m_doc.seq.range = std::make_pair(sh.a, sh.b);
            m_playing = false;
            dropUnkeyedEdits();
            m_t = sh.a;
            m_camFollow = true;
            if (sh.cam > 0 && m_doc.camera(sh.cam)) { setActiveCamera(sh.cam); m_selCamera = sh.cam; }
        }
    };

    // ---- actors
    o[QStringLiteral("spawn_cast")] = [this](const QJsonObject &c) {
        const QString id = c.value(QStringLiteral("id")).toString(), presetName = c.value(QStringLiteral("preset")).toString();
        for (const QJsonValue &v : std::as_const(m_cast)) {
            const QJsonObject ch = v.toObject();
            if (ch.value(QStringLiteral("id")).toString() != id) continue;
            const QJsonArray presets = ch.value(QStringLiteral("presets")).toArray();
            QJsonObject pick = presets.isEmpty() ? QJsonObject() : presets.first().toObject();
            for (const QJsonValue &p : presets) if (p.toObject().value(QStringLiteral("name")).toString() == presetName) pick = p.toObject();
            const QString model = pick.value(QStringLiteral("model")).toString(id);
            QJsonObject extra = c;
            extra.insert(QStringLiteral("code"), ch.value(QStringLiteral("code")));
            extra.insert(QStringLiteral("tree"), ch.value(QStringLiteral("tree")));
            extra.insert(QStringLiteral("preset"), pick.value(QStringLiteral("name")));
            extra.insert(QStringLiteral("general"), ch.value(QStringLiteral("general")));
            spawnModel(model, c.value(QStringLiteral("name")).toString(ch.value(QStringLiteral("name")).toString()), QStringLiteral("character"), extra);
            return;
        }
        log(QStringLiteral("error"), QStringLiteral("spawn_cast: unknown character %1").arg(id));
    };
    o[QStringLiteral("spawn_mesh")] = [this](const QJsonObject &c) {
        const QString mesh = c.value(QStringLiteral("mesh")).toString();
        if (mesh.isEmpty()) return;
        spawnModel(qualify(c.value(QStringLiteral("game")).toString(m_catalogGame), QStringLiteral("mesh:") + mesh),
                   c.value(QStringLiteral("name")).toString(mesh.section(QLatin1Char('/'), -1).section(QLatin1Char('.'), 0, 0)), QStringLiteral("object"), c);
    };
    o[QStringLiteral("cast_preset")] = [this](const QJsonObject &c) {
        Actor *a = argActor(c);
        if (!a || a->castId.isEmpty()) return;
        for (const QJsonValue &v : castOf(*a)) {
            const QJsonObject ch = v.toObject();
            if (ch.value(QStringLiteral("id")).toString() != a->castId) continue;
            const QJsonArray presets = ch.value(QStringLiteral("presets")).toArray();
            QJsonObject pick;
            const QJsonValue want = c.value(QStringLiteral("preset"));
            for (int i = 0; i < presets.size(); ++i) {
                const QJsonObject p = presets[i].toObject();
                if ((want.isString() && p.value(QStringLiteral("name")).toString() == want.toString()) || (want.isDouble() && i + 1 == want.toInt())) pick = p;
            }
            if (pick.isEmpty()) return;
            a->model = pick.value(QStringLiteral("model")).toString();
            a->preset = pick.value(QStringLiteral("name")).toString();
            m_models.remove(a->id);
            m_bindings.clear();
            if (m_scene) m_scene->removeActor(a->id);
            ensureModel(a->id);
        }
    };
    o[QStringLiteral("select_actor")] = [this](const QJsonObject &c) {
        const qint64 id = addr(c);
        if (c.value(QStringLiteral("add")).toBool() && m_doc.actor(id)) {
            if (m_selActor && m_selActor != id) m_multi.insert(m_selActor);
            if (m_multi.contains(id) && m_selActor != id) m_multi.remove(id);
            else { m_multi.insert(id); m_selActor = id; }
        } else {
            m_multi.clear();
            m_selActor = m_doc.actor(id) ? id : 0;
        }
        markData();
    };
    o[QStringLiteral("select_clear")] = [this](const QJsonObject &) { m_multi.clear(); m_selActor = 0; markData(); };
    o[QStringLiteral("add_actor")] = o[QStringLiteral("select_actor")];
    o[QStringLiteral("set_transform")] = [this](const QJsonObject &c) {
        Actor *a = argActor(c);
        if (!a) return;
        if (c.contains(QStringLiteral("pos"))) a->pos = v3(c.value(QStringLiteral("pos")));
        if (c.contains(QStringLiteral("euler"))) { const QVector3D e = v3(c.value(QStringLiteral("euler"))); a->rot = fm::fromEulerDeg(e.x(), e.y(), e.z()); }
        bool keyed = false;
        for (const Track &tr : std::as_const(m_doc.seq.tracks)) if (tr.kind == QLatin1String("xform") && tr.actor == a->id && !tr.keys.isEmpty()) keyed = true;
        if (keyed || m_autokey) keyTransform(*a, std::floor(m_t), &a->pos, &a->rot);   // a keyed placement changes at the playhead
    };
    o[QStringLiteral("set_scale")] = [this](const QJsonObject &c) {
        Actor *a = argActor(c);
        if (a && c.contains(QStringLiteral("scale"))) a->scale = v3(c.value(QStringLiteral("scale")), QVector3D(1, 1, 1));
    };
    o[QStringLiteral("move_to_camera")] = [this](const QJsonObject &c) {
        Actor *a = argActor(c);
        if (!a) return;
        QQuaternion f;
        a->pos = spawnPoint(0.0, &f);
    };
    o[QStringLiteral("face_camera")] = [this](const QJsonObject &c) {
        Actor *a = argActor(c);
        if (!a) return;
        const QVector3D d = currentView().pos - a->pos;
        if (QVector2D(d.x(), d.z()).length() < 0.01f) return;
        a->rot = fm::fromEulerDeg(0, std::atan2(d.x(), d.z()) * 180 / fm::kPi, 0);
    };
    o[QStringLiteral("flip_facing")] = [this](const QJsonObject &c) { if (Actor *a = argActor(c)) a->rot = (fm::fromEulerDeg(0, 180, 0) * a->rot).normalized(); };
    o[QStringLiteral("set_root_lock")] = [this](const QJsonObject &c) { if (Actor *a = argActor(c)) a->rootLock = c.value(QStringLiteral("value")).toBool(); };
    o[QStringLiteral("cast_physics")] = [this](const QJsonObject &c) {
        if (Actor *a = argActor(c)) { a->physics = c.value(QStringLiteral("value")).toBool(true); m_chains.remove(a->id); }
    };
    o[QStringLiteral("actor_visible")] = [this](const QJsonObject &c) { if (Actor *a = argActor(c)) a->hidden = !c.value(QStringLiteral("value")).toBool(true); };
    o[QStringLiteral("destroy_object")] = [this](const QJsonObject &c) { if (Actor *a = argActor(c)) removeActor(a->id); };
    o[QStringLiteral("forget_actor")] = o[QStringLiteral("destroy_object")];
    o[QStringLiteral("destroy_cast_all")] = [this](const QJsonObject &) {
        QList<qint64> ids;
        for (const Actor &a : std::as_const(m_doc.actors)) if (a.kind == QLatin1String("character")) ids << a.id;
        for (qint64 id : ids) removeActor(id);
    };
    o[QStringLiteral("group_key")] = [this](const QJsonObject &c) {
        const double t = std::floor(num(c, "t", m_t));
        for (qint64 id : m_multi) if (Actor *a = m_doc.actor(id)) keyTransform(*a, t);
    };
    o[QStringLiteral("group_remove")] = [this](const QJsonObject &) { const auto ids = m_multi; for (qint64 id : ids) removeActor(id); m_multi.clear(); };
    o[QStringLiteral("duplicate_cast")] = [this](const QJsonObject &c) {
        Actor *src = argActor(c);
        if (!src) return;
        const Actor copy = *src;
        QJsonObject extra{{QStringLiteral("id"), copy.castId}, {QStringLiteral("tree"), copy.castTree}, {QStringLiteral("code"), copy.castCode},
                          {QStringLiteral("preset"), copy.preset}, {QStringLiteral("no_idle"), true},
                          {QStringLiteral("pos"), fm::arr(copy.pos + QVector3D(float(num(c, "dx", 0.8)), 0, float(num(c, "dz", 0))))},
                          {QStringLiteral("rot"), fm::arr(copy.rot)}};
        spawnModel(copy.model, copy.displayName + QStringLiteral(" copy"), copy.kind, extra);
        Actor &na = m_doc.actors.last();
        na.idlePath = copy.idlePath;
        na.idleMot = copy.idleMot;
        const QVector3D shift = na.pos - copy.pos;
        QVector<Track> add;
        for (const Track &tr : std::as_const(m_doc.seq.tracks)) {
            if (tr.actor != copy.id) continue;
            Track n = tr;
            n.actor = na.id;
            n.actorName = na.name;
            n.name = tr.name;
            n.name.replace(copy.displayName, na.displayName);
            for (AnimClip &x : n.clips) x.id = newId();
            for (PoseClip &x : n.poses) x.id = newId();
            for (XKey &k : n.keys) k.pos += shift;
            add << n;
        }
        m_doc.seq.tracks << add;
    };

    // ---- animation catalog and previews
    o[QStringLiteral("catalog_search")] = [this](const QJsonObject &c) {
        const QString q = c.value(QStringLiteral("q")).toString().toLower();
        const QString group = c.value(QStringLiteral("group")).toString(), owner = c.value(QStringLiteral("owner")).toString();
        const QStringList words = q.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        QJsonArray out;
        const int limit = c.value(QStringLiteral("limit")).toInt(240);
        for (const QJsonValue &v : std::as_const(m_animCatalog)) {
            QJsonObject e = v.toObject();
            if (!group.isEmpty() && e.value(QStringLiteral("g")).toString() != group) continue;
            if (!owner.isEmpty() && e.value(QStringLiteral("c")).toString() != owner) continue;
            const QString hay = (e.value(QStringLiteral("n")).toString() + QLatin1Char(' ') + e.value(QStringLiteral("p")).toString()).toLower();
            if (!std::all_of(words.cbegin(), words.cend(), [&](const QString &w) { return hay.contains(w); })) continue;
            e.insert(QStringLiteral("bank"), bankFor(e.value(QStringLiteral("p")).toString()));
            out.append(e);
            if (out.size() >= limit) break;
        }
        m_catalogResults = out;
        markData();
    };
    o[QStringLiteral("load_motlist")] = [this](const QJsonObject &c) {
        Actor *a = argActor(c);
        const QString path = c.value(QStringLiteral("path")).toString();
        if (!a || path.isEmpty()) return;
        a->banks.insert(bankFor(path), path);
        ensureAnimations(path);
        markData();
    };
    o[QStringLiteral("play")] = [this](const QJsonObject &c) {
        Actor *a = argActor(c);
        if (!a) return;
        const QString path = pathForBank(c.value(QStringLiteral("bank")).toInt());
        if (path.isEmpty()) return;
        a->livePath = path;
        a->liveMot = c.value(QStringLiteral("mot")).toInt(-1);
        a->liveSpeed = num(c, "speed", 1);
        a->liveStartMs = m_clock.elapsed();
        ensureAnimations(path);
    };
    // hover a clip in the Asset Browser: it plays while the mouse rests on it, then the character goes back
    o[QStringLiteral("play_preview")] = [this](const QJsonObject &c) {
        Actor *a = argActor(c);
        const QString path = pathForBank(c.value(QStringLiteral("bank")).toInt());
        if (!a || path.isEmpty()) return;
        const int mot = c.value(QStringLiteral("mot")).toInt(-1);
        if (m_clipPreviewActor == a->id && m_clipPreviewPath == path && m_clipPreviewMot == mot) return;
        m_clipPreviewActor = a->id;
        m_clipPreviewPath = path;
        m_clipPreviewMot = mot;
        const double speed = num(c, "speed", 1);
        m_clipPreviewSpeed = speed > 0.01 ? speed : 1.0;
        m_clipPreviewStartMs = m_clock.elapsed();
        ensureAnimations(path);
    };
    o[QStringLiteral("play_preview_end")] = [this](const QJsonObject &) { m_clipPreviewActor = 0; m_clipPreviewMot = -1; m_clipPreviewPath.clear(); };
    // stop the clip Play started (the timeline and the idle take over again)
    o[QStringLiteral("play_stop")] = [this](const QJsonObject &c) { if (Actor *a = argActor(c)) { a->liveMot = -1; a->livePath.clear(); } };
    o[QStringLiteral("mesh_search")] = [this](const QJsonObject &c) {
        const QStringList words = c.value(QStringLiteral("q")).toString().toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const QString cat = c.value(QStringLiteral("cat")).toString();
        const int limit = c.value(QStringLiteral("limit")).toInt(120);
        QJsonArray out;
        for (const dir::CatalogEntry &e : std::as_const(m_props)) {
            if (!cat.isEmpty() && e.group != cat) continue;
            const QString title = e.extra.value(QStringLiteral("title")).toString();
            const QString hay = (e.path + QLatin1Char(' ') + e.group + QLatin1Char(' ') + title).toLower();
            if (!std::all_of(words.cbegin(), words.cend(), [&](const QString &w) { return hay.contains(w); })) continue;
            QJsonObject row{{QStringLiteral("p"), e.path}, {QStringLiteral("n"), e.name}, {QStringLiteral("c"), e.group}, {QStringLiteral("id"), qualify(m_catalogGame, e.id)},
                            {QStringLiteral("game"), m_catalogGame}};
            if (!title.isEmpty()) row.insert(QStringLiteral("name"), title);
            out.append(row);
            if (out.size() >= limit) break;
        }
        m_meshResults = out;
        markData();
    };

    // ---- poser (working pose = bones edited but not keyed)
    auto boneRest = [this](const Actor &a, const QString &name) -> std::optional<QQuaternion> {
        const PreparedModel *pm = prepared(a.id);
        if (!pm) return std::nullopt;
        const int b = pm->model->skeleton.find(name);
        if (b < 0) return std::nullopt;
        const Pose p = basePose(a, m_t, false);
        return b < p.size() ? p[b].r : pm->model->skeleton.bones[b].rotation;
    };
    o[QStringLiteral("select_joint")] = [this, boneRest](const QJsonObject &c) {
        Actor *a = argActor(c);
        if (!a) return;
        // choosing a bone does not pose it: it keeps animating until it is turned
        a->workJoint = c.value(QStringLiteral("name")).toString();
        Q_UNUSED(boneRest);
    };
    o[QStringLiteral("set_joint_euler")] = [this](const QJsonObject &c) {
        Actor *a = argActor(c);
        if (!a) return;
        const QString name = c.value(QStringLiteral("name")).toString(a->workJoint);
        if (name.isEmpty()) return;
        const QVector3D e = v3(c.value(QStringLiteral("euler")));
        a->work.insert(name, fm::fromEulerDeg(e.x(), e.y(), e.z()));
        a->workJoint = name;
        if (m_autokey) keyframePose(*a, std::floor(m_t), {{name, a->work.value(name)}});
    };
    o[QStringLiteral("reset_joint")] = [this, boneRest](const QJsonObject &c) {
        Actor *a = argActor(c);
        if (!a) return;
        const QString name = c.value(QStringLiteral("name")).toString(a->workJoint);
        a->work.remove(name);
        if (const auto q = boneRest(*a, name)) a->work.insert(name, *q);
    };
    o[QStringLiteral("remove_joint")] = [this](const QJsonObject &c) {
        Actor *a = argActor(c);
        if (!a) return;
        const QString name = c.value(QStringLiteral("name")).toString(a->workJoint);
        a->work.remove(name);
        if (a->workJoint == name) a->workJoint.clear();
    };
    o[QStringLiteral("mirror_joint")] = [this](const QJsonObject &c) {
        Actor *a = argActor(c);
        if (!a) return;
        const QString name = c.value(QStringLiteral("name")).toString(a->workJoint);
        const QString other = mirrorName(name);
        const auto current = currentLocal(*a, name);
        if (other.isEmpty() || !current) return;
        const QQuaternion q = *current;
        a->work.insert(other, QQuaternion(q.scalar(), q.x(), -q.y(), -q.z()));
    };
    o[QStringLiteral("clear_pose")] = [this](const QJsonObject &c) { if (Actor *a = argActor(c)) { a->work.clear(); a->workJoint.clear(); } };
    o[QStringLiteral("pose_weight")] = [this](const QJsonObject &c) { if (Actor *a = argActor(c)) a->workWeight = std::clamp(num(c, "value", 1), 0.0, 1.0); };
    o[QStringLiteral("keyframe_pose")] = [this](const QJsonObject &c) {
        Actor *a = argActor(c);
        if (!a || a->work.isEmpty()) return;
        if (c.contains(QStringLiteral("t"))) keyframePose(*a, std::floor(num(c, "t")), a->work);
        else commitPoseEdit(*a);
    };
    o[QStringLiteral("load_key")] = [this](const QJsonObject &c) {
        Actor *a = argActor(c);
        if (!a) return;
        const double t = num(c, "t", m_t);
        for (const Track &tr : std::as_const(m_doc.seq.tracks)) {
            if (tr.kind != QLatin1String("pose") || tr.actor != a->id) continue;
            for (const PoseClip &pc : tr.poses)
                for (const PoseKey &k : pc.keys)
                    if (std::abs(pc.start + k.t - t) < 1) a->work = k.joints;
        }
    };
    o[QStringLiteral("capture_pose")] = [this](const QJsonObject &c) {
        Actor *a = argActor(c);
        const PreparedModel *pm = a ? prepared(a->id) : nullptr;
        if (!a || !pm) return;
        const Pose p = basePose(*a, m_t, false);
        const QString filter = c.value(QStringLiteral("filter")).toString(QStringLiteral("all"));
        int n = 0;
        for (int b = 0; b < pm->model->skeleton.bones.size() && b < p.size(); ++b) {
            const QString name = pm->model->skeleton.bones[b].name;
            const bool fingers = name.contains(QLatin1String("Thumb")) || name.contains(QLatin1String("Index")) || name.contains(QLatin1String("Middle"))
                                 || name.contains(QLatin1String("Ring")) || name.contains(QLatin1String("Pinky"));
            if (filter == QLatin1String("fingers") && !fingers) continue;
            if (name.endsWith(QLatin1String("_s")) || name.startsWith(QLatin1String("joint_"))) continue;
            a->work.insert(name, p[b].r);
            ++n;
        }
        log(QStringLiteral("info"), QStringLiteral("captured %1 joints into the working pose of %2").arg(n).arg(a->name));
    };
    o[QStringLiteral("save_pose")] = [this](const QJsonObject &c) {
        Actor *a = argActor(c);
        const QString name = c.value(QStringLiteral("name")).toString();
        if (!a || name.isEmpty()) return;
        QJsonObject j;
        for (auto it = a->work.cbegin(); it != a->work.cend(); ++it) j.insert(it.key(), fm::arr(it.value()));
        QSaveFile f(projectsDir() + QStringLiteral("/../poses/%1.json").arg(name));
        if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(QJsonObject{{QStringLiteral("joints"), j}}).toJson()); f.commit(); }
        markData();
    };
    o[QStringLiteral("load_pose")] = [this](const QJsonObject &c) {
        Actor *a = argActor(c);
        QFile f(projectsDir() + QStringLiteral("/../poses/%1.json").arg(c.value(QStringLiteral("name")).toString()));
        if (!a || !f.open(QIODevice::ReadOnly)) return;
        const QJsonObject j = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("joints")).toObject();
        if (!c.value(QStringLiteral("merge")).toBool()) a->work.clear();
        for (auto it = j.begin(); it != j.end(); ++it) a->work.insert(it.key(), fm::quat(it.value()));
    };

    // ---- lights
    o[QStringLiteral("light_add")] = [this](const QJsonObject &c) {
        Light l;
        l.id = newId();
        l.kind = c.value(QStringLiteral("kind")).toString(QStringLiteral("spot"));
        if (l.kind != QLatin1String("point") && l.kind != QLatin1String("spot") && l.kind != QLatin1String("sun") && l.kind != QLatin1String("area")) l.kind = QStringLiteral("point");
        static const QHash<QString, QString> label = {{QStringLiteral("point"), QStringLiteral("Light")}, {QStringLiteral("spot"), QStringLiteral("Spot")},
                                                      {QStringLiteral("area"), QStringLiteral("Area")}, {QStringLiteral("sun"), QStringLiteral("Sun")}};
        l.name = c.value(QStringLiteral("name")).toString(QStringLiteral("%1 %2").arg(label.value(l.kind), QString::number(m_doc.lights.size() + 1)));
        l.intensity = l.kind == QLatin1String("spot") ? 3000 : l.kind == QLatin1String("sun") ? 6 : 1500;
        const ViewFrame v = currentView();
        Actor *a = selectedActor();
        if (a && c.value(QStringLiteral("above")).toBool(true)) {
            QVector3D p = a->pos;
            for (const ActorFrame &f : std::as_const(m_frames)) if (f.id == a->id) p = f.pos;
            const QVector3D fwd = fm::forward(v.rot);
            l.pos = QVector3D(p.x() - fwd.x() * 1.2f, p.y() + 2.2f, p.z() - fwd.z() * 1.2f);
            l.rot = fm::lookRotation(l.pos, p + QVector3D(0, 1.3f, 0));
        } else {
            l.pos = v.pos + fm::forward(v.rot) * 1.5f;
            l.rot = v.rot;
        }
        m_doc.lights << l;
        m_selLight = l.id;
    };
    o[QStringLiteral("light_update")] = [this](const QJsonObject &c) {
        Light *l = m_doc.light(c.value(QStringLiteral("id")).toInt(m_selLight));
        if (!l) return;
        const QJsonObject f = c.value(QStringLiteral("fields")).toObject();
        for (auto it = f.begin(); it != f.end(); ++it) {
            const QString k = it.key();
            const QJsonValue v = it.value();
            if (k == QLatin1String("pos")) l->pos = v3(v);
            else if (k == QLatin1String("euler")) { const QVector3D e = v3(v); l->rot = fm::fromEulerDeg(e.x(), e.y(), e.z()); }
            else if (k == QLatin1String("name")) l->name = v.toString();
            else if (k == QLatin1String("color")) l->color = v3(v, QVector3D(1, 1, 1));
            else if (k == QLatin1String("intensity")) l->intensity = v.toDouble();
            else if (k == QLatin1String("radius")) l->radius = v.toDouble();
            else if (k == QLatin1String("cone")) l->cone = v.toDouble();
            else if (k == QLatin1String("spread")) l->spread = v.toDouble();
            else if (k == QLatin1String("shadows")) l->shadows = v.toBool();
            else if (k == QLatin1String("enabled")) l->enabled = v.toBool();
            else if (k == QLatin1String("temperature")) l->temperature = v.toDouble();
            else if (k == QLatin1String("blackbody")) l->blackbody = v.toBool();
            else if (k == QLatin1String("size")) l->size = v.toDouble();
            else if (k == QLatin1String("specular")) l->specular = v.toDouble();
        }
    };
    o[QStringLiteral("light_remove")] = [this](const QJsonObject &c) {
        const int id = c.value(QStringLiteral("id")).toInt(m_selLight);
        m_doc.lights.erase(std::remove_if(m_doc.lights.begin(), m_doc.lights.end(), [id](const Light &l) { return l.id == id; }), m_doc.lights.end());
        const QString target = QStringLiteral("light:%1").arg(id);
        m_doc.seq.tracks.erase(std::remove_if(m_doc.seq.tracks.begin(), m_doc.seq.tracks.end(), [&](const Track &t) { return t.kind == QLatin1String("channel") && t.target == target; }),
                               m_doc.seq.tracks.end());
        if (m_selLight == id) m_selLight = 0;
    };
    o[QStringLiteral("light_select")] = [this](const QJsonObject &c) { m_selLight = c.value(QStringLiteral("id")).toInt(); };
    o[QStringLiteral("light_aim")] = [this](const QJsonObject &c) {
        Light *l = m_doc.light(c.value(QStringLiteral("id")).toInt(m_selLight));
        Actor *a = argActor(c);
        if (!l || !a) return;
        l->rot = fm::lookRotation(l->pos, a->pos + QVector3D(0, float(num(c, "height", 1.3)), 0));
    };
    o[QStringLiteral("light_to_camera")] = [this](const QJsonObject &c) {
        Light *l = m_doc.light(c.value(QStringLiteral("id")).toInt(m_selLight));
        if (!l) return;
        const ViewFrame v = currentView();
        l->pos = v.pos;
        l->rot = v.rot;
    };

    // ---- editor switches
    o[QStringLiteral("autokey")] = [this](const QJsonObject &c) { m_autokey = c.value(QStringLiteral("value")).toBool(); };
    o[QStringLiteral("motion_edit")] = [this](const QJsonObject &c) {
        if (c.contains(QStringLiteral("value"))) m_motionEdit = c.value(QStringLiteral("value")).toBool();
        if (c.contains(QStringLiteral("relative"))) m_relativeEdits = c.value(QStringLiteral("relative")).toBool();
    };
    o[QStringLiteral("overlay_skeleton")] = [this](const QJsonObject &c) { m_skeleton = c.value(QStringLiteral("value")).toBool(); };
    o[QStringLiteral("overlay_onion")] = [this](const QJsonObject &c) { m_onion = c.value(QStringLiteral("value")).toBool(); };
    o[QStringLiteral("show_cameras")] = [this](const QJsonObject &c) { m_showCameras = c.value(QStringLiteral("value")).toBool(true); };
    o[QStringLiteral("gizmo")] = [this](const QJsonObject &c) {
        if (c.contains(QStringLiteral("enabled"))) m_gizmo.insert(QStringLiteral("enabled"), c.value(QStringLiteral("enabled")).toBool());
        if (c.contains(QStringLiteral("target"))) m_gizmo.insert(QStringLiteral("target"), c.value(QStringLiteral("target")));
        const QString tool = c.value(QStringLiteral("tool")).toString();
        if (tool == QLatin1String("move") || tool == QLatin1String("rotate") || tool == QLatin1String("scale")) m_gizmo.insert(QStringLiteral("op"), tool);
        if (c.contains(QStringLiteral("mode"))) m_gizmo.insert(QStringLiteral("mode"), c.value(QStringLiteral("mode")));
        if (c.contains(QStringLiteral("snap"))) m_gizmo.insert(QStringLiteral("snap"), c.value(QStringLiteral("snap")));
    };
    o[QStringLiteral("undo")] = [this](const QJsonObject &) { undo(); };
    o[QStringLiteral("redo")] = [this](const QJsonObject &) { redo(); };
    o[QStringLiteral("eval")] = [this](const QJsonObject &c) {
        m_eval = {{QStringLiteral("id"), c.value(QStringLiteral("id"))}, {QStringLiteral("ok"), false},
                  {QStringLiteral("error"), QStringLiteral("the Lua console needs the game running (Game > Connect to the running game)")}};
        markData();
    };

    // ---- projects (the film is the Studio's own file)
    o[QStringLiteral("project_save")] = [this](const QJsonObject &c) {
        QString name = c.value(QStringLiteral("name")).toString(m_project);
        if (name.isEmpty()) name = QStringLiteral("untitled");
        if (m_doc.gameKey.isEmpty()) m_doc.gameKey = browseGame();
        QSaveFile f(projectsDir() + QLatin1Char('/') + name + QStringLiteral(".film.json"));
        if (f.open(QIODevice::WriteOnly) && f.write(QJsonDocument(toJson(m_doc)).toJson(QJsonDocument::Indented)) > 0 && f.commit()) {
            m_project = name;
            dropAutosave();
            log(QStringLiteral("info"), QStringLiteral("saved %1").arg(f.fileName()));
        } else log(QStringLiteral("error"), QStringLiteral("could not save %1").arg(name));
        markData();
    };
    o[QStringLiteral("project_load")] = [this](const QJsonObject &c) {
        const QString name = c.value(QStringLiteral("name")).toString();
        QFile f(name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')) ? name : projectsDir() + QLatin1Char('/') + name + QStringLiteral(".film.json"));
        if (!f.open(QIODevice::ReadOnly)) { log(QStringLiteral("error"), QStringLiteral("no film called %1").arg(name)); return; }
        pushUndo(QStringLiteral("open ") + name);
        restore(QJsonDocument::fromJson(f.readAll()).object());
        m_project = QFileInfo(f.fileName()).fileName().remove(QStringLiteral(".film.json"));
        m_t = 0;
        m_playing = false;
        m_selActor = 0;
        m_selCamera = 0;
        setActiveCamera(0);
        log(QStringLiteral("info"), QStringLiteral("opened %1").arg(m_project));
    };
    o[QStringLiteral("project_new")] = [this](const QJsonObject &c) {
        pushUndo(QStringLiteral("new film"));
        for (const Actor &a : std::as_const(m_doc.actors)) if (m_scene) m_scene->removeActor(a.id);
        m_models.clear();
        m_bindings.clear();
        const Camera work = m_doc.work;
        m_doc = Document();
        m_doc.work = work;
        m_doc.hasWork = true;
        m_doc.gameKey = browseGame();
        m_project = c.value(QStringLiteral("name")).toString().isEmpty() ? QStringLiteral("untitled") : c.value(QStringLiteral("name")).toString();
        m_t = 0;
        m_playing = false;
        m_selActor = 0;
        m_selCamera = 0;
        m_selLight = 0;
        m_selClip = {};
        m_multi.clear();
        setActiveCamera(0);
        markData();
    };
    registerRigOps();
    registerChannelOps();
    registerAutosaveOps();
    // the world: sun, sky and the map's lighting {fields: {sun, sun_strength, sun_yaw, sun_elevation, sun_temp, sky, lighting, map_lights}}
    o[QStringLiteral("world_set")] = [this](const QJsonObject &c) {
        const QJsonObject f = c.value(QStringLiteral("fields")).toObject();
        QJsonObject cur = worldJson(m_doc.world);
        for (auto it = f.begin(); it != f.end(); ++it) cur.insert(it.key(), it.value());
        m_doc.world = worldFrom(cur);
    };
}
