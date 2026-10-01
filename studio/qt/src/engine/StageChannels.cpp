// Channels (SFM's animatable values): any number of a light, camera, character or constraint keyed over time on
// a `channel` track — a light that dims, a focus pull, a head that stops looking, a prop that appears. A value
// with a channel follows it; changing that value (slider, Element Viewer, automation) keys it at the current
// frame, so an animated value is edited the way it is animated.
#include "Maths.h"
#include "StageRuntime.h"

#include <QJsonDocument>
#include <cmath>

using namespace film;

namespace {
struct ChannelDef { const char *field, *label; };
// what can be animated, per kind of target
const QHash<QString, QVector<ChannelDef>> &channelDefs()
{
    static const QHash<QString, QVector<ChannelDef>> defs{
        {QStringLiteral("light"), {{"intensity", "Intensity"}, {"temperature", "Temperature"}, {"radius", "Range"}, {"cone", "Cone"},
                                   {"spread", "Edge softness"}, {"bounce", "Bounce"}, {"volumetric", "Fog"}, {"specular", "Highlights"},
                                   {"size", "Size"}, {"red", "Red"}, {"green", "Green"}, {"blue", "Blue"}, {"enabled", "On"}}},
        {QStringLiteral("cam"), {{"fov", "Field of view"}, {"roll", "Roll"}, {"dof_f", "Aperture"}, {"dof_focus", "Focus distance"}}},
        {QStringLiteral("actor"), {{"lookat_weight", "Look-at weight"}, {"scale", "Size"}, {"visible", "Visible"}, {"pose_weight", "Pose weight"}}},
        {QStringLiteral("constraint"), {{"weight", "Weight"}}},
        {QStringLiteral("world"), {{"sun", "Sun"}, {"sun_yaw", "Sun direction"}, {"sun_elevation", "Sun height"}, {"sun_temp", "Sun colour"},
                                   {"sky", "Sky light"}, {"map_lights", "Map lights"}, {"exposure", "Exposure"}, {"contrast", "Contrast"},
                                   {"saturation", "Saturation"}, {"brightness", "Brightness"}, {"bloom", "Bloom"}, {"vignette", "Vignette"},
                                   {"sharpen", "Sharpen"}}},
    };
    return defs;
}
bool stepped(const QString &field) { return field == QLatin1String("enabled") || field == QLatin1String("visible"); }
} // namespace

// "light:3/intensity" -> ("light:3", "intensity")
static std::pair<QString, QString> splitChannel(const QString &channel)
{
    const int i = channel.lastIndexOf(QLatin1Char('/'));
    return i > 0 ? std::make_pair(channel.left(i), channel.mid(i + 1)) : std::make_pair(channel, QString());
}

std::optional<double> StageRuntime::channelGet(const QString &target, const QString &field) const
{
    const QString kind = target.section(QLatin1Char(':'), 0, 0);
    const qint64 id = target.section(QLatin1Char(':'), 1).toLongLong();
    if (kind == QLatin1String("light")) {
        for (const Light &l : m_doc.lights) {
            if (l.id != id) continue;
            if (field == QLatin1String("intensity")) return l.intensity;
            if (field == QLatin1String("temperature")) return l.temperature;
            if (field == QLatin1String("radius")) return l.radius;
            if (field == QLatin1String("cone")) return l.cone;
            if (field == QLatin1String("spread")) return l.spread;
            if (field == QLatin1String("bounce")) return l.bounce;
            if (field == QLatin1String("volumetric")) return l.volumetric;
            if (field == QLatin1String("specular")) return l.specular;
            if (field == QLatin1String("size")) return l.size;
            if (field == QLatin1String("red")) return l.color.x();
            if (field == QLatin1String("green")) return l.color.y();
            if (field == QLatin1String("blue")) return l.color.z();
            if (field == QLatin1String("enabled")) return l.enabled ? 1.0 : 0.0;
        }
    } else if (kind == QLatin1String("cam")) {
        const Camera *c = const_cast<Document &>(m_doc).camera(int(id));
        if (!c) return std::nullopt;
        if (field == QLatin1String("fov")) return c->fov;
        if (field == QLatin1String("roll")) return c->roll;
        if (field == QLatin1String("dof_f")) return c->dofF.value_or(2.8);
        if (field == QLatin1String("dof_focus")) return c->dofFocus.value_or(3.0);
    } else if (kind == QLatin1String("actor")) {
        const Actor *a = m_doc.actor(id);
        if (!a) return std::nullopt;
        if (field == QLatin1String("lookat_weight")) return a->lookat.weight;
        if (field == QLatin1String("scale")) return a->scale.x();
        if (field == QLatin1String("visible")) return a->hidden ? 0.0 : 1.0;
        if (field == QLatin1String("pose_weight")) return a->workWeight;
    } else if (kind == QLatin1String("constraint")) {
        for (const Constraint &c : m_doc.constraints)
            if (c.id == id && field == QLatin1String("weight")) return c.weight;
    } else if (kind == QLatin1String("world")) {
        const World &w = m_doc.world;
        if (field == QLatin1String("sun")) return w.sunStrength;
        if (field == QLatin1String("sun_yaw")) return w.sunYaw;
        if (field == QLatin1String("sun_elevation")) return w.sunElevation;
        if (field == QLatin1String("sun_temp")) return w.sunTemp;
        if (field == QLatin1String("sky")) return w.sky;
        if (field == QLatin1String("map_lights")) return w.mapLights;
        if (field == QLatin1String("exposure")) return w.exposure;
        if (field == QLatin1String("contrast")) return w.contrast;
        if (field == QLatin1String("saturation")) return w.saturation;
        if (field == QLatin1String("brightness")) return w.brightness;
        if (field == QLatin1String("bloom")) return w.bloom;
        if (field == QLatin1String("vignette")) return w.vignette;
        if (field == QLatin1String("sharpen")) return w.sharpen;
    }
    return std::nullopt;
}

