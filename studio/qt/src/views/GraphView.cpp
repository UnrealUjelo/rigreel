#include "GraphView.h"

#include "IconProvider.h"
#include "TimelineMath.h"

#include <QJsonArray>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace {
const QColor kText(0xd6, 0xd6, 0xdc);
const QColor kMuted(0x8a, 0x8a, 0x94);
constexpr double kPad = 22;
} // namespace

GraphView::GraphView(QQuickItem *parent) : TimeCanvas(parent) { m_header = 230; }

void GraphView::setNormalized(bool n)
{
    if (n == m_normalized) return;
    m_normalized = n;
    emit normalizedChanged();
    computeRanges();
    update();
}

QList<GraphView::Channel> GraphView::channelsOf(const QString &kind) const
{
    if (kind == QLatin1String("xform"))
        return {{QStringLiteral("x"), QStringLiteral("X"), QColor(0xff, 0x6b, 0x6b), QStringLiteral("m")}, {QStringLiteral("y"), QStringLiteral("Y"), QColor(0x7f, 0xd1, 0x8a), QStringLiteral("m")},
                {QStringLiteral("z"), QStringLiteral("Z"), QColor(0x6e, 0xa8, 0xff), QStringLiteral("m")}, {QStringLiteral("yaw"), QStringLiteral("Facing"), QColor(0xf5, 0xa5, 0x24), QStringLiteral("°")}};
    if (kind == QLatin1String("cammove"))
        return {{QStringLiteral("x"), QStringLiteral("X"), QColor(0xff, 0x6b, 0x6b), QStringLiteral("m")}, {QStringLiteral("y"), QStringLiteral("Y"), QColor(0x7f, 0xd1, 0x8a), QStringLiteral("m")},
                {QStringLiteral("z"), QStringLiteral("Z"), QColor(0x6e, 0xa8, 0xff), QStringLiteral("m")}, {QStringLiteral("fov"), QStringLiteral("Field of view"), QColor(0xf5, 0xa5, 0x24), QStringLiteral("°")},
                {QStringLiteral("roll"), QStringLiteral("Roll"), QColor(0xe0, 0x7a, 0xc8), QStringLiteral("°")}};
    if (kind == QLatin1String("channel"))
        return {{QStringLiteral("v"), QStringLiteral("Value"), QColor(0x4f, 0xc8, 0xc0), QString()}};
    return {};
}

QList<QJsonObject> GraphView::keyedTracks() const
{
    QList<QJsonObject> out;
    if (!m_studio) return out;
    for (const QJsonValue &v : m_studio->sequence().value(QStringLiteral("tracks")).toArray()) {
        const QJsonObject t = v.toObject();
        const QString k = t.value(QStringLiteral("kind")).toString();
        if (k == QLatin1String("xform") || k == QLatin1String("cammove") || k == QLatin1String("pose") || k == QLatin1String("channel"))
            out << t;
    }
    return out;
}

QJsonObject GraphView::currentTrack() const
{
    const QList<QJsonObject> ts = keyedTracks();
    for (const QJsonObject &t : ts)
        if (t.value(QStringLiteral("idx")).toInt() == m_studio->graphTrack())
            return t;
    return ts.isEmpty() ? QJsonObject() : ts.first();
}

QString GraphView::trackLabel(const QJsonObject &t) const
{
    const QString kind = t.value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String("channel")) return t.value(QStringLiteral("label")).toString();
    if (kind == QLatin1String("cammove")) {
        for (const QJsonValue &v : m_studio->stateObj().value(QStringLiteral("cameras")).toArray())
            if (v.toObject().value(QStringLiteral("i")).toInt() == t.value(QStringLiteral("cam")).toInt())
                return v.toObject().value(QStringLiteral("name")).toString() + QStringLiteral(" · move");
        return QStringLiteral("Camera move");
    }
    return TL::shortName(t.value(QStringLiteral("actor_name")).toString()) + QStringLiteral(" · ") + TL::kindLabel(kind);
}

