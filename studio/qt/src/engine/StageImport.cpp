// External animations in the standalone runtime (import.lua): the character's rig for the retargeter, retargeted
// clips onto the timeline, and the film's pose / travel keys out for Blender. Files live under the Studio's own
// folder (Documents/Director Studio); commands name them "director/..." like the game runtime does.
#include "Maths.h"
#include "StageRuntime.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <cmath>

using namespace film;

QString StageRuntime::studioDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/Director Studio");
}

// "director/rigs/x.json" -> <Documents>/Director Studio/rigs/x.json (absolute paths stay)
QString StageRuntime::studioPath(const QString &path) const
{
    if (QDir::isAbsolutePath(path)) return path;
    QString p = path;
    if (p.startsWith(QLatin1String("director/"))) p = p.mid(9);
    return studioDir() + QLatin1Char('/') + p;
}

QString StageRuntime::rigCode(const Actor &a) const
{
    if (!a.castId.isEmpty()) return a.castId;
    QString n = a.name;
    return n.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9]")), QStringLiteral("_"));
}

QStringList StageRuntime::importsFor(const QString &code) const
{
    QStringList out;
    for (const QFileInfo &fi : QDir(studioDir() + QStringLiteral("/import/") + code).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name))
        out << fi.completeBaseName();
    return out;
}

namespace {
bool writeJson(const QString &file, const QJsonObject &o)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    return f.commit();
}
} // namespace

