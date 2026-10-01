// Drive & Record in the standalone runtime (drive.lua): possess a character and walk it with WASD relative to the
// view (Shift jogs); it turns toward where it walks and plays its own stand / walk / jog loops. R records: the
// timeline runs and receives position keys plus the locomotion clips as they change. Esc stops.
#include "Maths.h"
#include "StageRuntime.h"

#include <cmath>

using namespace film;

namespace {
// the first clip whose name holds one of the patterns (in order), skipping names with an excluded word
int pickClip(const dir::AnimationSet &set, const QStringList &patterns, const QStringList &exclude)
{
    for (const QString &pat : patterns)
        for (const dir::AnimationClip &c : set.clips) {
            const QString n = c.name.toLower();
            if (!n.contains(pat)) continue;
            if (std::any_of(exclude.cbegin(), exclude.cend(), [&](const QString &x) { return n.contains(x); })) continue;
            return c.id >= 0 ? c.id : int(&c - set.clips.constData());
        }
    return -1;
}
} // namespace

void StageRuntime::driveStart(qint64 actorId)
{
    Actor *a = m_doc.actor(actorId);
    if (!a) return;
    if (m_drive.on) driveStop();
    const QString list = generalFor(*a, true);
    if (list.isEmpty()) { log(QStringLiteral("warn"), QStringLiteral("drive: no walking clips for %1").arg(a->displayName.isEmpty() ? a->name : a->displayName)); return; }
    m_drive = {};
    m_drive.on = true;
    m_drive.actor = actorId;
    m_drive.list = list;
    m_driveIn = {};
    // Drive is a take of its own: the character starts from where it is shown now
    if (const ActorFrame *f = frameOf(actorId)) { a->pos = f->pos; a->rot = f->rot; }
    ensureAnimations(list, [this, actorId, list]() {
        const dir::AnimationSetPtr set = animations(list);
        if (!set || !m_drive.on || m_drive.actor != actorId) return;
        m_drive.idle = pickClip(*set, {QStringLiteral("stand_loop"), QStringLiteral("idle_loop"), QStringLiteral("wait_loop"), QStringLiteral("stand"), QStringLiteral("idle")},
                                {QStringLiteral("hide"), QStringLiteral("crouch")});
        m_drive.walk = pickClip(*set, {QStringLiteral("walk_f_loop"), QStringLiteral("walk_front_loop"), QStringLiteral("walk_loop")},
                                {QStringLiteral("stairs"), QStringLiteral("hide"), QStringLiteral("crouch"), QStringLiteral("add"), QStringLiteral("diverse"), QStringLiteral("curve")});
        m_drive.jog = pickClip(*set, {QStringLiteral("jog_loop_vera"), QStringLiteral("jog_loop"), QStringLiteral("run_loop"), QStringLiteral("dash_loop"), QStringLiteral("jog_f_loop"),
                                      QStringLiteral("jog_straight_loop")},
                               {QStringLiteral("stairs"), QStringLiteral("hide"), QStringLiteral("crouch"), QStringLiteral("add"), QStringLiteral("curve")});
        if (m_drive.jog < 0) m_drive.jog = m_drive.walk;
        // ground speed of each loop from its root travel, so the feet do not skate
        auto speedOf = [&](int mot, double fallback) {
            const ClipBinding *b = mot >= 0 ? binding(actorId, list, mot) : nullptr;
            if (!b || b->frames() <= 1) return fallback;
            QVector3D d = b->rootAt(b->frames()).t - b->rootAt(0).t;
            d.setY(0);
            const double v = d.length() / (b->frames() / 60.0);
            return v > 0.3 ? v : fallback;
        };
        m_drive.walkSpeed = speedOf(m_drive.walk, 1.35);
        m_drive.jogSpeed = speedOf(m_drive.jog, 3.1);
        m_drive.state.clear();                                // play the first loop on the next step
        markData();
    });
}