void GraphView::onStateChanged()
{
    if (!m_studio) { update(); return; }
    // the left column: tracks and their channels
    m_side.clear();
    const int cur = currentTrack().value(QStringLiteral("idx")).toInt();
    double y = 0;
    for (const QJsonObject &t : keyedTracks()) {
        const int idx = t.value(QStringLiteral("idx")).toInt();
        m_side.append({idx, true, QString(), y, 26});
        y += 26;
        if (idx == cur)
            for (const Channel &c : channelsOf(t.value(QStringLiteral("kind")).toString())) {
                m_side.append({idx, false, c.field, y, 22});
                y += 22;
            }
    }
    setContentHeight(y + 20);
    computeRanges();
    update();
}

void GraphView::computeRanges()
{
    m_ranges.clear();
    if (!m_studio) return;
    const QJsonObject t = currentTrack();
    const QJsonArray keys = t.value(QStringLiteral("keys")).toArray();
    double gmin = 1e18, gmax = -1e18;
    for (const Channel &c : channelsOf(t.value(QStringLiteral("kind")).toString())) {
        double lo = 1e18, hi = -1e18;
        for (const QJsonValue &kv : keys) {
            const double v = TL::channel(kv.toObject(), c.field);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        if (keys.isEmpty()) { lo = 0; hi = 1; }
        const double span = std::max(hi - lo, c.unit == QLatin1String("°") ? 5.0 : 0.5);
        const double mid = (lo + hi) / 2;
        m_ranges.insert(c.field, {mid - span * 0.6, mid + span * 0.6});
        if (!m_hidden.contains(QStringLiteral("%1:%2").arg(t.value(QStringLiteral("idx")).toInt()).arg(c.field))) {
            gmin = std::min(gmin, mid - span * 0.6);
            gmax = std::max(gmax, mid + span * 0.6);
        }
    }
    if (gmin < gmax && (m_vmin == -1 && m_vmax == 1)) { m_vmin = gmin; m_vmax = gmax; }
}

void GraphView::frameValues()
{
    const QJsonObject t = currentTrack();
    double gmin = 1e18, gmax = -1e18;
    for (const Channel &c : channelsOf(t.value(QStringLiteral("kind")).toString())) {
        if (m_hidden.contains(QStringLiteral("%1:%2").arg(t.value(QStringLiteral("idx")).toInt()).arg(c.field))) continue;
        const auto r = m_ranges.value(c.field);
        gmin = std::min(gmin, r.first);
        gmax = std::max(gmax, r.second);
    }
    if (gmin < gmax) { m_vmin = gmin; m_vmax = gmax; }
    frameAll();
    update();
}

double GraphView::yOf(const QString &f, double v) const
{
    double lo = m_vmin, hi = m_vmax;
    if (m_normalized) { const auto r = m_ranges.value(f, {0.0, 1.0}); lo = r.first; hi = r.second; }
    const double top = RulerH + kPad, bottom = height() - kPad;
    return top + (1 - (v - lo) / std::max(1e-9, hi - lo)) * (bottom - top);
}

double GraphView::vOf(const QString &f, double y) const
{
    double lo = m_vmin, hi = m_vmax;
    if (m_normalized) { const auto r = m_ranges.value(f, {0.0, 1.0}); lo = r.first; hi = r.second; }
    const double top = RulerH + kPad, bottom = height() - kPad;
    return lo + (1 - (y - top) / std::max(1.0, bottom - top)) * (hi - lo);
}

GraphView::KeyHit GraphView::hitKey(const QPointF &p) const
{
    KeyHit best;
    const QJsonObject t = currentTrack();
    const int idx = t.value(QStringLiteral("idx")).toInt();
    double bd = 9;
    for (const Channel &c : channelsOf(t.value(QStringLiteral("kind")).toString())) {
        if (m_hidden.contains(QStringLiteral("%1:%2").arg(idx).arg(c.field))) continue;
        for (const QJsonValue &kv : t.value(QStringLiteral("keys")).toArray()) {
            const QJsonObject k = kv.toObject();
            const double kt = k.value(QStringLiteral("t")).toDouble();
            const double v = TL::channel(k, c.field);
            const double d = std::hypot(xOf(kt) - p.x(), yOf(c.field, v) - p.y());
            if (d < bd) { bd = d; best = {true, {idx, 0, kt}, c.field, v, k.value(QStringLiteral("ease")).toString()}; }
        }
    }
    return best;
}

void GraphView::paint(QPainter *p)
{
    if (!m_studio) return;
    const double w = width(), h = height();
    p->setRenderHint(QPainter::Antialiasing, true);
    QFont f = p->font();
    f.setPixelSize(11);
    p->setFont(f);
    const QJsonObject t = currentTrack();
    const QString kind = t.value(QStringLiteral("kind")).toString();
    const int idx = t.value(QStringLiteral("idx")).toInt();
    const QJsonArray keys = t.value(QStringLiteral("keys")).toArray();

    // grid: time columns, value rows
    p->save();
    p->setClipRect(QRectF(m_header, RulerH, w - m_header, h - RulerH));
    int step = 1;
    for (int s : {1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1200, 3000, 6000}) { step = s; if (s * ppf() >= 80) break; }
    p->setPen(QColor(255, 255, 255, 12));
    for (double tt = std::floor(tOf(m_header) / step) * step; xOf(tt) < w; tt += step)
        if (tt >= 0) p->drawLine(QPointF(std::round(xOf(tt)) + 0.5, RulerH), QPointF(std::round(xOf(tt)) + 0.5, h));
    if (!m_normalized) {
        const double span = m_vmax - m_vmin;
        double vstep = std::pow(10.0, std::floor(std::log10(std::max(1e-6, span / 5))));
        if (span / vstep > 10) vstep *= 2;
        p->setPen(QColor(255, 255, 255, 14));
        for (double v = std::ceil(m_vmin / vstep) * vstep; v <= m_vmax; v += vstep) {
            const double y = yOf(QString(), v);
            p->drawLine(QPointF(m_header, y), QPointF(w, y));
            p->setPen(kMuted);
            p->drawText(QPointF(m_header + 4, y - 3), QString::number(v, 'g', 4));
            p->setPen(QColor(255, 255, 255, 14));
        }
        p->setPen(QColor(255, 255, 255, 40));
        p->drawLine(QPointF(m_header, yOf(QString(), 0)), QPointF(w, yOf(QString(), 0)));
    }
    paintRangeShade(p, RulerH);

    if (t.isEmpty()) {
        p->setPen(kMuted);
        p->drawText(QRectF(m_header + 20, RulerH + 30, w - m_header - 40, 80), Qt::TextWordWrap,
                    QStringLiteral("No keyed tracks yet. Key a character's position (K), a camera (M) or a pose, and its curves appear here."));
    } else if (kind == QLatin1String("pose")) {
        p->setPen(kMuted);
        p->drawText(QRectF(m_header + 20, RulerH + 12, w - m_header - 40, 40), Qt::TextWordWrap,
                    QStringLiteral("Pose keys rotate many joints at once, so they have no single curve: retime them here or in the Motion Editor, edit values in the Animation Set Editor."));
        for (const QJsonValue &cv : t.value(QStringLiteral("clips")).toArray())
            for (const QJsonValue &kv : cv.toObject().value(QStringLiteral("keys")).toArray()) {
                const double x = xOf(cv.toObject().value(QStringLiteral("start")).toDouble() + kv.toDouble());
                p->fillRect(QRectF(x - 1, RulerH + 50, 2, h - RulerH - 60), QColor(0xa2, 0x78, 0xe0));
            }
    } else {
        const QList<KeyRef> sel = m_studio->keySel();
        for (const Channel &c : channelsOf(kind)) {
            if (m_hidden.contains(QStringLiteral("%1:%2").arg(idx).arg(c.field))) continue;
            QPainterPath path;
            bool first = true;
            for (double px = m_header; px <= w; px += 2) {
                const double y = yOf(c.field, TL::sample(keys, c.field, tOf(px)));
                if (first) { path.moveTo(px, y); first = false; } else path.lineTo(px, y);
            }
            p->setPen(QPen(c.color, 1.7));
            p->setBrush(Qt::NoBrush);
            p->drawPath(path);
            for (const QJsonValue &kv : keys) {
                const QJsonObject k = kv.toObject();
                const KeyRef ref{idx, 0, k.value(QStringLiteral("t")).toDouble()};
                double kt = ref.t, v = TL::channel(k, c.field);
                const bool selected = sel.contains(ref);
                if (m_drag == Drag::Key && m_moved && selected) {
                    kt += m_dt;
                    if (c.field == m_press.field) v += m_dv;
                }
                const double x = xOf(kt), y = yOf(c.field, v);
                p->setPen(selected ? QPen(c.color, 2) : QPen(c.color.darker(160), 1));
                p->setBrush(selected ? QColor(255, 255, 255) : c.color);
                if (k.value(QStringLiteral("ease")).toString() == QLatin1String("hold"))
                    p->drawRect(QRectF(x - 4, y - 4, 8, 8));
                else
                    p->drawEllipse(QPointF(x, y), selected ? 5.5 : 4.2, selected ? 5.5 : 4.2);
            }
            // value at the playhead, at the left edge of the curve area
            const double cur = TL::sample(keys, c.field, seqT());
            p->setPen(c.color);
            QFont bf = f;
            bf.setBold(true);
            p->setFont(bf);
            p->drawText(QPointF(m_header + 8, std::clamp(yOf(c.field, cur) - 6, double(RulerH + 12), h - 6)),
                        QStringLiteral("%1 %2%3").arg(c.label).arg(cur, 0, 'f', c.unit == QLatin1String("°") ? 1 : 2).arg(c.unit));
            p->setFont(f);
        }
    }
    if (m_drag == Drag::Rubber && m_moved) {
        p->setPen(QPen(QColor(0x7f, 0xbf, 0xff), 1));
        p->setBrush(QColor(0x4a, 0xa8, 0xff, 40));
        p->drawRect(m_rubber);
    }
    p->restore();

    // left column
    p->fillRect(QRectF(0, RulerH, m_header, h - RulerH), QColor(0x24, 0x24, 0x27));
    p->save();
    p->setClipRect(QRectF(0, RulerH, m_header, h - RulerH));
    for (const Side &s : std::as_const(m_side)) {
        const double top = RulerH + s.y - m_vscroll;
        if (top + s.h < RulerH || top > h) continue;
        const QJsonObject tr = [&] { for (const QJsonObject &x : keyedTracks()) if (x.value(QStringLiteral("idx")).toInt() == s.track) return x; return QJsonObject(); }();
        if (s.header) {
            const bool cur = s.track == idx;
            p->fillRect(QRectF(0, top, m_header, s.h), cur ? QColor(0x2f, 0x3b, 0x4d) : QColor(0x2a, 0x2a, 0x2e));
            const QColor kc = TL::kindColor(tr.value(QStringLiteral("kind")).toString());
            p->fillRect(QRectF(8, top + 6, 3, s.h - 12), kc);
            p->setPen(kText);
            QFont bf = f;
            bf.setBold(cur);
            p->setFont(bf);
            p->drawText(QRectF(18, top, m_header - 26, s.h), Qt::AlignVCenter | Qt::AlignLeft, p->fontMetrics().elidedText(trackLabel(tr), Qt::ElideRight, int(m_header - 26)));
            p->setFont(f);
        } else {
            const bool hidden = m_hidden.contains(QStringLiteral("%1:%2").arg(s.track).arg(s.field));
            Channel ch;
            for (const Channel &c : channelsOf(tr.value(QStringLiteral("kind")).toString())) if (c.field == s.field) ch = c;
            p->setPen(Qt::NoPen);
            p->setBrush(hidden ? QColor(0x44, 0x44, 0x48) : ch.color);
            p->drawRoundedRect(QRectF(24, top + 6, 10, 10), 2, 2);
            p->setPen(hidden ? kMuted : kText);
            const double cur = TL::sample(tr.value(QStringLiteral("keys")).toArray(), s.field, seqT());
            p->drawText(QRectF(42, top, m_header - 50, s.h), Qt::AlignVCenter | Qt::AlignLeft, ch.label);
            p->setPen(kMuted);
            p->drawText(QRectF(42, top, m_header - 56, s.h), Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("%1%2").arg(cur, 0, 'f', ch.unit == QLatin1String("°") ? 1 : 3).arg(ch.unit));
        }
        p->fillRect(QRectF(0, top + s.h - 1, m_header, 1), QColor(0x14, 0x14, 0x16));
    }
    p->restore();
    p->fillRect(QRectF(m_header - 1, 0, 1, h), QColor(0x14, 0x14, 0x16));
    p->fillRect(QRectF(0, 0, m_header, RulerH), QColor(0x26, 0x26, 0x29));
    p->setPen(kMuted);
    p->drawText(QRectF(10, 0, m_header - 16, RulerH), Qt::AlignVCenter | Qt::AlignLeft, m_normalized ? QStringLiteral("Channels · normalized") : QStringLiteral("Channels · shared axis"));
    paintRuler(p, false);
    paintPlayhead(p);
}

void GraphView::mousePressEvent(QMouseEvent *e)
{
    if (!m_studio) return;
    const QPointF pos = e->position();
    m_pressPos = pos;
    m_moved = false;
    m_dt = m_dv = 0;
    if (rulerPress(e, false)) return;
    if (e->button() == Qt::MiddleButton) {
        if (e->modifiers() & Qt::AltModifier) { m_drag = Drag::VPan; m_vpanMin = m_vmin; m_vpanMax = m_vmax; }
        else { m_drag = Drag::Pan; panPress(e); }
        return;
    }
    if (pos.x() < m_header) {
        for (const Side &s : std::as_const(m_side)) {
            const double top = RulerH + s.y - m_vscroll;
            if (pos.y() < top || pos.y() >= top + s.h) continue;
            if (s.header) m_studio->setGraphTrack(s.track);
            else {
                const QString key = QStringLiteral("%1:%2").arg(s.track).arg(s.field);
                if (m_hidden.contains(key)) m_hidden.remove(key); else m_hidden.insert(key);
                update();
            }
            return;
        }
        return;
    }
    const KeyHit hit = hitKey(pos);
    if (e->button() == Qt::RightButton) {
        if (hit.ok) {
            QList<KeyRef> sel = m_studio->keySel();
            if (!sel.contains(hit.ref)) m_studio->setKeySel({hit.ref});
            emit menuRequested(QStringLiteral("key"), {{QStringLiteral("items"), m_studio->keySelList()}, {QStringLiteral("ease"), hit.ease}}, pos.x(), pos.y());
        } else {
            emit menuRequested(QStringLiteral("graph"), {{QStringLiteral("t"), std::round(tOf(pos.x()))}}, pos.x(), pos.y());
        }
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    if (hit.ok) {
        QList<KeyRef> sel = m_studio->keySel();
        if (shift) {
            if (sel.contains(hit.ref)) sel.removeAll(hit.ref); else sel.append(hit.ref);
            m_studio->setKeySel(sel);
            return;
        }
        if (!sel.contains(hit.ref)) m_studio->setKeySel({hit.ref});
        m_press = hit;
        m_drag = Drag::Key;
    } else {
        m_drag = Drag::Rubber;
        m_rubberBase = shift ? m_studio->keySel() : QList<KeyRef>();
        m_rubber = QRectF(pos, pos);
    }
}

void GraphView::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_studio) return;
    if (rulerMove(e)) return;
    const QPointF pos = e->position();
    if (m_drag == Drag::Pan) { panMove(e); return; }
    if (m_drag == Drag::VPan) {
        const double per = (m_vpanMax - m_vpanMin) / std::max(1.0, height() - RulerH - 2 * kPad);
        const double d = (pos.y() - m_pressPos.y()) * per;
        m_vmin = m_vpanMin + d;
        m_vmax = m_vpanMax + d;
        update();
        return;
    }
    if (!m_moved && (pos - m_pressPos).manhattanLength() > 3) m_moved = true;
    if (!m_moved) return;
    if (m_drag == Drag::Key) {
        m_dt = std::round(tOf(pos.x()) - tOf(m_pressPos.x()));
        m_dv = vOf(m_press.field, pos.y()) - vOf(m_press.field, m_pressPos.y());
        if (e->modifiers() & Qt::ShiftModifier) m_dt = 0;
        if (e->modifiers() & Qt::ControlModifier) m_dv = 0;
        setHover(QStringLiteral("%1%2").arg(m_dt ? QStringLiteral("%1%2 f").arg(m_dt > 0 ? "+" : "").arg(m_dt) : QString())
                     .arg(m_dv ? QStringLiteral("  %1%2").arg(m_dv > 0 ? "+" : "").arg(m_dv, 0, 'f', 3) : QString()));
        update();
    } else if (m_drag == Drag::Rubber) {
        m_rubber = QRectF(m_pressPos, pos).normalized();
        QList<KeyRef> hits = m_rubberBase;
        const QJsonObject t = currentTrack();
        const int idx = t.value(QStringLiteral("idx")).toInt();
        for (const Channel &c : channelsOf(t.value(QStringLiteral("kind")).toString())) {
            if (m_hidden.contains(QStringLiteral("%1:%2").arg(idx).arg(c.field))) continue;
            for (const QJsonValue &kv : t.value(QStringLiteral("keys")).toArray()) {
                const QJsonObject k = kv.toObject();
                const KeyRef ref{idx, 0, k.value(QStringLiteral("t")).toDouble()};
                if (m_rubber.contains(QPointF(xOf(ref.t), yOf(c.field, TL::channel(k, c.field)))) && !hits.contains(ref))
                    hits << ref;
            }
        }
        m_studio->setKeySel(hits);
        update();
    }
}