void StageRuntime::registerImportOps()
{
    auto &o = m_ops;
    m_readonly << QStringLiteral("export_rig") << QStringLiteral("export_anim") << QStringLiteral("bake_root_motion");

    // the bind pose of every joint, relative to the character (origin, facing +Z): what retarget.py maps onto
    o[QStringLiteral("export_rig")] = [this](const QJsonObject &c) {
        const Actor *a = argActor(c);
        const PreparedModel *pm = a ? prepared(a->id) : nullptr;
        if (!pm || !pm->model) { log(QStringLiteral("warn"), QStringLiteral("rig: the character is still loading")); return; }
        const dir::Skeleton &sk = pm->model->skeleton;
        const QVector<QMatrix4x4> g = globalMatrices(sk, bindPose(sk));
        QVector<QQuaternion> rot(sk.bones.size());
        QJsonObject joints;
        for (int i = 0; i < sk.bones.size(); ++i) {
            const dir::Bone &b = sk.bones[i];
            rot[i] = (b.parent >= 0 && b.parent < i ? rot[b.parent] * b.rotation : b.rotation).normalized();
            joints.insert(b.name, QJsonObject{{QStringLiteral("parent"), b.parent >= 0 ? QJsonValue(sk.bones[b.parent].name) : QJsonValue()},
                                              {QStringLiteral("pos"), fm::arr(g[i].column(3).toVector3D())}, {QStringLiteral("rot"), fm::arr(rot[i])}});
        }
        const QString code = rigCode(*a);
        const QString rel = QStringLiteral("director/rigs/%1.json").arg(code);
        if (!writeJson(studioPath(rel), QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("name"), a->name}, {QStringLiteral("base_pose"), true},
                                                    {QStringLiteral("joints"), joints}})) {
            log(QStringLiteral("error"), QStringLiteral("rig: could not write %1").arg(studioPath(rel)));
            return;
        }
        m_lastRig = rel;
        log(QStringLiteral("info"), QStringLiteral("rig exported: %1 (%2 joints)").arg(code).arg(sk.bones.size()));
        markData();
    };

    // a retargeted clip: dense pose keys as a pose clip, the hips' travel as position keys from where the character
    // stands and faces now
    o[QStringLiteral("import_anim")] = [this](const QJsonObject &c) {
        Actor *a = argActor(c);
        if (!a) return;
        QFile f(studioPath(c.value(QStringLiteral("file")).toString()));
        if (!f.open(QIODevice::ReadOnly)) { log(QStringLiteral("error"), QStringLiteral("import: cannot read %1").arg(f.fileName())); return; }
        const QJsonObject data = QJsonDocument::fromJson(f.readAll()).object();
        const QJsonArray keys = data.value(QStringLiteral("keys")).toArray();
        if (keys.isEmpty()) { log(QStringLiteral("error"), QStringLiteral("import: no clip data in %1").arg(f.fileName())); return; }
        const double start = std::floor(c.contains(QStringLiteral("start")) ? c.value(QStringLiteral("start")).toDouble() : m_t);
        PoseClip clip;
        clip.id = newId();
        clip.start = start;
        clip.mode = QStringLiteral("override");
        clip.fadeIn = clip.fadeOut = 6;
        clip.loop = c.value(QStringLiteral("loop")).toBool();
        for (const QJsonValue &kv : keys) {
            const QJsonObject k = kv.toObject();
            PoseKey pk;
            pk.t = std::floor(k.value(QStringLiteral("t")).toDouble());
            pk.ease = QStringLiteral("linear");
            const QJsonObject j = k.value(QStringLiteral("joints")).toObject();
            for (auto it = j.begin(); it != j.end(); ++it) pk.joints.insert(it.key(), fm::quat(it.value()).normalized());
            clip.keys << pk;
        }
        std::stable_sort(clip.keys.begin(), clip.keys.end(), [](const PoseKey &x, const PoseKey &y) { return x.t < y.t; });
        clip.dur = std::max(1.0, clip.keys.last().t + 1);
        Track &pt = track(QStringLiteral("pose"), a->id, 0);
        pt.poses << clip;
        std::stable_sort(pt.poses.begin(), pt.poses.end(), [](const PoseClip &x, const PoseClip &y) { return x.start < y.start; });
        int travel = 0;
        const QJsonArray root = data.value(QStringLiteral("root")).toArray();
        if (root.size() > 1 && c.value(QStringLiteral("root")).toBool(true)) {
            const QVector3D p0 = a->pos;
            const double yaw0 = fm::toEulerUpright(a->rot).y();
            const double src0 = root.first().toObject().value(QStringLiteral("yaw")).toDouble();
            const double turn = (yaw0 - src0) * fm::kPi / 180;
            const double cs = std::cos(turn), sn = std::sin(turn);
            const int every = std::max(1, c.value(QStringLiteral("root_every")).toInt(4));
            Track &xt = track(QStringLiteral("xform"), a->id, 0);
            for (int i = 0; i < root.size(); ++i) {
                if (i % every && i != root.size() - 1) continue;
                const QJsonObject k = root[i].toObject();
                const double dx = k.value(QStringLiteral("dx")).toDouble(), dz = k.value(QStringLiteral("dz")).toDouble();
                XKey key;
                key.t = start + std::floor(k.value(QStringLiteral("t")).toDouble());
                key.pos = p0 + QVector3D(float(dx * cs + dz * sn), 0, float(-dx * sn + dz * cs));
                key.rot = fm::fromEulerDeg(0, yaw0 + (k.value(QStringLiteral("yaw")).toDouble(src0) - src0), 0);
                key.ease = QStringLiteral("linear");
                xt.keys.removeIf([&](const XKey &x) { return std::abs(x.t - key.t) < 0.5; });
                xt.keys << key;
                ++travel;
            }
            std::stable_sort(xt.keys.begin(), xt.keys.end(), [](const XKey &x, const XKey &y) { return x.t < y.t; });
        }
        grow(start + clip.dur);
        m_selClip = {};
        log(QStringLiteral("info"), QStringLiteral("imported %1: %2 pose keys, %3 travel keys, %4 frames")
                                        .arg(QFileInfo(f.fileName()).completeBaseName()).arg(clip.keys.size()).arg(travel).arg(int(clip.dur)));
    };

    // the selected character's keyed pose and travel for Blender (tools/anim_import)
    o[QStringLiteral("export_anim")] = [this](const QJsonObject &c) {
        const Actor *a = argActor(c);
        const PreparedModel *pm = a ? prepared(a->id) : nullptr;
        if (!pm || !pm->model) return;
        const dir::Skeleton &sk = pm->model->skeleton;
        QJsonObject joints;
        for (const dir::Bone &b : sk.bones)
            joints.insert(b.name, QJsonObject{{QStringLiteral("parent"), b.parent >= 0 ? QJsonValue(sk.bones[b.parent].name) : QJsonValue()},
                                              {QStringLiteral("pos"), fm::arr(b.translation)}, {QStringLiteral("rot"), fm::arr(b.rotation)}});
        QJsonArray pose, travel;
        for (const Track &tr : std::as_const(m_doc.seq.tracks)) {
            if (tr.actor != a->id) continue;
            if (tr.kind == QLatin1String("pose"))
                for (const PoseClip &pc : tr.poses)
                    for (const PoseKey &k : pc.keys) {
                        QJsonObject j;
                        for (auto it = k.joints.cbegin(); it != k.joints.cend(); ++it) j.insert(it.key(), fm::arr(it.value()));
                        pose.append(QJsonObject{{QStringLiteral("t"), pc.start + k.t}, {QStringLiteral("joints"), j}, {QStringLiteral("ease"), k.ease}});
                    }
            if (tr.kind == QLatin1String("xform"))
                for (const XKey &k : tr.keys) travel.append(QJsonObject{{QStringLiteral("t"), k.t}, {QStringLiteral("pos"), fm::arr(k.pos)}, {QStringLiteral("rot"), fm::arr(k.rot)}});
        }
        const QString name = a->displayName.isEmpty() ? a->name : a->displayName;
        const QString rel = QStringLiteral("director/export/%1_%2.json").arg(QString(name).replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9]")), QStringLiteral("_")),
                                                                           QTime::currentTime().toString(QStringLiteral("HHmmss")));
        const QJsonObject out{{QStringLiteral("rig"), QJsonObject{{QStringLiteral("code"), rigCode(*a)}, {QStringLiteral("joints"), joints}}},
                              {QStringLiteral("fps"), m_doc.seq.fps}, {QStringLiteral("pose"), pose}, {QStringLiteral("travel"), travel}, {QStringLiteral("name"), name}};
        if (!writeJson(studioPath(rel), out)) { log(QStringLiteral("error"), QStringLiteral("export failed: %1").arg(studioPath(rel))); return; }
        m_lastExport = studioPath(rel);
        log(QStringLiteral("info"), QStringLiteral("exported %1 pose keys, %2 travel keys -> %3").arg(pose.size()).arg(travel.size()).arg(m_lastExport));
        markData();
    };

    // the game runtime measures a clip's travel by playing it; here root motion is read straight from the clip
    o[QStringLiteral("bake_root_motion")] = [](const QJsonObject &) {};
}
