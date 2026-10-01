// The manipulator of the standalone viewport (SFM's move / rotate / scale handles). The runtime says where the
// handles are (the selected character or prop, or its selected bone; world or local axes) and applies drags; the
// viewport draws the handles and turns mouse motion into metres, degrees or a scale factor along one axis.
#include "Maths.h"
#include "StageRuntime.h"

#include <cmath>

using namespace film;

namespace {
QJsonArray arr3(const QVector3D &v) { return fm::arr(v); }

// SFM-like snapping steps per setting (1 = off)
double snapMove(int snap) { return snap == 2 ? 0.1 : snap == 3 ? 0.5 : snap == 4 ? 1.0 : 0.0; }
double snapTurn(int snap) { return snap == 2 ? 15.0 : snap == 3 ? 45.0 : snap == 4 ? 90.0 : 0.0; }
double snapped(double v, double step) { return step > 0 ? std::round(v / step) * step : v; }
} // namespace

// where the handles sit this frame: {kind: actor | bone, pos, axes [x, y, z]} or empty
QJsonObject StageRuntime::gizmoFrame() const
{
    if (!m_gizmo.value(QStringLiteral("enabled")).toBool(true) || m_rendering) return {};
    const Actor *a = m_doc.actor(m_selActor);
    const ActorFrame *f = a ? frameOf(a->id) : nullptr;
    if (!a || !f) return {};
    const bool local = m_gizmo.value(QStringLiteral("mode")).toString() == QLatin1String("local");
    const bool bone = m_gizmo.value(QStringLiteral("target")).toString() == QLatin1String("bone") && !a->workJoint.isEmpty();
    QVector3D pos = f->pos;
    QQuaternion rot = f->rot;
    if (bone && !jointWorld(*f, a->workJoint, &pos, &rot)) return {};
    // bones turn about their own axes unless World is chosen; characters and props move in world axes by default
    const QQuaternion axes = local || (bone && m_gizmo.value(QStringLiteral("mode")).toString() != QLatin1String("world")) ? rot : QQuaternion();
    const QString ik = bone ? effectorChain(a->id, a->workJoint) : QString();
    const QQuaternion shown = !ik.isEmpty() && m_gizmo.value(QStringLiteral("op")).toString() == QLatin1String("move") && !local ? QQuaternion() : axes;
    return {{QStringLiteral("kind"), bone ? QStringLiteral("bone") : QStringLiteral("actor")}, {QStringLiteral("pos"), arr3(pos)}, {QStringLiteral("ik"), ik},
            {QStringLiteral("axes"), QJsonArray{arr3(shown.rotatedVector({1, 0, 0})), arr3(shown.rotatedVector({0, 1, 0})), arr3(shown.rotatedVector({0, 0, 1}))}},
            {QStringLiteral("joint"), bone ? a->workJoint : QString()}};
}