bool StageRuntime::channelSet(const QString &target, const QString &field, double v)
{
    const QString kind = target.section(QLatin1Char(':'), 0, 0);
    const qint64 id = target.section(QLatin1Char(':'), 1).toLongLong();
    if (kind == QLatin1String("light")) {
        for (Light &l : m_doc.lights) {
            if (l.id != id) continue;
            if (field == QLatin1String("intensity")) l.intensity = std::max(0.0, v);
            else if (field == QLatin1String("temperature")) l.temperature = std::clamp(v, 1000.0, 20000.0);
            else if (field == QLatin1String("radius")) l.radius = std::max(0.05, v);
            else if (field == QLatin1String("cone")) l.cone = std::clamp(v, 1.0, 170.0);
            else if (field == QLatin1String("spread")) l.spread = std::clamp(v, 0.0, 1.0);
            else if (field == QLatin1String("bounce")) l.bounce = std::max(0.0, v);
            else if (field == QLatin1String("volumetric")) l.volumetric = std::max(0.0, v);
            else if (field == QLatin1String("specular")) l.specular = std::max(0.0, v);
            else if (field == QLatin1String("size")) l.size = std::max(0.0, v);
            else if (field == QLatin1String("red")) l.color.setX(float(std::clamp(v, 0.0, 1.0)));
            else if (field == QLatin1String("green")) l.color.setY(float(std::clamp(v, 0.0, 1.0)));
            else if (field == QLatin1String("blue")) l.color.setZ(float(std::clamp(v, 0.0, 1.0)));
            else if (field == QLatin1String("enabled")) l.enabled = v >= 0.5;
            else return false;
            return true;
        }
    } else if (kind == QLatin1String("cam")) {
        Camera *c = m_doc.camera(int(id));
        if (!c) return false;
        if (field == QLatin1String("fov")) c->fov = std::clamp(v, 1.0, 170.0);
        else if (field == QLatin1String("roll")) c->roll = v;
        else if (field == QLatin1String("dof_f")) c->dofF = std::clamp(v, 0.5, 64.0);
        else if (field == QLatin1String("dof_focus")) c->dofFocus = std::max(0.05, v);
        else return false;
        return true;
    } else if (kind == QLatin1String("actor")) {
        Actor *a = m_doc.actor(id);
        if (!a) return false;
        if (field == QLatin1String("lookat_weight")) a->lookat.weight = std::clamp(v, 0.0, 1.0);
        else if (field == QLatin1String("scale")) { const float s = float(std::max(0.01, v)); a->scale = QVector3D(s, s, s); }
        else if (field == QLatin1String("visible")) a->hidden = v < 0.5;
        else if (field == QLatin1String("pose_weight")) a->workWeight = std::clamp(v, 0.0, 1.0);
        else return false;
        return true;
    } else if (kind == QLatin1String("constraint")) {
        for (Constraint &c : m_doc.constraints)
            if (c.id == id && field == QLatin1String("weight")) { c.weight = std::clamp(v, 0.0, 1.0); return true; }
    } else if (kind == QLatin1String("world")) {
        World &w = m_doc.world;
        if (field == QLatin1String("sun")) w.sunStrength = std::max(0.0, v);
        else if (field == QLatin1String("sun_yaw")) w.sunYaw = v;
        else if (field == QLatin1String("sun_elevation")) w.sunElevation = std::clamp(v, -10.0, 90.0);
        else if (field == QLatin1String("sun_temp")) w.sunTemp = std::clamp(v, 1500.0, 15000.0);
        else if (field == QLatin1String("sky")) w.sky = std::max(0.0, v);
        else if (field == QLatin1String("map_lights")) w.mapLights = std::max(0.0, v);
        else if (field == QLatin1String("exposure")) w.exposure = std::clamp(v, -6.0, 6.0);
        else if (field == QLatin1String("contrast")) w.contrast = std::clamp(v, 0.0, 3.0);
        else if (field == QLatin1String("saturation")) w.saturation = std::clamp(v, 0.0, 3.0);
        else if (field == QLatin1String("brightness")) w.brightness = std::clamp(v, 0.0, 3.0);
        else if (field == QLatin1String("bloom")) w.bloom = std::clamp(v, 0.0, 1.0);
        else if (field == QLatin1String("vignette")) w.vignette = std::clamp(v, 0.0, 1.0);
        else if (field == QLatin1String("sharpen")) w.sharpen = std::clamp(v, 0.0, 1.0);
        else return false;
        return true;
    }
    return false;
}

