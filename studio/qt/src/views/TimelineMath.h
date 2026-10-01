// Shared bits for the time-based views: easing (identical to the runtime's pose.lua), colours, small helpers.
#pragma once

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <algorithm>
#include <cmath>

namespace TL {

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

// track colours (SFM-like: clips are saturated blocks on a dark lane)
inline QColor kindColor(const QString &kind)
{
    if (kind == QLatin1String("anim")) return QColor(0xd0, 0x8a, 0x3c);
    if (kind == QLatin1String("pose")) return QColor(0xa2, 0x78, 0xe0);
    if (kind == QLatin1String("xform")) return QColor(0x4f, 0xa8, 0xe0);
    if (kind == QLatin1String("camera") || kind == QLatin1String("cammove")) return QColor(0xe0, 0xc0, 0x4a);
    if (kind == QLatin1String("path")) return QColor(0x7f, 0xd1, 0x8a);
    if (kind == QLatin1String("audio")) return QColor(0x6c, 0xc2, 0x4a);
    if (kind == QLatin1String("film")) return QColor(0x8c, 0x98, 0xa8);
    if (kind == QLatin1String("channel")) return QColor(0x4f, 0xc8, 0xc0);
    return QColor(0x9a, 0x9a, 0x9a);
}

inline QString kindLabel(const QString &kind)
{
    if (kind == QLatin1String("anim")) return QStringLiteral("Animation");
    if (kind == QLatin1String("pose")) return QStringLiteral("Pose");
    if (kind == QLatin1String("xform")) return QStringLiteral("Position & rotation");
    if (kind == QLatin1String("camera")) return QStringLiteral("Camera cuts");
    if (kind == QLatin1String("cammove")) return QStringLiteral("Camera move");
    if (kind == QLatin1String("path")) return QStringLiteral("Walk path");
    if (kind == QLatin1String("channel")) return QStringLiteral("Channel");
    return kind;
}

// channel value of an xform / cammove key
inline double channel(const QJsonObject &k, const QString &f)
{
    const QJsonArray p = k.value(QStringLiteral("pos")).toArray();
    if (f == QLatin1String("x")) return p.at(0).toDouble();
    if (f == QLatin1String("y")) return p.at(1).toDouble();
    if (f == QLatin1String("z")) return p.at(2).toDouble();
    return k.value(f).toDouble();
}

// sample a channel at time t with the runtime's per-key easing (angles take the short way round)
inline double sample(const QJsonArray &keys, const QString &f, double t)
{
    if (keys.isEmpty()) return 0;
    const QJsonObject first = keys.first().toObject(), last = keys.last().toObject();
    if (t <= first.value(QStringLiteral("t")).toDouble()) return channel(first, f);
    if (t >= last.value(QStringLiteral("t")).toDouble()) return channel(last, f);
    int i = 0;
    while (i < keys.size() - 1 && keys[i + 1].toObject().value(QStringLiteral("t")).toDouble() <= t) ++i;
    const QJsonObject k0 = keys[i].toObject(), k1 = keys[i + 1].toObject();
    const double t0 = k0.value(QStringLiteral("t")).toDouble(), t1 = k1.value(QStringLiteral("t")).toDouble();
    const double s = ease(k0.value(QStringLiteral("ease")).toString(), (t - t0) / std::max(1e-6, t1 - t0));
    const double a = channel(k0, f);
    double b = channel(k1, f);
    if (f == QLatin1String("yaw")) { double d = b - a; while (d > 180) d -= 360; while (d < -180) d += 360; b = a + d; }
    return a + (b - a) * s;
}

inline QString shortName(QString n)
{
    if (n.startsWith(QLatin1String("Director_"))) n = n.mid(9);
    n.replace(QLatin1Char('_'), QLatin1Char(' '));
    return n.simplified();
}

} // namespace TL