void GraphView::mouseReleaseEvent(QMouseEvent *e)
{
    if (!m_studio) return;
    if (rulerRelease(e)) return;
    const Drag d = m_drag;
    m_drag = Drag::None;
    setHover(QString());
    if (d == Drag::Pan) { panRelease(e); return; }
    if (d == Drag::Key) {
        if (!m_moved) {
            m_studio->send(QStringLiteral("seq_seek"), {{QStringLiteral("t"), m_press.ref.t}});
        } else {
            const QJsonObject t = currentTrack();
            const QList<KeyRef> sel = m_studio->keySel();
            if (std::abs(m_dv) > 1e-9) {
                // every selected key of this curve moves by the same amount
                for (const QJsonValue &kv : t.value(QStringLiteral("keys")).toArray()) {
                    const QJsonObject k = kv.toObject();
                    const KeyRef ref{t.value(QStringLiteral("idx")).toInt(), 0, k.value(QStringLiteral("t")).toDouble()};
                    if (!sel.contains(ref)) continue;
                    const double nv = TL::channel(k, m_press.field) + m_dv;
                    m_studio->cmd(QStringLiteral("set_key_value"), {{QStringLiteral("items"), QVariantList{QVariantMap{{QStringLiteral("track"), ref.track}, {QStringLiteral("t"), ref.t}}}},
                                                                  {QStringLiteral("field"), m_press.field}, {QStringLiteral("value"), nv}});
                }
            }
            if (m_dt != 0) {
                m_studio->cmd(QStringLiteral("move_keys"), {{QStringLiteral("items"), m_studio->keySelList()}, {QStringLiteral("dt"), m_dt}});
                QList<KeyRef> moved;
                for (KeyRef k : sel) { k.t = std::max(0.0, k.t + m_dt); moved << k; }
                m_studio->setKeySel(moved);
            }
        }
    } else if (d == Drag::Rubber && !m_moved) {
        m_studio->setKeySel({});
    }
    m_moved = false;
    m_dt = m_dv = 0;
    update();
}

void GraphView::hoverMoveEvent(QHoverEvent *e)
{
    if (m_drag != Drag::None) return;
    const KeyHit h = hitKey(e->position());
    if (h.ok) {
        setCursor(Qt::SizeAllCursor);
        setHover(QStringLiteral("%1 = %2 @ %3 · drag: value / time (Shift: value only, Ctrl: time only) · right-click: interpolation").arg(h.field).arg(h.value, 0, 'f', 3).arg(h.ref.t));
    } else {
        unsetCursor();
        setHover(QString());
    }
}

void GraphView::wheelEvent(QWheelEvent *e)
{
    if ((e->modifiers() & Qt::AltModifier) && !m_normalized) {
        const double d = e->angleDelta().y() ? e->angleDelta().y() : e->angleDelta().x();
        const double f = d > 0 ? 1 / 1.2 : 1.2;
        const double mid = vOf(QString(), e->position().y());
        m_vmin = mid - (mid - m_vmin) * f;
        m_vmax = mid + (m_vmax - mid) * f;
        update();
        e->accept();
        return;
    }
    TimeCanvas::wheelEvent(e);
}
