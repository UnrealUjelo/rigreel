#include "Film.h"
#include "Maths.h"

#include <algorithm>

namespace film {

using fm::arr;

Actor *Document::actor(qint64 id)
{
    for (Actor &a : actors) if (a.id == id) return &a;
    return nullptr;
}
const Actor *Document::actor(qint64 id) const
{
    for (const Actor &a : actors) if (a.id == id) return &a;
    return nullptr;
}
Actor *Document::actorByName(const QString &name)
{
    for (Actor &a : actors) if (a.name == name) return &a;
    return nullptr;
}
Light *Document::light(int id)
{
    for (Light &l : lights) if (l.id == id) return &l;
    return nullptr;
}
Constraint *Document::constraint(int id)
{
    for (Constraint &c : constraints) if (c.id == id) return &c;
    return nullptr;
}

// ------------------------------------------------------------------ JSON pieces
namespace {
QJsonValue opt(const std::optional<double> &v) { return v ? QJsonValue(*v) : QJsonValue(); }
std::optional<double> opt(const QJsonValue &v) { return v.isDouble() ? std::optional<double>(v.toDouble()) : std::nullopt; }

QJsonObject keyJson(const XKey &k, bool cam)
{
    QJsonObject o{{QStringLiteral("t"), k.t}, {QStringLiteral("pos"), arr(k.pos)}, {QStringLiteral("rot"), arr(k.rot)}};
    if (!k.ease.isEmpty()) o.insert(QStringLiteral("ease"), k.ease);
    if (cam) {
        o.insert(QStringLiteral("fov"), k.fov);
        o.insert(QStringLiteral("roll"), k.roll);
        if (k.dofF) o.insert(QStringLiteral("dof_f"), *k.dofF);
        if (k.dofFocus) o.insert(QStringLiteral("dof_focus"), *k.dofFocus);
    }
    return o;
}
XKey keyFrom(const QJsonObject &o)
{
    XKey k;
    k.t = o.value(QStringLiteral("t")).toDouble();
    k.pos = fm::vec3(o.value(QStringLiteral("pos")));
    k.rot = fm::quat(o.value(QStringLiteral("rot")));
    k.fov = o.value(QStringLiteral("fov")).toDouble();
    k.roll = o.value(QStringLiteral("roll")).toDouble();
    k.dofF = opt(o.value(QStringLiteral("dof_f")));
    k.dofFocus = opt(o.value(QStringLiteral("dof_focus")));
    k.v = o.value(QStringLiteral("v")).toDouble();
    k.ease = o.value(QStringLiteral("ease")).toString();
    if (k.ease == QLatin1String("smooth")) k.ease.clear();
    return k;
}
QJsonObject jointsJson(const QMap<QString, QQuaternion> &j)
{
    QJsonObject o;
    for (auto it = j.cbegin(); it != j.cend(); ++it) o.insert(it.key(), arr(it.value()));
    return o;
}
QMap<QString, QQuaternion> jointsFrom(const QJsonObject &o)
{
    QMap<QString, QQuaternion> j;
    for (auto it = o.begin(); it != o.end(); ++it) j.insert(it.key(), fm::quat(it.value()));
    return j;
}

QJsonObject cameraJson(const Camera &c)
{
    QJsonObject o{{QStringLiteral("name"), c.name}, {QStringLiteral("mode"), c.mode}, {QStringLiteral("pos"), arr(c.pos)}, {QStringLiteral("rot"), arr(c.rot)},
                  {QStringLiteral("fov"), c.fov}, {QStringLiteral("roll"), c.roll}, {QStringLiteral("dof"), c.dof}, {QStringLiteral("dof_f"), opt(c.dofF)},
                  {QStringLiteral("dof_focus"), opt(c.dofFocus)}};
    if (c.target) o.insert(QStringLiteral("target"), double(c.target));
    if (!c.shake.isEmpty()) o.insert(QStringLiteral("shake"), c.shake);
    return o;
}
Camera cameraFrom(const QJsonObject &o)
{
    Camera c;
    c.name = o.value(QStringLiteral("name")).toString();
    c.mode = o.value(QStringLiteral("mode")).toString(QStringLiteral("static"));
    c.pos = fm::vec3(o.value(QStringLiteral("pos")));
    c.rot = fm::quat(o.value(QStringLiteral("rot")));
    c.fov = o.value(QStringLiteral("fov")).toDouble(50);
    c.roll = o.value(QStringLiteral("roll")).toDouble();
    c.dof = o.value(QStringLiteral("dof")).toBool(true);
    c.dofF = opt(o.value(QStringLiteral("dof_f")));
    c.dofFocus = opt(o.value(QStringLiteral("dof_focus")));
    c.target = qint64(o.value(QStringLiteral("target")).toDouble());
    c.shake = o.value(QStringLiteral("shake")).toObject();
    return c;
}

QJsonObject lightJson(const Light &l)
{
    return {{QStringLiteral("id"), l.id}, {QStringLiteral("kind"), l.kind}, {QStringLiteral("name"), l.name}, {QStringLiteral("pos"), arr(l.pos)},
            {QStringLiteral("euler"), arr(fm::toEulerUpright(l.rot))}, {QStringLiteral("rot"), arr(l.rot)}, {QStringLiteral("intensity"), l.intensity},
            {QStringLiteral("color"), arr(l.color)}, {QStringLiteral("radius"), l.radius}, {QStringLiteral("cone"), l.cone}, {QStringLiteral("spread"), l.spread},
            {QStringLiteral("shadows"), l.shadows}, {QStringLiteral("enabled"), l.enabled}, {QStringLiteral("temperature"), l.temperature},
            {QStringLiteral("blackbody"), l.blackbody}, {QStringLiteral("bounce"), l.bounce}, {QStringLiteral("volumetric"), l.volumetric},
            {QStringLiteral("specular"), l.specular}, {QStringLiteral("size"), l.size}, {QStringLiteral("shadow_bias"), 0}, {QStringLiteral("shadow_soft"), 0}};
}
Light lightFrom(const QJsonObject &o)
{
    Light l;
    l.id = o.value(QStringLiteral("id")).toInt();
    l.kind = o.value(QStringLiteral("kind")).toString(QStringLiteral("point"));
    l.name = o.value(QStringLiteral("name")).toString();
    l.pos = fm::vec3(o.value(QStringLiteral("pos")));
    l.rot = o.contains(QStringLiteral("rot")) ? fm::quat(o.value(QStringLiteral("rot")))
                                               : [&] { const QVector3D e = fm::vec3(o.value(QStringLiteral("euler"))); return fm::fromEulerDeg(e.x(), e.y(), e.z()); }();
    l.intensity = o.value(QStringLiteral("intensity")).toDouble(1500);
    l.color = fm::vec3(o.value(QStringLiteral("color")), QVector3D(1, 0.96f, 0.9f));
    l.radius = o.value(QStringLiteral("radius")).toDouble(8);
    l.cone = o.value(QStringLiteral("cone")).toDouble(45);
    l.spread = o.value(QStringLiteral("spread")).toDouble(0.5);
    l.shadows = o.value(QStringLiteral("shadows")).toBool(true);
    l.enabled = o.value(QStringLiteral("enabled")).toBool(true);
    l.temperature = o.value(QStringLiteral("temperature")).toDouble(6500);
    l.blackbody = o.value(QStringLiteral("blackbody")).toBool();
    l.bounce = o.value(QStringLiteral("bounce")).toDouble(1);
    l.volumetric = o.value(QStringLiteral("volumetric")).toDouble(1);
    l.specular = o.value(QStringLiteral("specular")).toDouble(1);
    l.size = o.value(QStringLiteral("size")).toDouble(0.25);
    return l;
}
QJsonObject lookatJson(const LookAt &l)
{
    QJsonObject o{{QStringLiteral("enabled"), l.enabled}, {QStringLiteral("kind"), l.kind}, {QStringLiteral("weight"), l.weight},
                  {QStringLiteral("max_deg"), l.maxDeg}, {QStringLiteral("smooth"), l.smooth}, {QStringLiteral("neck_share"), l.neckShare},
                  {QStringLiteral("flip"), l.flip}};
    if (l.target) o.insert(QStringLiteral("target"), double(l.target));
    if (l.kind == QLatin1String("point")) o.insert(QStringLiteral("point"), arr(l.point));
    return o;
}
LookAt lookatFrom(const QJsonObject &o)
{
    LookAt l;
    l.enabled = o.value(QStringLiteral("enabled")).toBool();
    l.kind = o.value(QStringLiteral("kind")).toString(QStringLiteral("camera"));
    l.target = qint64(o.value(QStringLiteral("target")).toDouble());
    l.point = fm::vec3(o.value(QStringLiteral("point")));
    l.weight = o.value(QStringLiteral("weight")).toDouble(1);
    l.maxDeg = o.value(QStringLiteral("max_deg")).toDouble(75);
    l.smooth = o.value(QStringLiteral("smooth")).toDouble(0.12);
    l.neckShare = o.value(QStringLiteral("neck_share")).toDouble(0.35);
    l.flip = o.value(QStringLiteral("flip")).toBool();
    return l;
}

QJsonObject constraintJson(const Constraint &c)
{
    QJsonObject o{{QStringLiteral("id"), c.id}, {QStringLiteral("kind"), c.kind}, {QStringLiteral("actor"), double(c.actor)}, {QStringLiteral("chain"), c.chain},
                  {QStringLiteral("offset"), arr(c.offset)}, {QStringLiteral("weight"), c.weight}, {QStringLiteral("enabled"), c.enabled},
                  {QStringLiteral("name"), c.name}};
    if (c.targetActor) o.insert(QStringLiteral("target_actor"), double(c.targetActor));
    if (!c.targetJoint.isEmpty()) o.insert(QStringLiteral("target_joint"), c.targetJoint);
    if (c.point) o.insert(QStringLiteral("point"), arr(*c.point));
    if (c.range) o.insert(QStringLiteral("range"), QJsonArray{c.range->first, c.range->second});
    return o;
}
Constraint constraintFrom(const QJsonObject &o)
{
    Constraint c;
    c.id = o.value(QStringLiteral("id")).toInt();
    c.kind = o.value(QStringLiteral("kind")).toString(QStringLiteral("ik_pin"));
    c.actor = qint64(o.value(QStringLiteral("actor")).toDouble());
    c.chain = o.value(QStringLiteral("chain")).toString(QStringLiteral("R_Hand"));
    c.targetActor = qint64(o.value(QStringLiteral("target_actor")).toDouble());
    c.targetJoint = o.value(QStringLiteral("target_joint")).toString();
    if (o.contains(QStringLiteral("point"))) c.point = fm::vec3(o.value(QStringLiteral("point")));
    c.offset = fm::vec3(o.value(QStringLiteral("offset")));
    c.weight = o.value(QStringLiteral("weight")).toDouble(1);
    if (const QJsonArray r = o.value(QStringLiteral("range")).toArray(); r.size() == 2) c.range = std::make_pair(r[0].toDouble(), r[1].toDouble());
    c.enabled = o.value(QStringLiteral("enabled")).toBool(true);
    c.name = o.value(QStringLiteral("name")).toString();
    return c;
}

QJsonObject pathJson(const PathSpec &p)
{
    QJsonArray pts;
    for (const QVector3D &v : p.points) pts.append(arr(v));
    return {{QStringLiteral("points"), pts}, {QStringLiteral("start"), p.start}, {QStringLiteral("dur"), p.dur}, {QStringLiteral("speed"), p.speed},
            {QStringLiteral("ease"), p.ease}, {QStringLiteral("gait"), p.gait}, {QStringLiteral("clip_id"), p.clipId}, {QStringLiteral("clip_name"), p.clipName},
            {QStringLiteral("clip_speed_ref"), p.clipSpeedRef}, {QStringLiteral("dur_pinned"), p.durPinned}, {QStringLiteral("speed_pinned"), p.speedPinned}};
}
PathSpec pathFrom(const QJsonObject &o)
{
    PathSpec p;
    for (const QJsonValue &v : o.value(QStringLiteral("points")).toArray()) p.points << fm::vec3(v);
    p.start = o.value(QStringLiteral("start")).toDouble();
    p.dur = std::max(1.0, o.value(QStringLiteral("dur")).toDouble(60));
    p.speed = o.value(QStringLiteral("speed")).toDouble(1.35);
    p.ease = o.value(QStringLiteral("ease")).toDouble(0.08);
    p.gait = o.value(QStringLiteral("gait")).toString(QStringLiteral("walk"));
    p.clipId = o.value(QStringLiteral("clip_id")).toInt();
    p.clipName = o.value(QStringLiteral("clip_name")).toString();
    p.clipSpeedRef = o.value(QStringLiteral("clip_speed_ref")).toDouble();
    p.durPinned = o.value(QStringLiteral("dur_pinned")).toBool();
    p.speedPinned = o.value(QStringLiteral("speed_pinned")).toBool();
    return p;
}
} // namespace

QJsonObject worldJson(const World &w)
{
    return {{QStringLiteral("sun"), w.sun}, {QStringLiteral("sun_strength"), w.sunStrength}, {QStringLiteral("sun_yaw"), w.sunYaw},
            {QStringLiteral("sun_elevation"), w.sunElevation}, {QStringLiteral("sun_temp"), w.sunTemp}, {QStringLiteral("sky"), w.sky},
            {QStringLiteral("lighting"), w.lighting}, {QStringLiteral("map_lights"), w.mapLights},
            {QStringLiteral("exposure"), w.exposure}, {QStringLiteral("contrast"), w.contrast}, {QStringLiteral("saturation"), w.saturation},
            {QStringLiteral("brightness"), w.brightness}, {QStringLiteral("bloom"), w.bloom}, {QStringLiteral("vignette"), w.vignette},
            {QStringLiteral("sharpen"), w.sharpen}, {QStringLiteral("tonemap"), w.tonemap}};
}

World worldFrom(const QJsonObject &o)
{
    World w;
    w.sun = o.value(QStringLiteral("sun")).toString(w.sun);
    w.sunStrength = o.value(QStringLiteral("sun_strength")).toDouble(w.sunStrength);
    w.sunYaw = o.value(QStringLiteral("sun_yaw")).toDouble(w.sunYaw);
    w.sunElevation = o.value(QStringLiteral("sun_elevation")).toDouble(w.sunElevation);
    w.sunTemp = o.value(QStringLiteral("sun_temp")).toDouble(w.sunTemp);
    w.sky = o.value(QStringLiteral("sky")).toDouble(w.sky);
    w.lighting = o.value(QStringLiteral("lighting")).toString();
    w.mapLights = o.value(QStringLiteral("map_lights")).toDouble(w.mapLights);
    w.exposure = o.value(QStringLiteral("exposure")).toDouble(w.exposure);
    w.contrast = o.value(QStringLiteral("contrast")).toDouble(w.contrast);
    w.saturation = o.value(QStringLiteral("saturation")).toDouble(w.saturation);
    w.brightness = o.value(QStringLiteral("brightness")).toDouble(w.brightness);
    w.bloom = o.value(QStringLiteral("bloom")).toDouble(w.bloom);
    w.vignette = o.value(QStringLiteral("vignette")).toDouble(w.vignette);
    w.sharpen = o.value(QStringLiteral("sharpen")).toDouble(w.sharpen);
    w.tonemap = o.value(QStringLiteral("tonemap")).toString(w.tonemap);
    return w;
}

QJsonObject toJson(const Document &d)
{
    QJsonArray actors, cams, lights, tracks, shots, constraints;
    for (const Actor &a : d.actors) {
        QJsonObject ao{
            {QStringLiteral("id"), double(a.id)}, {QStringLiteral("name"), a.name}, {QStringLiteral("display_name"), a.displayName},
            {QStringLiteral("kind"), a.kind}, {QStringLiteral("model"), a.model}, {QStringLiteral("cast_id"), a.castId}, {QStringLiteral("physics"), a.physics},
            {QStringLiteral("cast_tree"), a.castTree}, {QStringLiteral("cast_code"), a.castCode}, {QStringLiteral("preset"), a.preset},
            {QStringLiteral("pos"), arr(a.pos)}, {QStringLiteral("rot"), arr(a.rot)}, {QStringLiteral("scale"), arr(a.scale)},
            {QStringLiteral("hidden"), a.hidden}, {QStringLiteral("root_lock"), a.rootLock}, {QStringLiteral("idle_path"), a.idlePath}, {QStringLiteral("idle_mot"), a.idleMot},
            {QStringLiteral("work"), jointsJson(a.work)}, {QStringLiteral("work_weight"), a.workWeight}, {QStringLiteral("lookat"), lookatJson(a.lookat)}};
        if (a.attached)
            ao.insert(QStringLiteral("attached"), QJsonObject{{QStringLiteral("actor"), double(a.attached->actor)}, {QStringLiteral("joint"), a.attached->joint},
                                                              {QStringLiteral("pos"), arr(a.attached->pos)}, {QStringLiteral("rot"), arr(a.attached->rot)}});
        actors.append(ao);
    }
    for (const Constraint &c : d.constraints) constraints.append(constraintJson(c));
    for (const Camera &c : d.cameras) cams.append(cameraJson(c));
    for (const Light &l : d.lights) lights.append(lightJson(l));
    const Sequence &s = d.seq;
    for (const Track &tr : s.tracks) {
        QJsonObject t{{QStringLiteral("kind"), tr.kind}, {QStringLiteral("name"), tr.name}, {QStringLiteral("actor"), double(tr.actor)},
                      {QStringLiteral("actor_name"), tr.actorName}, {QStringLiteral("layer"), tr.layer}};
        if (tr.kind == QLatin1String("anim")) {
            QJsonArray clips;
            for (const AnimClip &c : tr.clips)
                clips.append(QJsonObject{{QStringLiteral("id"), c.id}, {QStringLiteral("start"), c.start}, {QStringLiteral("dur"), c.dur}, {QStringLiteral("path"), c.path},
                                         {QStringLiteral("mot"), c.mot}, {QStringLiteral("name"), c.name}, {QStringLiteral("endframe"), c.endframe},
                                         {QStringLiteral("offset"), c.offset}, {QStringLiteral("speed"), c.speed}, {QStringLiteral("blend"), c.blend},
                                         {QStringLiteral("loop"), c.loop}});
            t.insert(QStringLiteral("clips"), clips);
        } else if (tr.kind == QLatin1String("pose")) {
            QJsonArray clips;
            for (const PoseClip &c : tr.poses) {
                QJsonArray keys;
                for (const PoseKey &k : c.keys) {
                    QJsonObject ko{{QStringLiteral("t"), k.t}, {QStringLiteral("joints"), jointsJson(k.joints)}};
                    if (!k.ease.isEmpty()) ko.insert(QStringLiteral("ease"), k.ease);
                    keys.append(ko);
                }
                clips.append(QJsonObject{{QStringLiteral("id"), c.id}, {QStringLiteral("start"), c.start}, {QStringLiteral("dur"), c.dur}, {QStringLiteral("keys"), keys},
                                         {QStringLiteral("weight"), c.weight}, {QStringLiteral("fade_in"), c.fadeIn}, {QStringLiteral("fade_out"), c.fadeOut},
                                         {QStringLiteral("loop"), c.loop}, {QStringLiteral("mode"), c.mode}});
            }
            t.insert(QStringLiteral("clips"), clips);
        } else if (tr.kind == QLatin1String("xform") || tr.kind == QLatin1String("cammove")) {
            QJsonArray keys;
            for (const XKey &k : tr.keys) keys.append(keyJson(k, tr.kind == QLatin1String("cammove")));
            t.insert(QStringLiteral("keys"), keys);
            t.insert(QStringLiteral("cam"), tr.cam);
            if (!tr.lookatName.isEmpty()) t.insert(QStringLiteral("lookat_name"), tr.lookatName);
        } else if (tr.kind == QLatin1String("camera")) {
            QJsonArray cuts;
            for (const Cut &c : tr.cuts) cuts.append(QJsonObject{{QStringLiteral("id"), c.id}, {QStringLiteral("start"), c.start}, {QStringLiteral("cam"), c.cam}});
            t.insert(QStringLiteral("cuts"), cuts);
        } else if (tr.kind == QLatin1String("path")) {
            t.insert(QStringLiteral("path"), pathJson(tr.path));
        } else if (tr.kind == QLatin1String("channel")) {
            QJsonArray keys;
            for (const XKey &k : tr.keys) {
                QJsonObject ko{{QStringLiteral("t"), k.t}, {QStringLiteral("v"), k.v}};
                if (!k.ease.isEmpty()) ko.insert(QStringLiteral("ease"), k.ease);
                keys.append(ko);
            }
            t.insert(QStringLiteral("keys"), keys);
            t.insert(QStringLiteral("target"), tr.target);
            t.insert(QStringLiteral("field"), tr.field);
        }
        tracks.append(t);
    }
    for (const Shot &sh : s.shots) {
        QJsonObject o = sh.extra;
        o.insert(QStringLiteral("id"), sh.id);
        o.insert(QStringLiteral("name"), sh.name);
        o.insert(QStringLiteral("a"), sh.a);
        o.insert(QStringLiteral("b"), sh.b);
        if (sh.cam) o.insert(QStringLiteral("cam"), sh.cam);
        shots.append(o);
    }
    QJsonObject seq{{QStringLiteral("name"), s.name}, {QStringLiteral("fps"), s.fps}, {QStringLiteral("length"), s.length}, {QStringLiteral("loop"), s.loop},
                    {QStringLiteral("tracks"), tracks}, {QStringLiteral("shots"), shots}};
    if (s.range) seq.insert(QStringLiteral("range"), QJsonArray{s.range->first, s.range->second});
    if (s.falloff) seq.insert(QStringLiteral("falloff"), QJsonArray{s.falloff->first, s.falloff->second});
    if (!s.audio.isEmpty()) seq.insert(QStringLiteral("audio"), s.audio);
    QJsonObject out{{QStringLiteral("format"), QStringLiteral("director-film")}, {QStringLiteral("version"), 1}, {QStringLiteral("game"), d.gameKey},
                    {QStringLiteral("actors"), actors}, {QStringLiteral("cameras"), cams}, {QStringLiteral("lights"), lights},
                    {QStringLiteral("sequence"), seq}, {QStringLiteral("next_id"), d.nextId}, {QStringLiteral("next_actor"), double(d.nextActor)},
                    {QStringLiteral("overlay"), d.overlay}, {QStringLiteral("constraints"), constraints}, {QStringLiteral("world"), worldJson(d.world)}};
    if (d.hasWork) out.insert(QStringLiteral("work_camera"), cameraJson(d.work));
    if (!d.stageId.isEmpty())
        out.insert(QStringLiteral("stage"), QJsonObject{{QStringLiteral("id"), d.stageId}, {QStringLiteral("name"), d.stageName}, {QStringLiteral("number"), d.stageNumber}});
    return out;
}

Document fromJson(const QJsonObject &o)
{
    Document d;
    d.gameKey = o.value(QStringLiteral("game")).toString();
    d.nextId = std::max(1, o.value(QStringLiteral("next_id")).toInt(1));
    d.nextActor = std::max<qint64>(1001, qint64(o.value(QStringLiteral("next_actor")).toDouble(1001)));
    if (o.contains(QStringLiteral("overlay"))) d.overlay = o.value(QStringLiteral("overlay")).toObject();
    d.world = worldFrom(o.value(QStringLiteral("world")).toObject());
    const QJsonObject stage = o.value(QStringLiteral("stage")).toObject();
    d.stageId = stage.value(QStringLiteral("id")).toString();
    d.stageName = stage.value(QStringLiteral("name")).toString();
    d.stageNumber = stage.value(QStringLiteral("number")).toInt();
    for (const QJsonValue &v : o.value(QStringLiteral("actors")).toArray()) {
        const QJsonObject a = v.toObject();
        Actor x;
        x.id = qint64(a.value(QStringLiteral("id")).toDouble());
        x.name = a.value(QStringLiteral("name")).toString();
        x.displayName = a.value(QStringLiteral("display_name")).toString();
        x.kind = a.value(QStringLiteral("kind")).toString(QStringLiteral("character"));
        x.model = a.value(QStringLiteral("model")).toString();
        x.castId = a.value(QStringLiteral("cast_id")).toString();
        x.physics = a.value(QStringLiteral("physics")).toBool(true);
        x.castTree = a.value(QStringLiteral("cast_tree")).toString();
        x.castCode = a.value(QStringLiteral("cast_code")).toString();
        x.preset = a.value(QStringLiteral("preset")).toString();
        x.pos = fm::vec3(a.value(QStringLiteral("pos")));
        x.rot = fm::quat(a.value(QStringLiteral("rot")));
        x.scale = fm::vec3(a.value(QStringLiteral("scale")), QVector3D(1, 1, 1));
        x.hidden = a.value(QStringLiteral("hidden")).toBool();
        x.rootLock = a.value(QStringLiteral("root_lock")).toBool();
        x.idlePath = a.value(QStringLiteral("idle_path")).toString();
        x.idleMot = a.value(QStringLiteral("idle_mot")).toInt(-1);
        x.work = jointsFrom(a.value(QStringLiteral("work")).toObject());
        x.workWeight = a.value(QStringLiteral("work_weight")).toDouble(1);
        x.lookat = lookatFrom(a.value(QStringLiteral("lookat")).toObject());
        if (const QJsonObject at = a.value(QStringLiteral("attached")).toObject(); !at.isEmpty())
            x.attached = Attachment{qint64(at.value(QStringLiteral("actor")).toDouble()), at.value(QStringLiteral("joint")).toString(),
                                    fm::vec3(at.value(QStringLiteral("pos"))), fm::quat(at.value(QStringLiteral("rot")))};
        d.actors << x;
        d.nextActor = std::max(d.nextActor, x.id + 1);
    }
    for (const QJsonValue &v : o.value(QStringLiteral("constraints")).toArray()) {
        d.constraints << constraintFrom(v.toObject());
        d.nextId = std::max(d.nextId, d.constraints.last().id + 1);
    }
    for (const QJsonValue &v : o.value(QStringLiteral("cameras")).toArray()) d.cameras << cameraFrom(v.toObject());
    if (o.contains(QStringLiteral("work_camera"))) { d.work = cameraFrom(o.value(QStringLiteral("work_camera")).toObject()); d.hasWork = true; }
    for (const QJsonValue &v : o.value(QStringLiteral("lights")).toArray()) {
        d.lights << lightFrom(v.toObject());
        d.nextId = std::max(d.nextId, d.lights.last().id + 1);
    }
    const QJsonObject so = o.value(QStringLiteral("sequence")).toObject();
    Sequence &s = d.seq;
    s.name = so.value(QStringLiteral("name")).toString(QStringLiteral("sequence"));
    s.fps = so.value(QStringLiteral("fps")).toDouble(60);
    s.length = so.value(QStringLiteral("length")).toDouble(600);
    s.loop = so.value(QStringLiteral("loop")).toBool();
    if (const QJsonArray r = so.value(QStringLiteral("range")).toArray(); r.size() == 2) s.range = std::make_pair(r[0].toDouble(), r[1].toDouble());
    if (const QJsonArray f = so.value(QStringLiteral("falloff")).toArray(); f.size() == 2) s.falloff = std::make_pair(f[0].toDouble(), f[1].toDouble());
    s.audio = so.value(QStringLiteral("audio")).toObject();
    for (const QJsonValue &v : so.value(QStringLiteral("shots")).toArray()) {
        QJsonObject x = v.toObject();
        Shot sh;
        sh.id = x.take(QStringLiteral("id")).toInt();
        sh.name = x.take(QStringLiteral("name")).toString();
        sh.a = x.take(QStringLiteral("a")).toDouble();
        sh.b = x.take(QStringLiteral("b")).toDouble();
        sh.cam = x.take(QStringLiteral("cam")).toInt();
        sh.extra = x;
        s.shots << sh;
        d.nextId = std::max(d.nextId, sh.id + 1);
    }
    for (const QJsonValue &v : so.value(QStringLiteral("tracks")).toArray()) {
        const QJsonObject t = v.toObject();
        Track tr;
        tr.kind = t.value(QStringLiteral("kind")).toString();
        tr.name = t.value(QStringLiteral("name")).toString();
        tr.actor = qint64(t.value(QStringLiteral("actor")).toDouble());
        tr.actorName = t.value(QStringLiteral("actor_name")).toString();
        tr.layer = t.value(QStringLiteral("layer")).toInt();
        tr.cam = t.value(QStringLiteral("cam")).toInt();
        tr.lookatName = t.value(QStringLiteral("lookat_name")).toString();
        tr.target = t.value(QStringLiteral("target")).toString();
        tr.field = t.value(QStringLiteral("field")).toString();
        if (tr.kind == QLatin1String("path")) tr.path = pathFrom(t.value(QStringLiteral("path")).toObject());
        for (const QJsonValue &cv : t.value(QStringLiteral("clips")).toArray()) {
            const QJsonObject c = cv.toObject();
            if (tr.kind == QLatin1String("anim")) {
                AnimClip a;
                a.id = c.value(QStringLiteral("id")).toInt();
                a.start = c.value(QStringLiteral("start")).toDouble();
                a.dur = std::max(1.0, c.value(QStringLiteral("dur")).toDouble(1));
                a.path = c.value(QStringLiteral("path")).toString();
                a.mot = c.value(QStringLiteral("mot")).toInt(-1);
                a.name = c.value(QStringLiteral("name")).toString();
                a.endframe = c.value(QStringLiteral("endframe")).toDouble();
                a.offset = c.value(QStringLiteral("offset")).toDouble();
                a.speed = c.value(QStringLiteral("speed")).toDouble(1);
                a.blend = c.value(QStringLiteral("blend")).toDouble(10);
                a.loop = c.value(QStringLiteral("loop")).toBool();
                tr.clips << a;
                d.nextId = std::max(d.nextId, a.id + 1);
            } else if (tr.kind == QLatin1String("pose")) {
                PoseClip p;
                p.id = c.value(QStringLiteral("id")).toInt();
                p.start = c.value(QStringLiteral("start")).toDouble();
                p.dur = std::max(1.0, c.value(QStringLiteral("dur")).toDouble(1));
                p.weight = c.value(QStringLiteral("weight")).toDouble(1);
                p.fadeIn = c.value(QStringLiteral("fade_in")).toDouble(10);
                p.fadeOut = c.value(QStringLiteral("fade_out")).toDouble(10);
                p.loop = c.value(QStringLiteral("loop")).toBool();
                p.mode = c.value(QStringLiteral("mode")).toString(QStringLiteral("override"));
                for (const QJsonValue &kv : c.value(QStringLiteral("keys")).toArray()) {
                    const QJsonObject k = kv.toObject();
                    PoseKey pk;
                    pk.t = k.value(QStringLiteral("t")).toDouble();
                    pk.joints = jointsFrom(k.value(QStringLiteral("joints")).toObject());
                    pk.ease = k.value(QStringLiteral("ease")).toString();
                    p.keys << pk;
                }
                tr.poses << p;
                d.nextId = std::max(d.nextId, p.id + 1);
            }
        }
        for (const QJsonValue &kv : t.value(QStringLiteral("keys")).toArray()) tr.keys << keyFrom(kv.toObject());
        std::sort(tr.keys.begin(), tr.keys.end(), [](const XKey &a, const XKey &b) { return a.t < b.t; });
        for (const QJsonValue &cv : t.value(QStringLiteral("cuts")).toArray()) {
            const QJsonObject c = cv.toObject();
            Cut cut{c.value(QStringLiteral("id")).toInt(), c.value(QStringLiteral("start")).toDouble(), c.value(QStringLiteral("cam")).toInt()};
            tr.cuts << cut;
            d.nextId = std::max(d.nextId, cut.id + 1);
        }
        s.tracks << tr;
    }
    return d;
}

// ------------------------------------------------------------------ UI state (seq_state / cameras_state in bridge.lua)
QJsonObject sequenceState(const Document &d, double t, bool playing, double speed)
{
    const Sequence &s = d.seq;
    QJsonArray tracks;
    for (int i = 0; i < s.tracks.size(); ++i) {
        const Track &tr = s.tracks[i];
        const bool camTrack = tr.kind == QLatin1String("camera") || tr.kind == QLatin1String("cammove");
        QJsonObject o{{QStringLiteral("idx"), i + 1}, {QStringLiteral("kind"), tr.kind}, {QStringLiteral("name"), tr.name},
                      {QStringLiteral("layer"), tr.layer}, {QStringLiteral("missing"), !camTrack && !d.actor(tr.actor)}};
        if (!camTrack) {
            o.insert(QStringLiteral("actor"), double(tr.actor));
            o.insert(QStringLiteral("actor_name"), tr.actorName);
        }
        if (tr.kind == QLatin1String("anim")) {
            QJsonArray clips;
            for (const AnimClip &c : tr.clips)
                clips.append(QJsonObject{{QStringLiteral("id"), c.id}, {QStringLiteral("start"), c.start}, {QStringLiteral("dur"), c.dur}, {QStringLiteral("name"), c.name},
                                         {QStringLiteral("mot"), c.mot}, {QStringLiteral("path"), c.path}, {QStringLiteral("endframe"), c.endframe},
                                         {QStringLiteral("offset"), c.offset}, {QStringLiteral("speed"), c.speed}, {QStringLiteral("blend"), c.blend},
                                         {QStringLiteral("loop"), c.loop}, {QStringLiteral("rm"), false}});
            o.insert(QStringLiteral("clips"), clips);
        } else if (tr.kind == QLatin1String("pose")) {
            QJsonArray clips;
            for (const PoseClip &c : tr.poses) {
                QJsonArray kt, ke;
                for (const PoseKey &k : c.keys) { kt.append(k.t); ke.append(k.ease.isEmpty() ? QStringLiteral("smooth") : k.ease); }
                clips.append(QJsonObject{{QStringLiteral("id"), c.id}, {QStringLiteral("start"), c.start}, {QStringLiteral("dur"), c.dur}, {QStringLiteral("keys"), kt},
                                         {QStringLiteral("kease"), ke}, {QStringLiteral("weight"), c.weight}, {QStringLiteral("fade_in"), c.fadeIn},
                                         {QStringLiteral("fade_out"), c.fadeOut}, {QStringLiteral("loop"), c.loop}, {QStringLiteral("mode"), c.mode}});
            }
            o.insert(QStringLiteral("clips"), clips);
        } else if (tr.kind == QLatin1String("xform")) {
            QJsonArray keys;
            for (const XKey &k : tr.keys) {
                QJsonObject ko{{QStringLiteral("t"), k.t}, {QStringLiteral("pos"), arr(k.pos)}, {QStringLiteral("yaw"), fm::toEulerUpright(k.rot).y()}};
                if (!k.ease.isEmpty()) ko.insert(QStringLiteral("ease"), k.ease);
                keys.append(ko);
            }
            o.insert(QStringLiteral("keys"), keys);
        } else if (tr.kind == QLatin1String("channel")) {
            QJsonArray keys;
            for (const XKey &k : tr.keys) {
                QJsonObject ko{{QStringLiteral("t"), k.t}, {QStringLiteral("v"), k.v}};
                if (!k.ease.isEmpty()) ko.insert(QStringLiteral("ease"), k.ease);
                keys.append(ko);
            }
            o.insert(QStringLiteral("keys"), keys);
            o.insert(QStringLiteral("target"), tr.target);
            o.insert(QStringLiteral("field"), tr.field);
            o.insert(QStringLiteral("label"), tr.name);
            o.insert(QStringLiteral("missing"), false);
            if (tr.target.startsWith(QLatin1String("cam:"))) o.insert(QStringLiteral("cam"), tr.target.mid(4).toInt());
        } else if (tr.kind == QLatin1String("cammove")) {
            o.insert(QStringLiteral("cam"), tr.cam);
            if (!tr.lookatName.isEmpty()) o.insert(QStringLiteral("lookat_name"), tr.lookatName);
            QJsonArray keys;
            for (const XKey &k : tr.keys) {
                QJsonObject ko{{QStringLiteral("t"), k.t}, {QStringLiteral("pos"), arr(k.pos)}, {QStringLiteral("fov"), k.fov}, {QStringLiteral("roll"), k.roll}};
                if (!k.ease.isEmpty()) ko.insert(QStringLiteral("ease"), k.ease);
                keys.append(ko);
            }
            o.insert(QStringLiteral("keys"), keys);
        } else if (tr.kind == QLatin1String("camera")) {
            QJsonArray cuts;
            for (const Cut &c : tr.cuts) cuts.append(QJsonObject{{QStringLiteral("id"), c.id}, {QStringLiteral("start"), c.start}, {QStringLiteral("cam"), c.cam}});
            o.insert(QStringLiteral("cuts"), cuts);
        } else if (tr.kind == QLatin1String("path")) {
            const PathSpec &p = tr.path;
            QJsonArray pts;
            for (const QVector3D &v : p.points) pts.append(arr(v));
            o.insert(QStringLiteral("start"), p.start);
            o.insert(QStringLiteral("dur"), p.dur);
            o.insert(QStringLiteral("speed"), p.speed);
            o.insert(QStringLiteral("ease"), p.ease);
            o.insert(QStringLiteral("gait"), p.gait);
            o.insert(QStringLiteral("clip_name"), p.clipName);
            o.insert(QStringLiteral("points"), int(p.points.size()));
            o.insert(QStringLiteral("pts"), pts);
            o.insert(QStringLiteral("dur_pinned"), p.durPinned);
            const PathCurve c = pathCurve(p.points);
            o.insert(QStringLiteral("length"), c.length);
            QJsonArray curve;
            for (int k = 0; k < c.pos.size(); k += 2) curve.append(arr(c.pos[k]));
            if (!c.pos.isEmpty() && (c.pos.size() - 1) % 2) curve.append(arr(c.pos.last()));
            o.insert(QStringLiteral("curve"), curve);
        }
        tracks.append(o);
    }
    QJsonArray shots;
    for (const Shot &sh : s.shots) {
        QJsonObject o = sh.extra;
        o.insert(QStringLiteral("id"), sh.id);
        o.insert(QStringLiteral("name"), sh.name);
        o.insert(QStringLiteral("a"), sh.a);
        o.insert(QStringLiteral("b"), sh.b);
        if (sh.cam) o.insert(QStringLiteral("cam"), sh.cam);
        shots.append(o);
    }
    QJsonObject out{{QStringLiteral("name"), s.name}, {QStringLiteral("fps"), s.fps}, {QStringLiteral("length"), s.length}, {QStringLiteral("loop"), s.loop},
                    {QStringLiteral("playing"), playing}, {QStringLiteral("t"), t}, {QStringLiteral("tracks"), tracks}, {QStringLiteral("speed"), speed},
                    {QStringLiteral("shots"), shots}};
    if (s.range) out.insert(QStringLiteral("range"), QJsonArray{s.range->first, s.range->second});
    if (s.falloff) out.insert(QStringLiteral("falloff"), QJsonArray{s.falloff->first, s.falloff->second});
    if (!s.audio.isEmpty()) out.insert(QStringLiteral("audio"), s.audio);
    return out;
}

QJsonArray camerasState(const Document &d, int liveCamera)
{
    QJsonArray out;
    for (int i = 0; i < d.cameras.size(); ++i) {
        const Camera &c = d.cameras[i];
        QJsonObject o{{QStringLiteral("i"), i + 1}, {QStringLiteral("name"), c.name}, {QStringLiteral("mode"), c.mode}, {QStringLiteral("fov"), c.fov},
                      {QStringLiteral("pos"), arr(c.pos)}, {QStringLiteral("rot"), arr(c.rot)}, {QStringLiteral("euler"), arr(fm::toEulerUpright(c.rot))},
                      {QStringLiteral("live"), liveCamera == i + 1}, {QStringLiteral("dof"), c.dof}, {QStringLiteral("roll"), c.roll},
                      {QStringLiteral("radius"), 2.0}, {QStringLiteral("height"), 1.5}, {QStringLiteral("speed"), 0.2},
                      {QStringLiteral("offset"), QJsonArray{0, 1.4, 0}}, {QStringLiteral("smooth"), 0.12}};
        if (c.dofF) o.insert(QStringLiteral("dof_f"), *c.dofF);
        if (c.dofFocus) o.insert(QStringLiteral("dof_focus"), *c.dofFocus);
        if (c.target) {
            o.insert(QStringLiteral("target"), double(c.target));
            if (const Actor *a = d.actor(c.target)) o.insert(QStringLiteral("target_name"), a->displayName.isEmpty() ? a->name : a->displayName);
        }
        if (!c.shake.isEmpty()) o.insert(QStringLiteral("shake"), c.shake);
        out.append(o);
    }
    return out;
}

QJsonArray lightsState(const Document &d)
{
    QJsonArray out;
    for (const Light &l : d.lights) out.append(lightJson(l));
    return out;
}

// ------------------------------------------------------------------ evaluation helpers (sequence.lua)
double clipFrame(const AnimClip &c, double t)
{
    double f = c.offset + (t - c.start) * c.speed;
    if (c.endframe > 0) {
        if (c.loop) f = std::fmod(f, c.endframe);
        else f = std::clamp(f, 0.0, c.endframe - 0.01);
    }
    return std::max(0.0, f);
}

const AnimClip *clipAt(const Track &tr, double t)
{
    const AnimClip *found = nullptr;
    for (const AnimClip &c : tr.clips) if (t >= c.start && t < c.start + c.dur) found = &c;
    return found;
}

const PoseClip *poseClipAt(const Track &tr, double t)
{
    const PoseClip *found = nullptr;
    for (const PoseClip &c : tr.poses) if (t >= c.start && t < c.start + c.dur) found = &c;
    return found;
}

const Cut *cutAt(const Track &tr, double t)
{
    const Cut *found = nullptr;
    for (const Cut &c : tr.cuts) if (c.start <= t) found = &c;
    return found;
}

double fadeWeight(double lt, double dur, double fin, double fout)
{
    double w = 1;
    if (fin > 0 && lt < fin) w = std::min(w, lt / fin);
    if (fout > 0 && dur - lt < fout) w = std::min(w, std::max(0.0, (dur - lt) / fout));
    return std::clamp(w, 0.0, 1.0);
}

QMap<QString, QQuaternion> evalPoseKeys(const QVector<PoseKey> &keys, double t, bool loop)
{
    const int n = int(keys.size());
    if (n == 0) return {};
    if (n == 1) return keys[0].joints;
    const double last = keys[n - 1].t;
    if (loop && last > 0) t = std::fmod(t, last);
    if (t <= keys[0].t) return keys[0].joints;
    if (t >= last) return keys[n - 1].joints;
    int i = 0;
    while (i < n - 2 && !(t >= keys[i].t && t <= keys[i + 1].t)) ++i;
    const PoseKey &k0 = keys[i], &k1 = keys[i + 1];
    const double span = k1.t - k0.t;
    const double s = fm::ease(k0.ease, span > 0 ? (t - k0.t) / span : 1);
    QMap<QString, QQuaternion> out;
    for (auto it = k0.joints.cbegin(); it != k0.joints.cend(); ++it) {
        const auto j1 = k1.joints.constFind(it.key());
        out.insert(it.key(), j1 != k1.joints.cend() ? fm::slerp(it.value(), *j1, float(s)) : it.value());
    }
    for (auto it = k1.joints.cbegin(); it != k1.joints.cend(); ++it)
        if (!out.contains(it.key())) out.insert(it.key(), it.value());
    return out;
}

bool evalChannel(const QVector<XKey> &keys, double t, double *v)
{
    if (keys.isEmpty()) return false;
    if (t <= keys.first().t) { *v = keys.first().v; return true; }
    if (t >= keys.last().t) { *v = keys.last().v; return true; }
    int i = 0;
    while (i + 1 < keys.size() && keys[i + 1].t <= t) ++i;
    const XKey &k0 = keys[i], &k1 = keys[i + 1];
    const double s = k1.t > k0.t ? fm::ease(k0.ease, (t - k0.t) / (k1.t - k0.t)) : 0.0;
    *v = k0.v + (k1.v - k0.v) * s;
    return true;
}

bool evalKeys(const QVector<XKey> &keys, double t, XKey *out)
{
    if (keys.isEmpty()) return false;
    int i0 = 0, i1 = -1;
    for (int i = 0; i < keys.size(); ++i) {
        if (keys[i].t <= t) { i0 = i; i1 = i + 1 < keys.size() ? i + 1 : -1; }
        else break;
    }
    if (t < keys[0].t) { i0 = 0; i1 = -1; }
    const XKey &k0 = keys[i0];
    *out = k0;
    if (i1 >= 0 && keys[i1].t > k0.t) {
        const XKey &k1 = keys[i1];
        const float s = float(fm::ease(k0.ease, (t - k0.t) / (k1.t - k0.t)));
        out->pos = k0.pos + (k1.pos - k0.pos) * s;
        out->rot = fm::slerp(k0.rot, k1.rot, s);
        const double f0 = k0.fov, f1 = k1.fov > 0 ? k1.fov : k0.fov;
        out->fov = f0 + (f1 - f0) * s;
        out->roll = k0.roll + (k1.roll - k0.roll) * s;
        if (k0.dofF && k1.dofF) out->dofF = *k0.dofF + (*k1.dofF - *k0.dofF) * s;
        if (k0.dofFocus && k1.dofFocus) out->dofFocus = *k0.dofFocus + (*k1.dofFocus - *k0.dofFocus) * s;
    }
    return true;
}

// ------------------------------------------------------------------ walk paths (path.lua)
PathCurve pathCurve(const QVector<QVector3D> &P)
{
    PathCurve c;
    if (P.isEmpty()) return c;
    if (P.size() == 1) { c.pos << P[0]; c.tan << QVector3D(0, 0, 1); c.dist << 0; return c; }
    constexpr int kSamples = 16;
    auto cr = [](const QVector3D &p0, const QVector3D &p1, const QVector3D &p2, const QVector3D &p3, float u) {
        const float u2 = u * u, u3 = u2 * u;
        return 0.5f * (2 * p1 + (-p0 + p2) * u + (2 * p0 - 5 * p1 + 4 * p2 - p3) * u2 + (-p0 + 3 * p1 - 3 * p2 + p3) * u3);
    };
    double d = 0;
    for (int i = 0; i + 1 < P.size(); ++i) {
        const QVector3D &p0 = P[std::max(0, i - 1)], &p1 = P[i], &p2 = P[i + 1], &p3 = P[std::min(int(P.size()) - 1, i + 2)];
        const int n = i == P.size() - 2 ? kSamples : kSamples - 1;
        for (int k = 0; k <= n; ++k) {
            const QVector3D pos = cr(p0, p1, p2, p3, float(k) / kSamples);
            if (!c.pos.isEmpty()) d += (pos - c.pos.last()).length();
            c.pos << pos;
            c.dist << d;
        }
    }
    c.length = d;
    for (int k = 0; k < c.pos.size(); ++k) {
        QVector3D t = c.pos[std::min(k + 1, int(c.pos.size()) - 1)] - c.pos[std::max(k - 1, 0)];
        t.setY(0);
        c.tan << (t.length() > 1e-5f ? t.normalized() : (k > 0 ? c.tan[k - 1] : QVector3D(0, 0, 1)));
    }
    return c;
}

bool pathAt(const PathCurve &c, double d, QVector3D *pos, QVector3D *tan)
{
    const int n = int(c.pos.size());
    if (n == 0) return false;
    if (d <= 0 || n == 1) { *pos = c.pos[0]; *tan = c.tan[0]; return true; }
    if (d >= c.length) { *pos = c.pos[n - 1]; *tan = c.tan[n - 1]; return true; }
    const int hi = int(std::upper_bound(c.dist.cbegin(), c.dist.cend(), d) - c.dist.cbegin());
    const int lo = std::max(0, hi - 1);
    const double span = c.dist[hi] - c.dist[lo];
    const float u = float(span > 0 ? (d - c.dist[lo]) / span : 0);
    *pos = c.pos[lo] + (c.pos[hi] - c.pos[lo]) * u;
    const QVector3D t = c.tan[lo] * (1 - u) + c.tan[hi] * u;
    *tan = t.length() > 1e-5f ? t.normalized() : c.tan[lo];
    return true;
}

double pathDistanceAt(const PathSpec &p, double length, double t)
{
    const double x = std::clamp((t - p.start) / std::max(1.0, p.dur), 0.0, 1.0);
    const double e = std::clamp(p.ease, 0.0, 0.45);
    if (e <= 0) return x * length;
    auto S = [](double u) { return u * u * u - u * u * u * u * 0.5; };   // integral of smoothstep
    double s;
    if (x < e) s = e * S(x / e);
    else if (x <= 1 - e) s = e * 0.5 + (x - e);
    else s = e * 0.5 + (1 - 2 * e) + e * (0.5 - S((1 - x) / e));
    return std::clamp(s / (1 - e), 0.0, 1.0) * length;
}

double pathDuration(const PathSpec &p, double length)
{
    const double speed = p.speed > 0.01 ? p.speed : 1.35;
    return std::max(1.0, std::round(length / speed * 60));
}

} // namespace film
