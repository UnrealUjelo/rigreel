// Small maths shared by the standalone runtime. Euler angles follow glm (what REFramework's Quaternion uses), so
// values typed in the Studio mean the same thing in both runtimes; easing matches pose.lua.
#pragma once

#include <QJsonArray>
#include <QQuaternion>
#include <QString>
#include <QVector3D>
#include <algorithm>
#include <cmath>

namespace fm {

constexpr double kPi = 3.14159265358979323846;

// glm::quat(vec3 eulerRadians)
inline QQuaternion fromEulerDeg(double xd, double yd, double zd)
{
    const double r = kPi / 180.0;
    const double cx = std::cos(xd * r * 0.5), cy = std::cos(yd * r * 0.5), cz = std::cos(zd * r * 0.5);
    const double sx = std::sin(xd * r * 0.5), sy = std::sin(yd * r * 0.5), sz = std::sin(zd * r * 0.5);
    return QQuaternion(float(cx * cy * cz + sx * sy * sz), float(sx * cy * cz - cx * sy * sz),
                       float(cx * sy * cz + sx * cy * sz), float(cx * cy * sz - sx * sy * cz));
}

// glm::eulerAngles(q) in degrees
inline QVector3D toEulerDeg(const QQuaternion &qin)
{
    const QQuaternion q = qin.normalized();
    const double w = q.scalar(), x = q.x(), y = q.y(), z = q.z();
    const double py = 2 * (y * z + w * x), px = w * w - x * x - y * y + z * z;
    const double pitch = (std::abs(px) < 1e-9 && std::abs(py) < 1e-9) ? 2 * std::atan2(x, w) : std::atan2(py, px);
    const double yaw = std::asin(std::clamp(-2 * (x * z - w * y), -1.0, 1.0));
    const double roll = std::atan2(2 * (x * y + w * z), w * w + x * x - y * y - z * z);
    const double d = 180.0 / kPi;
    return QVector3D(float(pitch * d), float(yaw * d), float(roll * d));
}

// The same rotation as toEulerDeg, but with pitch and roll kept within 90 degrees: glm reports a character turned
// past 90 degrees as (180, 180 - yaw, 180), which makes a "Facing" slider read and drag backwards.
// Rx(x)Ry(y)Rz(z) == Rx(x + 180)Ry(180 - y)Rz(z + 180).
inline QVector3D toEulerUpright(const QQuaternion &q)
{
    QVector3D e = toEulerDeg(q);
    if (std::abs(e.x()) > 90.f && std::abs(e.z()) > 90.f) {
        float y = 180.f - e.y();
        if (y > 180.f) y -= 360.f;
        e = QVector3D(e.x() - std::copysign(180.f, e.x()), y, e.z() - std::copysign(180.f, e.z()));
    }
    return e;
}

// slerp along the short path
inline QQuaternion slerp(const QQuaternion &a, QQuaternion b, float t)
{
    if (QQuaternion::dotProduct(a, b) < 0) b = -b;
    return QQuaternion::slerp(a, b, t).normalized();
}

// pose.lua Pose.ease: the outgoing key's ease shapes the segment to the next key
inline double ease(const QString &kind, double s)
{
    s = std::clamp(s, 0.0, 1.0);
    if (kind == QLatin1String("linear")) return s;
    if (kind == QLatin1String("hold")) return 0;
    if (kind == QLatin1String("in")) return s * s * s;
    if (kind == QLatin1String("out")) { const double u = 1 - s; return 1 - u * u * u; }
    if (kind == QLatin1String("cubic")) return s < 0.5 ? 4 * s * s * s : 1 - std::pow(-2 * s + 2, 3) / 2;
    return s * s * (3 - 2 * s);
}

// camera looking from eye to target (-Z forward, +Y up), like engine.lua E.look_rotation
inline QQuaternion lookRotation(const QVector3D &eye, const QVector3D &target, QVector3D up = QVector3D(0, 1, 0))
{
    QVector3D f = target - eye;
    if (f.length() < 1e-5f) return QQuaternion();
    f.normalize();
    if (std::abs(QVector3D::dotProduct(f, up)) > 0.999f) up = QVector3D(0, 0, 1);
    const QVector3D right = QVector3D::crossProduct(f, up).normalized();
    const QVector3D nup = QVector3D::crossProduct(right, f).normalized();
    return QQuaternion::fromAxes(right, nup, -f).normalized();
}

// forward (-Z), right (+X), up (+Y) of a rotation
inline QVector3D forward(const QQuaternion &q) { return q.rotatedVector(QVector3D(0, 0, -1)); }
// the colour of a black body at `kelvin` (Tanner Helland's fit), 0..1
inline QVector3D kelvinColor(double kelvin)
{
    const double t = std::clamp(kelvin, 1000.0, 40000.0) / 100.0;
    const double r = t <= 66 ? 255 : 329.698727446 * std::pow(t - 60, -0.1332047592);
    const double g = t <= 66 ? 99.4708025861 * std::log(t) - 161.1195681661 : 288.1221695283 * std::pow(t - 60, -0.0755148492);
    const double b = t >= 66 ? 255 : t <= 19 ? 0 : 138.5177312231 * std::log(t - 10) - 305.0447927307;
    return QVector3D(float(std::clamp(r, 0.0, 255.0) / 255), float(std::clamp(g, 0.0, 255.0) / 255), float(std::clamp(b, 0.0, 255.0) / 255));
}
inline QVector3D right(const QQuaternion &q) { return q.rotatedVector(QVector3D(1, 0, 0)); }
inline QVector3D up(const QQuaternion &q) { return q.rotatedVector(QVector3D(0, 1, 0)); }

// JSON helpers: vectors as [x, y, z], quaternions as [w, x, y, z] (the Lua runtime's layout)
inline QJsonArray arr(const QVector3D &v) { return {double(v.x()), double(v.y()), double(v.z())}; }
inline QJsonArray arr(const QQuaternion &q) { return {double(q.scalar()), double(q.x()), double(q.y()), double(q.z())}; }
inline QVector3D vec3(const QJsonValue &v, const QVector3D &def = {})
{
    const QJsonArray a = v.toArray();
    return a.size() >= 3 ? QVector3D(float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble())) : def;
}
inline QQuaternion quat(const QJsonValue &v)
{
    const QJsonArray a = v.toArray();
    return a.size() >= 4 ? QQuaternion(float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble()), float(a[3].toDouble())).normalized() : QQuaternion();
}

} // namespace fm