void StageRuntime::driveStop()
{
    if (!m_drive.on) return;
    if (m_drive.recording) driveRecord(false);
    if (Actor *a = m_doc.actor(m_drive.actor)) { a->liveMot = -1; a->livePath.clear(); }
    m_drive = {};
    m_driveIn = {};
}

void StageRuntime::driveRecord(bool on)
{
    if (!m_drive.on || on == m_drive.recording) return;
    Actor *a = m_doc.actor(m_drive.actor);
    if (!a) return;
    m_drive.recording = on;
    if (on) {
        pushUndo(QStringLiteral("record"));                   // the whole take undoes in one step
        dropUnkeyedEdits();
        m_playing = true;
        m_drive.lastKey = -99;
        keyTransform(*a, std::floor(m_t), &a->pos, &a->rot);
        driveOpenClip();
        log(QStringLiteral("info"), QStringLiteral("recording %1 from frame %2").arg(a->displayName.isEmpty() ? a->name : a->displayName).arg(int(m_t)));
    } else {
        driveCloseClip();
        keyTransform(*a, std::floor(m_t), &a->pos, &a->rot);
        m_playing = false;
        log(QStringLiteral("info"), QStringLiteral("recording stopped at frame %1").arg(int(m_t)));
    }
    markData();
}

// the locomotion clip of the current state starts on layer 0 at the playhead
void StageRuntime::driveOpenClip()
{
    driveCloseClip();
    const int mot = m_drive.state == QLatin1String("jog") ? m_drive.jog : m_drive.state == QLatin1String("walk") ? m_drive.walk : m_drive.idle;
    const dir::AnimationSetPtr set = animations(m_drive.list);
    if (mot < 0 || !set) return;
    const dir::AnimationClip *src = nullptr;
    for (const dir::AnimationClip &c : set->clips) if (c.id == mot) src = &c;
    Track &tr = track(QStringLiteral("anim"), m_drive.actor, 0);
    AnimClip clip;
    clip.id = newId();
    clip.start = std::floor(m_t);
    clip.dur = 1;
    clip.path = m_drive.list;
    clip.mot = mot;
    clip.name = src ? src->name : QString();
    clip.endframe = src ? src->frames : 0;
    clip.blend = 12;
    clip.loop = true;
    // a clip already under the playhead ends here
    for (AnimClip &c : tr.clips) if (c.start < clip.start && c.start + c.dur > clip.start) c.dur = std::max(1.0, clip.start - c.start);
    tr.clips << clip;
    std::stable_sort(tr.clips.begin(), tr.clips.end(), [](const AnimClip &x, const AnimClip &y) { return x.start < y.start; });
    m_drive.recClip = clip.id;
}

void StageRuntime::driveCloseClip()
{
    if (!m_drive.recClip) return;
    const double t = std::floor(m_t);
    const int id = m_drive.recClip;
    for (Track &tr : m_doc.seq.tracks) {
        if (tr.kind != QLatin1String("anim") || tr.actor != m_drive.actor) continue;
        for (AnimClip &c : tr.clips) if (c.id == id) c.dur = std::max(1.0, t - c.start);
        // a state left on the frame it began (walking off at once) leaves no clip behind
        tr.clips.removeIf([id, t](const AnimClip &c) { return c.id == id && c.start >= t; });
    }
    m_drive.recClip = 0;
}