void StageRuntime::registerGizmoOps()
{
    auto &o = m_ops;
    m_readonly << QStringLiteral("gizmo_drag") << QStringLiteral("gizmo_end");

    // a drag starts: remember where everything was (the whole drag is one undo step)
    o[QStringLiteral("gizmo_begin")] = [this](const QJsonObject &c) {
        const QJsonObject frame = gizmoFrame();
        Actor *a = m_doc.actor(m_selActor);
        if (!a || frame.isEmpty()) { m_gizmoDrag = {}; return; }
        GizmoDrag d;
        d.on = true;
        d.actor = a->id;
        d.op = c.value(QStringLiteral("kind")).toString(m_gizmo.value(QStringLiteral("op")).toString(QStringLiteral("move")));
        d.axis = std::clamp(c.value(QStringLiteral("axis")).toInt(), 0, 3);
        const QJsonArray axes = frame.value(QStringLiteral("axes")).toArray();
        d.dir = d.axis < 3 ? fm::vec3(axes.at(d.axis)) : QVector3D(0, 1, 0);
        d.joint = frame.value(QStringLiteral("joint")).toString();
        const ActorFrame *f = frameOf(a->id);
        d.pos = f ? f->pos : a->pos;
        d.rot = f ? f->rot : a->rot;
        d.scale = a->scale;
        if (!d.joint.isEmpty() && f) {
            // the joint's world turn and its parent's, from the pose on screen
            const PreparedModel *pm = prepared(a->id);
            const int b = pm && pm->model ? findJoint(pm->model->skeleton, d.joint) : -1;
            const int p = b >= 0 ? pm->model->skeleton.bones[b].parent : -1;
            QQuaternion jr, pr = f->rot;
            jointWorld(*f, d.joint, nullptr, &jr);
            if (p >= 0) jointWorld(*f, pm->model->skeleton.bones[p].name, nullptr, &pr);
            d.jointWorld = jr;
            d.parentWorld = pr;
            // Move on a hand or foot: the limb follows by IK from the pose on screen
            if (d.op == QLatin1String("move") && !frame.value(QStringLiteral("ik")).toString().isEmpty()) {
                d.ikChain = frame.value(QStringLiteral("ik")).toString();
                jointWorld(*f, d.joint, &d.ikStart, nullptr);
                d.startPose = f->pose;
            }
        }
        m_gizmoDrag = d;
    };

    // amount: metres along the axis (move), degrees about it (rotate), a factor (scale)
    o[QStringLiteral("gizmo_drag")] = [this](const QJsonObject &c) {
        GizmoDrag &d = m_gizmoDrag;
        Actor *a = d.on ? m_doc.actor(d.actor) : nullptr;
        if (!a) return;
        const int snap = m_gizmo.value(QStringLiteral("snap")).toInt(1);
        const double amount = c.value(QStringLiteral("amount")).toDouble();
        if (!d.ikChain.isEmpty()) {
            QMap<QString, QQuaternion> solved;
            if (solveIkPose(a->id, d.startPose, d.ikChain, d.ikStart + d.dir * float(snapped(amount, snapMove(snap))), &solved))
                for (auto it = solved.cbegin(); it != solved.cend(); ++it) a->work.insert(it.key(), it.value());
            return;
        }
        if (!d.joint.isEmpty()) {
            // a bone turns about the axis in world space; its local rotation goes into the working pose
            const QQuaternion turn = QQuaternion::fromAxisAndAngle(d.dir, float(snapped(amount, snapTurn(snap))));
            const QQuaternion local = (d.parentWorld.inverted() * turn * d.jointWorld).normalized();
            a->work.insert(d.joint, local);
            a->workJoint = d.joint;
            return;
        }
        if (d.op == QLatin1String("rotate")) {
            a->rot = (QQuaternion::fromAxisAndAngle(d.dir, float(snapped(amount, snapTurn(snap)))) * d.rot).normalized();
            a->pos = d.pos;
        } else if (d.op == QLatin1String("scale")) {
            const float k = float(std::clamp(amount, 0.05, 20.0));
            a->scale = d.axis < 3 ? d.scale + (d.scale * (k - 1)) * QVector3D(d.axis == 0, d.axis == 1, d.axis == 2) : d.scale * k;
        } else {
            a->pos = d.pos + d.dir * float(snapped(amount, snapMove(snap)));
            a->rot = d.rot;
        }
        // a keyed character keeps the change at the playhead (as typing it in the fields does)
        bool keyed = false;
        for (const Track &tr : std::as_const(m_doc.seq.tracks)) if (tr.kind == QLatin1String("xform") && tr.actor == a->id && !tr.keys.isEmpty()) keyed = true;
        if (d.op != QLatin1String("scale") && (keyed || m_autokey)) keyTransform(*a, std::floor(m_t), &a->pos, &a->rot);
    };

    o[QStringLiteral("gizmo_end")] = [this](const QJsonObject &) {
        GizmoDrag &d = m_gizmoDrag;
        Actor *a = d.on ? m_doc.actor(d.actor) : nullptr;
        if (a && !d.joint.isEmpty() && m_autokey) {
            QMap<QString, QQuaternion> keys{{d.joint, a->work.value(d.joint)}};
            if (!d.ikChain.isEmpty())
                for (const QString &n : a->work.keys()) keys.insert(n, a->work.value(n));
            keyframePose(*a, std::floor(m_t), keys);
        }
        m_gizmoDrag = {};
    };
}