QString StageRuntime::channelLabel(const QString &target, const QString &field) const
{
    const QString kind = target.section(QLatin1Char(':'), 0, 0);
    const qint64 id = target.section(QLatin1Char(':'), 1).toLongLong();
    QString what = target, prop = field;
    for (const ChannelDef &d : channelDefs().value(kind)) if (field == QLatin1String(d.field)) prop = QString::fromUtf8(d.label);
    if (kind == QLatin1String("light")) {
        for (const Light &l : m_doc.lights) if (l.id == id) what = l.name.isEmpty() ? QStringLiteral("Light %1").arg(id) : l.name;
    } else if (kind == QLatin1String("cam")) {
        if (const Camera *c = const_cast<Document &>(m_doc).camera(int(id))) what = c->name;
    } else if (kind == QLatin1String("actor")) {
        if (const Actor *a = m_doc.actor(id)) what = a->displayName.isEmpty() ? a->name : a->displayName;
    } else if (kind == QLatin1String("world")) {
        what = QStringLiteral("World");
    } else if (kind == QLatin1String("constraint")) {
        what = QStringLiteral("Constraint %1").arg(id);
        for (const Constraint &c : m_doc.constraints) if (c.id == id && !c.name.isEmpty()) what = c.name;
    }
    return what + QStringLiteral(" · ") + prop;
}

Track *StageRuntime::channelTrack(const QString &target, const QString &field)
{
    for (Track &tr : m_doc.seq.tracks)
        if (tr.kind == QLatin1String("channel") && tr.target == target && tr.field == field) return &tr;
    return nullptr;
}

// a key at t (the current value when none is given); makes the track the first time
void StageRuntime::keyChannel(const QString &target, const QString &field, double t, std::optional<double> v)
{
    const std::optional<double> cur = v ? v : channelGet(target, field);
    if (!cur) { log(QStringLiteral("warn"), QStringLiteral("%1 cannot be animated").arg(target + QLatin1Char('/') + field)); return; }
    Track *tr = channelTrack(target, field);
    if (!tr) {
        Track n;
        n.kind = QStringLiteral("channel");
        n.target = target;
        n.field = field;
        n.name = channelLabel(target, field);
        if (target.startsWith(QLatin1String("actor:"))) {
            n.actor = target.section(QLatin1Char(':'), 1).toLongLong();
            if (const Actor *a = m_doc.actor(n.actor)) n.actorName = a->name;
        }
        m_doc.seq.tracks << n;
        tr = &m_doc.seq.tracks.last();
    }
    XKey k;
    k.t = std::max(0.0, std::round(t));
    k.v = *cur;
    if (stepped(field)) k.ease = QStringLiteral("hold");
    auto it = std::find_if(tr->keys.begin(), tr->keys.end(), [&](const XKey &x) { return std::abs(x.t - k.t) < 0.5; });
    if (it != tr->keys.end()) { it->v = k.v; if (stepped(field)) it->ease = k.ease; }
    else {
        tr->keys << k;
        std::sort(tr->keys.begin(), tr->keys.end(), [](const XKey &a, const XKey &b) { return a.t < b.t; });
    }
    grow(k.t + 1);
}

// every channel sets its value for time t (after camera moves, before the picture reads lights / cameras)
void StageRuntime::applyChannels(double t)
{
    for (const Track &tr : std::as_const(m_doc.seq.tracks)) {
        if (tr.kind != QLatin1String("channel")) continue;
        double v;
        if (evalChannel(tr.keys, t, &v)) channelSet(tr.target, tr.field, v);
    }
}

// channels whose values an op changed (not what the channel says at this frame): key them here
QHash<QString, double> StageRuntime::channelSnapshot() const
{
    QHash<QString, double> out;
    for (const Track &tr : m_doc.seq.tracks)
        if (tr.kind == QLatin1String("channel"))
            if (const auto v = channelGet(tr.target, tr.field)) out.insert(tr.target + QLatin1Char('/') + tr.field, *v);
    return out;
}