void StageRuntime::driveStep(double dt)
{
    Actor *a = m_doc.actor(m_drive.actor);
    if (!a) { driveStop(); return; }
    // WASD relative to the view, on the ground
    const ViewFrame v = currentView();
    QVector3D f = fm::forward(v.rot), r = fm::right(v.rot);
    f.setY(0);
    r.setY(0);
    if (f.length() > 0.01f) f.normalize();
    if (r.length() > 0.01f) r.normalize();
    QVector3D d = f * m_driveIn.forward + r * m_driveIn.right;
    const bool moving = d.length() > 0.01f;
    const QString state = moving ? (m_driveIn.fast ? QStringLiteral("jog") : QStringLiteral("walk")) : QStringLiteral("idle");
    const int mot = state == QLatin1String("jog") ? m_drive.jog : state == QLatin1String("walk") ? m_drive.walk : m_drive.idle;
    if (state != m_drive.state && mot >= 0) {
        m_drive.state = state;
        a->livePath = m_drive.list;
        a->liveMot = mot;
        a->liveSpeed = 1;
        a->livePaused = false;
        a->liveStartMs = m_clock.elapsed();
        if (m_drive.recording) driveOpenClip();
    }
    if (moving) {
        d.normalize();
        // turn toward the direction of travel (540 degrees a second), then walk forward at the loop's own speed
        const double want = std::atan2(d.x(), d.z());
        const QVector3D fz = a->rot.rotatedVector(QVector3D(0, 0, 1));
        const double have = std::atan2(fz.x(), fz.z());
        const double diff = std::remainder(want - have, 2 * fm::kPi);
        const double step = 540.0 * fm::kPi / 180.0 * dt;
        const double yaw = have + std::clamp(diff, -step, step);
        a->rot = fm::fromEulerDeg(0, yaw * 180 / fm::kPi, 0);
        const double speed = state == QLatin1String("jog") ? m_drive.jogSpeed : m_drive.walkSpeed;
        QVector3D p = a->pos + d * float(speed * dt);
        float gy = 0;
        if (groundBelow(p + QVector3D(0, 1.0f, 0), 2.5f, &gy) && std::abs(gy - p.y()) < 1.2f) p.setY(gy);
        a->pos = p;
    }
    if (m_drive.recording) {
        if (m_t > m_doc.seq.length - 120) m_doc.seq.length += 600;   // keep the tape rolling
        const double t = std::floor(m_t);
        if (t - m_drive.lastKey >= 4) { keyTransform(*a, t, &a->pos, &a->rot); m_drive.lastKey = t; }
        for (Track &tr : m_doc.seq.tracks)
            if (tr.kind == QLatin1String("anim") && tr.actor == m_drive.actor)
                for (AnimClip &c : tr.clips) if (c.id == m_drive.recClip) c.dur = std::max(1.0, t - c.start + 1);
    }
}

QJsonObject StageRuntime::driveState() const
{
    const Actor *a = m_doc.actor(m_drive.actor);
    QJsonObject o{{QStringLiteral("on"), m_drive.on}, {QStringLiteral("recording"), m_drive.recording}, {QStringLiteral("state"), m_drive.state}};
    if (a) {
        o.insert(QStringLiteral("actor"), double(a->id));
        o.insert(QStringLiteral("name"), a->displayName.isEmpty() ? a->name : a->displayName);
    }
    return o;
}

void StageRuntime::registerDriveOps()
{
    auto &o = m_ops;
    m_readonly << QStringLiteral("drive") << QStringLiteral("drive_input");
    // the held keys, for automation (the viewport feeds them through StageScene::drive)
    o[QStringLiteral("drive_input")] = [this](const QJsonObject &c) {
        setDriveInput(float(c.value(QStringLiteral("forward")).toDouble()), float(c.value(QStringLiteral("right")).toDouble()), c.value(QStringLiteral("fast")).toBool());
    };
    o[QStringLiteral("drive")] = [this](const QJsonObject &c) {
        if (c.value(QStringLiteral("value")).toBool(true)) {
            const Actor *a = argActor(c);
            if (a) driveStart(a->id);
        } else driveStop();
    };
    o[QStringLiteral("record")] = [this](const QJsonObject &c) {
        if (!m_drive.on) { log(QStringLiteral("warn"), QStringLiteral("record: drive a character first")); return; }
        driveRecord(c.value(QStringLiteral("value")).toBool(!m_drive.recording));
    };
}