void StageRuntime::keyChangedChannels(const QHash<QString, double> &before)
{
    const double t = std::floor(m_t);
    QVector<std::pair<QString, QString>> key;
    for (const Track &tr : std::as_const(m_doc.seq.tracks)) {
        if (tr.kind != QLatin1String("channel")) continue;
        const QString name = tr.target + QLatin1Char('/') + tr.field;
        const auto now = channelGet(tr.target, tr.field);
        if (!now || !before.contains(name)) continue;
        const double was = before.value(name);
        double expect = was;
        evalChannel(tr.keys, m_t, &expect);
        const double eps = 1e-6 * std::max(1.0, std::abs(*now));
        if (std::abs(*now - was) > eps && std::abs(*now - expect) > eps) key.append({tr.target, tr.field});
    }
    for (const auto &k : key) keyChannel(k.first, k.second, t, std::nullopt);
}

// {"light:3/intensity": {track, keys, here}} for the key buttons next to the values
QJsonObject StageRuntime::channelsState() const
{
    QJsonObject out;
    const Sequence &s = m_doc.seq;
    for (int i = 0; i < s.tracks.size(); ++i) {
        const Track &tr = s.tracks[i];
        if (tr.kind != QLatin1String("channel")) continue;
        bool here = false;
        for (const XKey &k : tr.keys) if (std::abs(k.t - std::floor(m_t)) < 0.5) here = true;
        out.insert(tr.target + QLatin1Char('/') + tr.field,
                   QJsonObject{{QStringLiteral("track"), i + 1}, {QStringLiteral("keys"), int(tr.keys.size())}, {QStringLiteral("here"), here}});
    }
    return out;
}

void StageRuntime::registerChannelOps()
{
    auto &o = m_ops;
    auto parse = [](const QJsonObject &c) {
        if (c.contains(QStringLiteral("channel"))) return splitChannel(c.value(QStringLiteral("channel")).toString());
        return std::make_pair(c.value(QStringLiteral("target")).toString(), c.value(QStringLiteral("field")).toString());
    };
    // key a value now (or at t, to v): {channel: "light:3/intensity"} or {target, field}, t?, v?
    o[QStringLiteral("key_channel")] = [this, parse](const QJsonObject &c) {
        const auto [target, field] = parse(c);
        if (target.isEmpty() || field.isEmpty()) return;
        keyChannel(target, field, c.contains(QStringLiteral("t")) ? c.value(QStringLiteral("t")).toDouble() : std::floor(m_t),
                   c.contains(QStringLiteral("v")) ? std::optional<double>(c.value(QStringLiteral("v")).toDouble()) : std::nullopt);
    };
    o[QStringLiteral("remove_channel_key")] = [this, parse](const QJsonObject &c) {
        const auto [target, field] = parse(c);
        Track *tr = channelTrack(target, field);
        if (!tr) return;
        const double t = c.contains(QStringLiteral("t")) ? c.value(QStringLiteral("t")).toDouble() : std::floor(m_t);
        tr->keys.erase(std::remove_if(tr->keys.begin(), tr->keys.end(), [t](const XKey &k) { return std::abs(k.t - t) < 0.5; }), tr->keys.end());
        if (tr->keys.isEmpty()) {
            const Track *p = tr;
            m_doc.seq.tracks.erase(std::remove_if(m_doc.seq.tracks.begin(), m_doc.seq.tracks.end(), [p](const Track &x) { return &x == p; }), m_doc.seq.tracks.end());
        }
    };
    // stop animating a value (it keeps the value it has now)
    o[QStringLiteral("channel_clear")] = [this, parse](const QJsonObject &c) {
        const auto [target, field] = parse(c);
        m_doc.seq.tracks.erase(std::remove_if(m_doc.seq.tracks.begin(), m_doc.seq.tracks.end(), [&](const Track &x) {
                                   return x.kind == QLatin1String("channel") && x.target == target && x.field == field; }),
                               m_doc.seq.tracks.end());
    };
    // what can be animated (automation, the Element Viewer)
    o[QStringLiteral("channel_fields")] = [this](const QJsonObject &) {
        QJsonObject all;
        for (auto it = channelDefs().cbegin(); it != channelDefs().cend(); ++it) {
            QJsonArray f;
            for (const ChannelDef &d : it.value()) f.append(QJsonObject{{QStringLiteral("field"), QString::fromUtf8(d.field)}, {QStringLiteral("label"), QString::fromUtf8(d.label)}});
            all.insert(it.key(), f);
        }
        log(QStringLiteral("info"), QString::fromUtf8(QJsonDocument(all).toJson(QJsonDocument::Compact)));
    };
    m_readonly << QStringLiteral("channel_fields");
}
