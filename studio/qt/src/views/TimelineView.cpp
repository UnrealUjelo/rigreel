#include "TimelineView.h"

#include "GuideAudio.h"
#include "IconProvider.h"
#include "TimelineMath.h"

#include <QJsonArray>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>

#include <cmath>
#include <map>

namespace {

// Blender's timeline / dope sheet greys
const QColor kHeaderBg(0x30, 0x30, 0x30);
const QColor kHeaderSel(0x33, 0x4d, 0x80);
const QColor kLaneA(0x2b, 0x2b, 0x2b);
const QColor kLaneB(0x2e, 0x2e, 0x2e);
const QColor kGroupLane(0x35, 0x35, 0x35);
const QColor kLine(0x1d, 0x1d, 0x1d);
const QColor kText(0xe5, 0xe5, 0xe5);
const QColor kMuted(0xa5, 0xa5, 0xa5);
const QColor kSelect(0xff, 0xd0, 0x5a);

QString humanize(QString s)
{
    s.remove(QRegularExpression(QStringLiteral("\\.[a-z0-9]+$"), QRegularExpression::CaseInsensitiveOption));
    s.replace(QRegularExpression(QStringLiteral("[_\\-]+")), QStringLiteral(" "));
    s = s.simplified();
    if (!s.isEmpty())
        s[0] = s[0].toUpper();
    return s;
}

// friendly names for RE4's identifiers (the same rules as the Studio's Names.js)
QString friendlyActor(const QString &name, bool isPlayer)
{
    if (name.startsWith(QLatin1String("Director_")))
        return name.mid(9).replace(QLatin1Char('_'), QLatin1Char(' ')).simplified();
    static const QHash<QString, QString> chars{{QStringLiteral("cha0"), QStringLiteral("Leon")}, {QStringLiteral("cha1"), QStringLiteral("Ashley")},
                                               {QStringLiteral("cha2"), QStringLiteral("Ada")}, {QStringLiteral("cha3"), QStringLiteral("Luis")},
                                               {QStringLiteral("cha8"), QStringLiteral("Ada (Separate Ways)")}, {QStringLiteral("chb0"), QStringLiteral("Merchant")},
                                               {QStringLiteral("chc0"), QStringLiteral("Ganado (village)")}};
    static const QHash<QString, QString> roles{{QStringLiteral("0"), QStringLiteral("player")}, {QStringLiteral("1"), QStringLiteral("enemy")},
                                               {QStringLiteral("2"), QStringLiteral("companion")}, {QStringLiteral("3"), QStringLiteral("player")},
                                               {QStringLiteral("8"), QStringLiteral("mercenaries")}};
    static const QRegularExpression re(QStringLiteral("^ch(\\d)([a-z]\\d)z(\\d)"), QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(name);
    if (!m.hasMatch())
        return humanize(name);
    const QString code = QStringLiteral("ch") + m.captured(2).toLower();
    const QString role = roles.value(m.captured(1));
    const QString label = chars.value(code, role == QLatin1String("enemy") ? QStringLiteral("Enemy ") + m.captured(2).toUpper() : QStringLiteral("Character ") + m.captured(2).toUpper());
    if (isPlayer)
        return label;
    return !role.isEmpty() && role != QLatin1String("player") ? label + QStringLiteral(" · ") + role : label;
}

QString motionLabel(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("^(?:ch[a-z0-9]+_)?(?:[a-z0-9]+_)?(\\d{3,4})_(.+)$"), QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(name);
    return m.hasMatch() ? humanize(m.captured(2)) : humanize(name);
}

int rank(const QString &k)
{
    static const QHash<QString, int> r{{QStringLiteral("anim"), 0}, {QStringLiteral("pose"), 1}, {QStringLiteral("xform"), 2}, {QStringLiteral("path"), 3},
                                       {QStringLiteral("camera"), 4}, {QStringLiteral("cammove"), 5}, {QStringLiteral("channel"), 6}};
    return r.value(k, 9);
}

void elidedText(QPainter *p, const QRectF &r, const QString &s, int flags = Qt::AlignVCenter | Qt::AlignLeft)
{
    if (r.width() < 8)
        return;
    p->drawText(r, flags, p->fontMetrics().elidedText(s, Qt::ElideRight, int(r.width())));
}

} // namespace

TimelineView::TimelineView(QQuickItem *parent) : TimeCanvas(parent) {}

void TimelineView::setMode(const QString &m)
{
    if (m == m_mode)
        return;
    m_mode = m;
    emit modeChanged();
    onStateChanged();
}

void TimelineView::setSelectedShot(int id)
{
    if (id == m_selShot)
        return;
    m_selShot = id;
    emit selectedShotChanged();
    update();
}

void TimelineView::setHover(const QString &s)
{
    if (s != m_hover) {
        m_hover = s;
        emit hoverTextChanged();
    }
}

void TimelineView::geometryChange(const QRectF &n, const QRectF &o)
{
    TimeCanvas::geometryChange(n, o);
    setVscroll(m_vscroll);
}

void TimelineView::onStateChanged()
{
    buildRows();
    update();
}

void TimelineView::collapseAll(bool collapsed)
{
    m_collapsed.clear();
    if (collapsed)
        for (const Row &r : std::as_const(m_rows))
            if (r.kind == Row::Group)
                m_collapsed.insert(r.key);
    onStateChanged();
}

QString TimelineView::cameraName(int i) const
{
    if (!m_studio)
        return {};
    for (const QJsonValue &v : m_studio->stateObj().value(QStringLiteral("cameras")).toArray())
        if (v.toObject().value(QStringLiteral("i")).toInt() == i)
            return v.toObject().value(QStringLiteral("name")).toString();
    return QStringLiteral("camera %1").arg(i);
}

QString TimelineView::actorLabel(double addr) const
{
    for (const QJsonValue &v : m_studio->stateObj().value(QStringLiteral("actors")).toArray()) {
        const QJsonObject a = v.toObject();
        if (a.value(QStringLiteral("id")).toDouble() == addr)
            return friendlyActor(a.value(QStringLiteral("name")).toString(), a.value(QStringLiteral("is_player")).toBool());
    }
    return {};
}

QList<KeyRef> TimelineView::trackKeys(const QJsonObject &t) const
{
    QList<KeyRef> out;
    const QString kind = t.value(QStringLiteral("kind")).toString();
    const int idx = t.value(QStringLiteral("idx")).toInt();
    if (kind == QLatin1String("xform") || kind == QLatin1String("cammove") || kind == QLatin1String("channel")) {
        for (const QJsonValue &k : t.value(QStringLiteral("keys")).toArray())
            out.append({idx, 0, k.toObject().value(QStringLiteral("t")).toDouble()});
    } else if (kind == QLatin1String("pose")) {
        for (const QJsonValue &cv : t.value(QStringLiteral("clips")).toArray()) {
            const QJsonObject c = cv.toObject();
            for (const QJsonValue &k : c.value(QStringLiteral("keys")).toArray())
                out.append({idx, c.value(QStringLiteral("id")).toInt(), c.value(QStringLiteral("start")).toDouble() + k.toDouble()});
        }
    }
    return out;
}

QVariantList TimelineView::allKeys() const
{
    QVariantList out;
    if (!m_studio)
        return out;
    for (const QJsonValue &tv : m_studio->sequence().value(QStringLiteral("tracks")).toArray())
        for (const KeyRef &k : trackKeys(tv.toObject())) {
            QVariantMap m{{QStringLiteral("track"), k.track}, {QStringLiteral("t"), k.t}};
            if (k.id) m.insert(QStringLiteral("id"), k.id);
            out.append(m);
        }
    return out;
}

bool TimelineView::keySelected(const KeyRef &k) const { return m_studio && m_studio->keySel().contains(k); }

double TimelineView::ghostT(const KeyRef &k) const
{
    if (m_drag != Drag::Keys || !m_moved || !m_dragKeys.contains(k))
        return -1e9;
    if (m_scaleKeys)
        return m_scalePivot + (k.t - m_scalePivot) * m_scaleFactor;
    return std::max(0.0, k.t + m_dt);
}

// ---------------------------------------------------------------------------------------------- rows
void TimelineView::buildRows()
{
    m_rows.clear();
    if (!m_studio) {
        setContentHeight(0);
        return;
    }
    const bool motion = m_mode == QLatin1String("motion");
    const QJsonObject st = m_studio->stateObj();
    const QJsonObject seq = m_studio->sequence();
    const QJsonArray tracks = seq.value(QStringLiteral("tracks")).toArray();
    const QJsonObject sel = st.value(QStringLiteral("selection")).toObject();
    const double selActor = sel.value(QStringLiteral("actor")).toDouble();
    QSet<double> multi;
    for (const QJsonValue &v : sel.value(QStringLiteral("multi")).toArray())
        multi.insert(v.toDouble());

    // distance of scene characters (for ordering)
    QHash<double, double> dist;
    for (const QJsonValue &v : m_studio->dataObj().value(QStringLiteral("scene")).toArray())
        dist.insert(v.toObject().value(QStringLiteral("addr")).toDouble(), v.toObject().value(QStringLiteral("dist")).toDouble());

    struct Who { QString label; double dist; bool player; bool object; };
    QList<double> order;
    QHash<double, Who> chars;
    for (const QJsonValue &v : st.value(QStringLiteral("actors")).toArray()) {
        const QJsonObject a = v.toObject();
        const double id = a.value(QStringLiteral("id")).toDouble();
        chars.insert(id, {friendlyActor(a.value(QStringLiteral("name")).toString(), a.value(QStringLiteral("is_player")).toBool()), dist.value(id, -1),
                          a.value(QStringLiteral("is_player")).toBool(), a.value(QStringLiteral("kind")).toString() == QLatin1String("object")});
        order << id;
    }
    QList<QJsonObject> camTracks, lightTracks, orphan;
    QHash<double, QList<QJsonObject>> byActor;
    for (const QJsonValue &tv : tracks) {
        const QJsonObject t = tv.toObject();
        const QString kind = t.value(QStringLiteral("kind")).toString();
        const double actor = t.value(QStringLiteral("actor")).toDouble();
        if (kind == QLatin1String("camera") || kind == QLatin1String("cammove") || (kind == QLatin1String("channel") && t.contains(QStringLiteral("cam")))) { camTracks << t; continue; }
        if (actor && !chars.contains(actor)) {
            const QString an = t.value(QStringLiteral("actor_name")).toString();
            chars.insert(actor, {an.isEmpty() ? QStringLiteral("missing character") : friendlyActor(an, false), -1, false, false});
            order << actor;
        }
        if (kind == QLatin1String("channel") && (t.value(QStringLiteral("target")).toString().startsWith(QLatin1String("light:")) || t.value(QStringLiteral("target")).toString() == QLatin1String("world"))) { lightTracks << t; continue; }
        if (actor) byActor[actor] << t; else orphan << t;
    }
    std::stable_sort(order.begin(), order.end(), [&](double a, double b) {
        const Who &wa = chars[a], &wb = chars[b];
        if (wa.player != wb.player) return wa.player;
        return (wa.dist < 0 ? 999 : wa.dist) < (wb.dist < 0 ? 999 : wb.dist);
    });
    auto sortTracks = [](QList<QJsonObject> &l) {
        std::stable_sort(l.begin(), l.end(), [](const QJsonObject &a, const QJsonObject &b) {
            const int ra = rank(a.value(QStringLiteral("kind")).toString()), rb = rank(b.value(QStringLiteral("kind")).toString());
            if (ra != rb) return ra < rb;
            return a.value(QStringLiteral("layer")).toInt() + a.value(QStringLiteral("cam")).toInt() < b.value(QStringLiteral("layer")).toInt() + b.value(QStringLiteral("cam")).toInt();
        });
    };
    auto keep = [&](const QJsonObject &t) {
        if (!motion) return true;
        const QString k = t.value(QStringLiteral("kind")).toString();
        return k == QLatin1String("xform") || k == QLatin1String("cammove") || k == QLatin1String("pose") || k == QLatin1String("channel");
    };
    const double trackH = motion ? 22 : 26;
    double y = 0;
    auto push = [&](Row r) { r.y = y; y += r.h; m_rows.append(r); };
    auto addGroup = [&](Row g, const QList<QJsonObject> &ts) {
        g.collapsed = m_collapsed.contains(g.key);
        for (const QJsonObject &t : ts)
            for (const KeyRef &k : trackKeys(t)) g.summary << k.t;
        push(g);
        if (g.collapsed) return;
        for (const QJsonObject &t : ts) {
            if (!keep(t)) continue;
            Row r;
            r.kind = Row::Track;
            r.key = QStringLiteral("t:") + QString::number(t.value(QStringLiteral("idx")).toInt());
            r.track = t;
            r.h = trackH;
            r.actor = t.value(QStringLiteral("actor")).toDouble();
            r.selected = m_studio->graphTrack() == t.value(QStringLiteral("idx")).toInt();
            push(r);
        }
    };

    if (!motion) {
        Row film;
        film.kind = Row::Film;
        film.key = QStringLiteral("~film");
        film.label = QStringLiteral("Film");
        film.icon = QStringLiteral("clapperboard");
        film.h = 34;
        push(film);
    }
    sortTracks(camTracks);
    {
        Row g;
        g.kind = Row::Group;
        g.key = QStringLiteral("~camera");
        g.label = QStringLiteral("Cameras");
        g.icon = QStringLiteral("video");
        g.isCamera = true;
        addGroup(g, camTracks);
    }
    if (!lightTracks.isEmpty()) {
        Row g;
        g.kind = Row::Group;
        g.key = QStringLiteral("~lights");
        g.label = QStringLiteral("Lights");
        g.icon = QStringLiteral("lightbulb");
        addGroup(g, lightTracks);
    }
    const QJsonObject audio = seq.value(QStringLiteral("audio")).toObject();
    if (!motion && !audio.isEmpty()) {
        Row a;
        a.kind = Row::Audio;
        a.key = QStringLiteral("~audio");
        a.label = audio.value(QStringLiteral("name")).toString(QStringLiteral("Audio"));
        a.icon = QStringLiteral("audio-waveform");
        a.h = 44;
        push(a);
    }
    for (double addr : std::as_const(order)) {
        QList<QJsonObject> ts = byActor.value(addr);
        sortTracks(ts);
        const Who &w = chars[addr];
        Row g;
        g.kind = Row::Group;
        g.key = QString::number(addr, 'f', 0);
        g.label = w.label;
        g.sub = w.dist >= 0 ? QStringLiteral("%1 m").arg(w.dist, 0, 'f', 0) : QString();
        g.icon = w.object ? QStringLiteral("box") : QStringLiteral("user");
        g.actor = addr;
        g.selected = addr == selActor || multi.contains(addr);
        addGroup(g, ts);
    }
    for (const QJsonObject &t : std::as_const(orphan)) {
        if (!keep(t)) continue;
        Row r;
        r.kind = Row::Track;
        r.key = QStringLiteral("o:") + QString::number(t.value(QStringLiteral("idx")).toInt());
        r.track = t;
        r.h = trackH;
        push(r);
    }
    setContentHeight(y + 40);
}

// ---------------------------------------------------------------------------------------------- painting
void TimelineView::paintKey(QPainter *p, double x, double cy, const QColor &c, bool sel, const QString &ease, double size)
{
    const double r = size / 2;
    QPainterPath path;
    if (ease == QLatin1String("hold")) {
        path.addRect(QRectF(x - r * 0.8, cy - r * 0.8, r * 1.6, r * 1.6));
    } else {
        path.moveTo(x, cy - r);
        path.lineTo(x + r, cy);
        path.lineTo(x, cy + r);
        path.lineTo(x - r, cy);
        path.closeSubpath();
    }
    Q_UNUSED(c);
    const QColor fill = sel ? QColor(0xff, 0xbe, 0x33) : QColor(0xe8, 0xe8, 0xe8);
    p->setPen(QPen(QColor(0x10, 0x10, 0x10), 1));
    p->setBrush(ease == QLatin1String("linear") ? QColor(0x2b, 0x2b, 0x2b) : fill);
    p->drawPath(path);
    if (ease == QLatin1String("linear")) { p->setPen(QPen(fill, 1.6)); p->setBrush(Qt::NoBrush); p->drawPath(path); }
}

void TimelineView::paint(QPainter *p)
{
    if (!m_studio)
        return;
    const double w = width(), h = height();
    p->setRenderHint(QPainter::Antialiasing, true);
    QFont f = p->font();
    f.setPixelSize(11);
    p->setFont(f);
    // lanes
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row &r = m_rows[i];
        const double top = rowTop(r);
        if (top + r.h < RulerH || top > h) continue;
        QColor bg = r.kind == Row::Group ? kGroupLane : (i % 2 ? kLaneA : kLaneB);
        if (r.kind == Row::Film) bg = QColor(0x33, 0x33, 0x33);
        p->fillRect(QRectF(m_header, top, w - m_header, r.h), bg);
        p->fillRect(QRectF(m_header, top + r.h - 1, w - m_header, 1), kLine);
    }
    // frame grid
    {
        p->save();
        p->setClipRect(QRectF(m_header, RulerH, w - m_header, h - RulerH));
        int step = 1;
        for (int s : {1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1200, 3000, 6000}) { step = s; if (s * ppf() >= 80) break; }
        p->setPen(QColor(255, 255, 255, 10));
        for (double t = std::floor(tOf(m_header) / step) * step; xOf(t) < w; t += step)
            if (t >= 0) p->drawLine(QPointF(std::round(xOf(t)) + 0.5, RulerH), QPointF(std::round(xOf(t)) + 0.5, h));
        p->restore();
    }
    paintRangeShade(p, RulerH);
    p->save();
    p->setClipRect(QRectF(m_header, RulerH, w - m_header, h - RulerH));
    for (const Row &r : std::as_const(m_rows)) {
        const double top = rowTop(r);
        if (top + r.h < RulerH || top > h) continue;
        paintLane(p, r);
    }
    if (m_drag == Drag::Rubber && m_moved) {
        p->setPen(QPen(QColor(0x7f, 0xbf, 0xff), 1));
        p->setBrush(QColor(0x4a, 0xa8, 0xff, 40));
        p->drawRect(m_rubber);
    }
    p->restore();
    // headers
    p->fillRect(QRectF(0, RulerH, m_header, h - RulerH), kHeaderBg);
    p->save();
    p->setClipRect(QRectF(0, RulerH, m_header, h - RulerH));
    for (int i = 0; i < m_rows.size(); ++i) {
        const double top = rowTop(m_rows[i]);
        if (top + m_rows[i].h < RulerH || top > h) continue;
        paintHeader(p, m_rows[i], i);
    }
    p->restore();
    p->fillRect(QRectF(m_header - 1, 0, 1, h), kLine);
    // ruler corner
    p->fillRect(QRectF(0, 0, m_header, RulerH), QColor(0x30, 0x30, 0x30));
    p->setPen(kMuted);
    QFont hf = f;
    hf.setPixelSize(10);
    p->setFont(hf);
    const QString tc = m_studio->timecode(seqT());
    p->drawText(QRectF(10, 0, m_header - 16, RulerH), Qt::AlignVCenter | Qt::AlignLeft,
                QStringLiteral("%1   ·   frame %2").arg(tc).arg(int(std::floor(seqT()))));
    p->setFont(f);
    paintRuler(p, m_mode == QLatin1String("motion"));
    paintPlayhead(p);
    if (m_rows.size() <= 2 && m_mode == QLatin1String("clip")) {
        p->setPen(kMuted);
        p->drawText(QRectF(m_header + 20, RulerH + 90, w - m_header - 40, 60), Qt::AlignLeft | Qt::TextWordWrap,
                    QStringLiteral("Nothing on the timeline yet. Spawn a character from the Asset Browser, pick a clip for it, or capture a camera (C)."));
    }
}

void TimelineView::paintHeader(QPainter *p, const Row &r, int index)
{
    const double top = rowTop(r);
    const QRectF rr(0, top, m_header - 1, r.h);
    const bool hover = index == m_hoverRow;
    if (r.selected)
        p->fillRect(rr, kHeaderSel);
    else if (r.kind == Row::Group)
        p->fillRect(rr, QColor(0x3a, 0x3a, 0x3a));
    else if (hover)
        p->fillRect(rr, QColor(0x3a, 0x3a, 0x3a));
    p->fillRect(QRectF(0, top + r.h - 1, m_header, 1), kLine);
    const double cy = top + r.h / 2;
    auto icon = [&](const QString &name, double x, const QColor &c, int px = 14) {
        const QImage img = IconProvider::icon(name, c, px);
        p->drawImage(QPointF(x, cy - px / 2.0), img);
    };
    p->setPen(kText);
    QFont f = p->font();
    if (r.kind == Row::Film) {
        icon(r.icon, 8, TL::kindColor(QStringLiteral("film")));
        f.setBold(true);
        p->setFont(f);
        const int shots = m_studio->sequence().value(QStringLiteral("shots")).toArray().size();
        elidedText(p, QRectF(28, top, m_header - 60, r.h), QStringLiteral("Film  ·  %1 shot%2").arg(shots).arg(shots == 1 ? "" : "s"));
        f.setBold(false);
        p->setFont(f);
        icon(QStringLiteral("plus"), m_header - 24, kText);
    } else if (r.kind == Row::Group) {
        icon(r.collapsed ? QStringLiteral("chevron-right") : QStringLiteral("chevron-down"), 4, kMuted, 12);
        icon(r.icon, 20, r.isCamera ? TL::kindColor(QStringLiteral("camera")) : QColor(0xb8, 0xc4, 0xd6));
        f.setBold(true);
        p->setFont(f);
        const double lw = m_header - 70;
        elidedText(p, QRectF(40, top, lw, r.h), r.label);
        f.setBold(false);
        p->setFont(f);
        if (!r.sub.isEmpty()) {
            p->setPen(kMuted);
            p->drawText(QRectF(40, top, m_header - 72, r.h), Qt::AlignVCenter | Qt::AlignRight, r.sub);
        }
        icon(QStringLiteral("plus"), m_header - 24, hover ? kText : kMuted);
    } else if (r.kind == Row::Audio) {
        icon(r.icon, 20, TL::kindColor(QStringLiteral("audio")));
        elidedText(p, QRectF(40, top, m_header - 72, r.h), r.label);
        icon(QStringLiteral("x"), m_header - 24, hover ? kText : kMuted, 13);
    } else {
        const QString kind = r.track.value(QStringLiteral("kind")).toString();
        const QColor c = TL::kindColor(kind);
        p->fillRect(QRectF(26, top + 5, 3, r.h - 10), c);
        QString label = TL::kindLabel(kind);
        if (kind == QLatin1String("anim"))
            label += r.track.value(QStringLiteral("layer")).toInt() == 1 ? QStringLiteral(" · upper body") : QStringLiteral(" · layer %1").arg(r.track.value(QStringLiteral("layer")).toInt());
        if (kind == QLatin1String("cammove"))
            label = cameraName(r.track.value(QStringLiteral("cam")).toInt()) + QStringLiteral(" · move");
        if (kind == QLatin1String("channel"))
            label = r.track.value(QStringLiteral("label")).toString();
        if (r.track.value(QStringLiteral("missing")).toBool())
            label += QStringLiteral(" (missing)");
        p->setPen(r.track.value(QStringLiteral("missing")).toBool() ? kMuted : kText);
        elidedText(p, QRectF(36, top, m_header - 66, r.h), label);
        if (hover)
            icon(QStringLiteral("x"), m_header - 22, kMuted, 12);
    }
}

void TimelineView::paintLane(QPainter *p, const Row &r)
{
    const double top = rowTop(r);
    const double cy = top + r.h / 2;
    const double w = width();
    const double len = seqLength();
    const int fpsv = fps();
    const QJsonObject st = m_studio->stateObj();
    const QJsonObject selClip = st.value(QStringLiteral("selection")).toObject().value(QStringLiteral("clip")).toObject();
    const int selTrack = selClip.value(QStringLiteral("track")).toInt(-1), selId = selClip.value(QStringLiteral("id")).toInt(-1);
    const bool motion = m_mode == QLatin1String("motion");
    const double keySize = motion ? 11 : 9;
    auto block = [&](double t0, double t1, double y0, double y1, const QColor &c, bool selected, const QString &label, double alpha = 1.0) -> QRectF {
        double x0 = xOf(t0), x1 = xOf(t1);
        if (x1 < m_header || x0 > w) return {};
        QRectF rect(x0, y0, std::max(4.0, x1 - x0), y1 - y0);
        QColor fill = c;
        fill.setAlphaF(0.62 * alpha);
        p->setPen(selected ? QPen(kSelect, 2) : QPen(c.lighter(120), 1));
        p->setBrush(fill);
        p->drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
        if (selected)
            p->fillRect(QRectF(rect.x() + 1, rect.y() + 1, rect.width() - 2, 3), kSelect);
        if (!label.isEmpty() && rect.width() > 24) {
            p->setPen(QColor(0xf4, 0xf4, 0xf6));
            const double lx = std::max(rect.x() + 6, m_header + 4.0);
            elidedText(p, QRectF(lx, rect.y(), rect.right() - lx - 4, std::min(rect.height(), 20.0)), label);
        }
        return rect;
    };

    if (r.kind == Row::Film) {
        const QJsonArray shots = m_studio->sequence().value(QStringLiteral("shots")).toArray();
        const double t = seqT();
        int n = 0;
        for (const QJsonValue &v : shots) {
            ++n;
            const QJsonObject s = v.toObject();
            const int id = s.value(QStringLiteral("id")).toInt();
            double a = s.value(QStringLiteral("a")).toDouble(), b = s.value(QStringLiteral("b")).toDouble();
            if (m_drag == Drag::Shot && m_moved && m_press.id == id) {
                if (m_press.edge == Edge::Body) { a += m_dt; b += m_dt; }
                else if (m_press.edge == Edge::Left) a = std::min(b - 1, a + m_dt);
                else b = std::max(a + 1, b + m_dt);
            }
            const bool current = t >= a && t < b;
            const QString cam = s.value(QStringLiteral("cam")).isDouble() ? cameraName(s.value(QStringLiteral("cam")).toInt()) : QStringLiteral("no camera");
            const QRectF rect = block(a, b, top + 4, top + r.h - 4, current ? QColor(0x6a, 0x7f, 0x9c) : TL::kindColor(QStringLiteral("film")), id == m_selShot,
                                      QStringLiteral("%1  ·  %2").arg(s.value(QStringLiteral("name")).toString(), cam), current ? 1.0 : 0.8);
            if (rect.isValid() && rect.width() > 40) {
                p->setPen(QColor(0xdd, 0xdd, 0xe4, 170));
                QFont sf = p->font();
                sf.setPixelSize(9);
                p->setFont(sf);
                p->drawText(QRectF(rect.x() + 6, rect.bottom() - 13, rect.width() - 10, 12), Qt::AlignLeft | Qt::AlignVCenter,
                            QStringLiteral("s%1  %2-%3").arg(n, 3, 10, QLatin1Char('0')).arg(int(a)).arg(int(b)));
                sf.setPixelSize(11);
                p->setFont(sf);
            }
        }
        if (shots.isEmpty()) {
            p->setPen(kMuted);
            p->drawText(QRectF(m_header + 10, top, 520, r.h), Qt::AlignVCenter | Qt::AlignLeft,
                        QStringLiteral("No shots. Set a time selection (I / O) and press + or double-click here to make one."));
        }
        return;
    }
    if (r.kind == Row::Group) {
        for (double kt : r.summary) {
            const double x = xOf(kt);
            if (x < m_header - 6 || x > w + 6) continue;
            paintKey(p, x, cy, QColor(0x9a, 0x9a, 0xa4), false, QString(), 7);
        }
        return;
    }
    if (r.kind == Row::Audio) {
        const QJsonObject a = m_studio->sequence().value(QStringLiteral("audio")).toObject();
        GuideAudio *ga = m_studio->guideAudio();
        double off = a.value(QStringLiteral("offset")).toDouble();
        if (m_drag == Drag::Audio && m_moved) off += m_dt;
        const double dur = ga->duration() > 0 ? ga->duration() : a.value(QStringLiteral("duration")).toDouble(10);
        const QColor c = TL::kindColor(QStringLiteral("audio"));
        const QRectF rect = block(off, off + dur * fpsv, top + 3, top + r.h - 3, c, false, QString(), 0.45);
        if (!rect.isValid()) return;
        const QVector<float> &pk = ga->peaks();
        if (!pk.isEmpty()) {
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(0x9c, 0xf0, 0x7c));
            const double mid = rect.center().y(), amp = rect.height() / 2 - 3;
            const double from = std::max(rect.left(), double(m_header)), to = std::min(rect.right(), w);
            for (double x = std::floor(from); x < to; x += 1) {
                const int i0 = int((x - rect.left()) / rect.width() * pk.size());
                const int i1 = std::max(i0 + 1, int((x + 1 - rect.left()) / rect.width() * pk.size()));
                float m = 0;
                for (int i = std::max(0, i0); i < i1 && i < pk.size(); ++i) m = std::max(m, pk[i]);
                const double hh = std::max(1.0, double(m) * amp);
                p->drawRect(QRectF(x, mid - hh, 1, hh * 2));
            }
        }
        p->setPen(QColor(0xe8, 0xff, 0xe0));
        const QString status = ga->decoding() ? QStringLiteral(" · reading…") : !ga->error().isEmpty() ? QStringLiteral(" · ") + ga->error() : QString();
        elidedText(p, QRectF(std::max(rect.x() + 6, m_header + 4.0), rect.y() + 1, rect.width() - 10, 14), a.value(QStringLiteral("name")).toString() + status);
        return;
    }

    // tracks
    const QJsonObject t = r.track;
    const QString kind = t.value(QStringLiteral("kind")).toString();
    const int idx = t.value(QStringLiteral("idx")).toInt();
    const QColor c = TL::kindColor(kind);
    if (kind == QLatin1String("camera")) {
        const QJsonArray cuts = t.value(QStringLiteral("cuts")).toArray();
        for (int i = 0; i < cuts.size(); ++i) {
            const QJsonObject cut = cuts[i].toObject();
            double s0 = cut.value(QStringLiteral("start")).toDouble();
            const int id = cut.value(QStringLiteral("id")).toInt();
            if (m_drag == Drag::Cut && m_moved && m_press.id == id) s0 = std::max(0.0, s0 + m_dt);
            const double end = i + 1 < cuts.size() ? cuts[i + 1].toObject().value(QStringLiteral("start")).toDouble() : len;
            block(s0, end, top + 3, top + r.h - 3, c, selTrack == idx && selId == id, cameraName(cut.value(QStringLiteral("cam")).toInt()));
        }
        return;
    }
    if (kind == QLatin1String("path")) {
        const double s0 = t.value(QStringLiteral("start")).toDouble(), d = t.value(QStringLiteral("dur")).toDouble();
        block(s0, s0 + d, top + 3, top + r.h - 3, c, t.value(QStringLiteral("drawing")).toBool(),
              t.value(QStringLiteral("drawing")).toBool() ? QStringLiteral("drawing… click the floor in the game")
                                                          : QStringLiteral("%1 m · %2 s · %3").arg(t.value(QStringLiteral("length")).toDouble(), 0, 'f', 1).arg(d / fpsv, 0, 'f', 1).arg(t.value(QStringLiteral("gait")).toString(QStringLiteral("walk"))));
        return;
    }
    if (kind == QLatin1String("xform") || kind == QLatin1String("cammove") || kind == QLatin1String("channel")) {
        const QJsonArray keys = t.value(QStringLiteral("keys")).toArray();
        if (keys.size() > 1) {
            p->setPen(QPen(QColor(c.red(), c.green(), c.blue(), 90), 2));
            p->drawLine(QPointF(xOf(keys.first().toObject().value(QStringLiteral("t")).toDouble()), cy), QPointF(xOf(keys.last().toObject().value(QStringLiteral("t")).toDouble()), cy));
        }
        for (const QJsonValue &kv : keys) {
            const QJsonObject k = kv.toObject();
            const KeyRef ref{idx, 0, k.value(QStringLiteral("t")).toDouble()};
            const double g = ghostT(ref);
            if (g > -1e8)
                paintKey(p, xOf(g), cy, QColor(255, 255, 255, 110), false, QString(), keySize);
            paintKey(p, xOf(ref.t), cy, c, keySelected(ref), k.value(QStringLiteral("ease")).toString(), keySize);
        }
        return;
    }
    // clips (anim / pose)
    for (const QJsonValue &cv : t.value(QStringLiteral("clips")).toArray()) {
        const QJsonObject cl = cv.toObject();
        const int id = cl.value(QStringLiteral("id")).toInt();
        double s0 = cl.value(QStringLiteral("start")).toDouble(), d = cl.value(QStringLiteral("dur")).toDouble();
        if (m_drag == Drag::Clip && m_moved && m_press.track == idx && m_press.id == id) {
            if (m_press.edge == Edge::Body) s0 = std::max(0.0, s0 + m_dt);
            else if (m_press.edge == Edge::Left) { const double ns = std::clamp(s0 + m_dt, 0.0, s0 + d - 1); d -= ns - s0; s0 = ns; }
            else d = std::max(1.0, d + m_dt);
        }
        const bool selected = selTrack == idx && selId == id;
        QString label;
        if (kind == QLatin1String("pose")) {
            label = QStringLiteral("Pose · %1 keys").arg(cl.value(QStringLiteral("keys")).toArray().size());
        } else {
            label = cl.value(QStringLiteral("name")).toString().isEmpty() ? QString::number(cl.value(QStringLiteral("mot")).toInt()) : motionLabel(cl.value(QStringLiteral("name")).toString());
            if (cl.value(QStringLiteral("loop")).toBool()) label += QStringLiteral("  ⟳");
            if (cl.value(QStringLiteral("rm")).toBool()) label += QStringLiteral("  → %1 m").arg(cl.value(QStringLiteral("rm_dist")).toDouble(), 0, 'f', 1);
            const double sp = cl.value(QStringLiteral("speed")).toDouble(1);
            if (std::abs(sp - 1) > 0.01) label += QStringLiteral("  ×%1").arg(sp, 0, 'f', 2);
        }
        const double alpha = (motion && kind == QLatin1String("pose")) ? 0.35 : 1.0;
        const QRectF rect = block(s0, s0 + d, top + 3, top + r.h - 3, c, selected, motion ? QString() : label, alpha);
        if (!rect.isValid()) continue;
        if (kind == QLatin1String("anim")) {
            // blend-in ramp
            const double bw = std::min(rect.width(), cl.value(QStringLiteral("blend")).toDouble(10) * ppf());
            if (bw > 3) {
                QPainterPath ramp;
                ramp.moveTo(rect.left(), rect.bottom());
                ramp.lineTo(rect.left() + bw, rect.top());
                ramp.lineTo(rect.left(), rect.top());
                ramp.closeSubpath();
                p->fillPath(ramp, QColor(0, 0, 0, 70));
            }
        } else {
            for (const QJsonValue &kv : cl.value(QStringLiteral("keys")).toArray()) {
                const double kt = s0 + kv.toDouble();
                const KeyRef ref{idx, id, cl.value(QStringLiteral("start")).toDouble() + kv.toDouble()};
                const double g = ghostT(ref);
                if (g > -1e8)
                    paintKey(p, xOf(g), cy, QColor(255, 255, 255, 110), false, QString(), keySize);
                paintKey(p, xOf(kt), cy, c.lighter(115), keySelected(ref), QString(), keySize);
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------- hit testing
TimelineView::Hit TimelineView::hitTest(const QPointF &pt) const
{
    Hit hit;
    if (pt.y() < RulerH || !m_studio)
        return hit;
    int ri = -1;
    for (int i = 0; i < m_rows.size(); ++i) {
        const double top = rowTop(m_rows[i]);
        if (pt.y() >= top && pt.y() < top + m_rows[i].h) { ri = i; break; }
    }
    if (ri < 0) {
        hit.kind = pt.x() >= m_header ? HitKind::Lane : HitKind::None;
        return hit;
    }
    const Row &r = m_rows[ri];
    hit.row = ri;
    if (pt.x() < m_header) {
        hit.kind = HitKind::Header;
        if (pt.x() > m_header - 30 && (r.kind != Row::Track || m_hoverRow == ri)) {
            hit.kind = HitKind::HeaderButton;
            hit.button = r.kind == Row::Track || r.kind == Row::Audio ? QStringLiteral("remove") : QStringLiteral("add");
        } else if (r.kind == Row::Group && pt.x() < 20) {
            hit.kind = HitKind::HeaderButton;
            hit.button = QStringLiteral("collapse");
        }
        return hit;
    }
    hit.kind = HitKind::Lane;
    const double x = pt.x();
    const double len = seqLength();
    const QJsonObject seq = m_studio->sequence();
    auto edgeOf = [&](double t0, double t1) {
        const double x0 = xOf(t0), x1 = xOf(t1);
        if (x1 - x0 > 16) {
            if (std::abs(x - x0) < 5) return Edge::Left;
            if (std::abs(x - x1) < 5) return Edge::Right;
        }
        return Edge::Body;
    };
    if (r.kind == Row::Film) {
        for (const QJsonValue &v : seq.value(QStringLiteral("shots")).toArray()) {
            const QJsonObject s = v.toObject();
            const double a = s.value(QStringLiteral("a")).toDouble(), b = s.value(QStringLiteral("b")).toDouble();
            if (x >= xOf(a) - 4 && x <= xOf(b) + 4) {
                hit.kind = HitKind::Shot;
                hit.id = s.value(QStringLiteral("id")).toInt();
                hit.start = a;
                hit.dur = b - a;
                hit.edge = edgeOf(a, b);
                return hit;
            }
        }
        return hit;
    }
    if (r.kind == Row::Audio) {
        const QJsonObject a = seq.value(QStringLiteral("audio")).toObject();
        const double off = a.value(QStringLiteral("offset")).toDouble();
        const double dur = m_studio->guideAudio()->duration() > 0 ? m_studio->guideAudio()->duration() : a.value(QStringLiteral("duration")).toDouble(10);
        if (x >= xOf(off) && x <= xOf(off + dur * fps())) {
            hit.kind = HitKind::Audio;
            hit.start = off;
        }
        return hit;
    }
    if (r.kind == Row::Group)
        return hit;
    const QJsonObject t = r.track;
    const QString kind = t.value(QStringLiteral("kind")).toString();
    const int idx = t.value(QStringLiteral("idx")).toInt();
    hit.track = idx;
    hit.clipKind = kind;
    // keys first
    double best = 7;
    if (kind == QLatin1String("xform") || kind == QLatin1String("cammove") || kind == QLatin1String("channel")) {
        for (const QJsonValue &kv : t.value(QStringLiteral("keys")).toArray()) {
            const QJsonObject k = kv.toObject();
            const double d = std::abs(xOf(k.value(QStringLiteral("t")).toDouble()) - x);
            if (d < best) {
                best = d;
                hit.kind = HitKind::Key;
                hit.key = {idx, 0, k.value(QStringLiteral("t")).toDouble()};
                hit.ease = k.value(QStringLiteral("ease")).toString();
            }
        }
        return hit;
    }
    if (kind == QLatin1String("pose")) {
        for (const QJsonValue &cv : t.value(QStringLiteral("clips")).toArray()) {
            const QJsonObject c = cv.toObject();
            const QJsonArray keys = c.value(QStringLiteral("keys")).toArray(), eases = c.value(QStringLiteral("kease")).toArray();
            for (int i = 0; i < keys.size(); ++i) {
                const double kt = c.value(QStringLiteral("start")).toDouble() + keys[i].toDouble();
                const double d = std::abs(xOf(kt) - x);
                if (d < best) {
                    best = d;
                    hit.kind = HitKind::Key;
                    hit.key = {idx, c.value(QStringLiteral("id")).toInt(), kt};
                    hit.ease = eases.at(i).toString();
                }
            }
        }
        if (hit.kind == HitKind::Key)
            return hit;
    }
    if (kind == QLatin1String("camera")) {
        const QJsonArray cuts = t.value(QStringLiteral("cuts")).toArray();
        for (int i = 0; i < cuts.size(); ++i) {
            const QJsonObject cut = cuts[i].toObject();
            const double s0 = cut.value(QStringLiteral("start")).toDouble();
            const double end = i + 1 < cuts.size() ? cuts[i + 1].toObject().value(QStringLiteral("start")).toDouble() : len;
            if (x >= xOf(s0) && x <= xOf(end)) {
                hit.kind = HitKind::Cut;
                hit.id = cut.value(QStringLiteral("id")).toInt();
                hit.start = s0;
                return hit;
            }
        }
        return hit;
    }
    if (kind == QLatin1String("path")) {
        const double s0 = t.value(QStringLiteral("start")).toDouble(), d = t.value(QStringLiteral("dur")).toDouble();
        if (x >= xOf(s0) && x <= xOf(s0 + d)) { hit.kind = HitKind::Path; hit.start = s0; }
        return hit;
    }
    if (m_mode == QLatin1String("motion") && kind == QLatin1String("pose"))
        return hit; // clips are not editable in the dope sheet
    for (const QJsonValue &cv : t.value(QStringLiteral("clips")).toArray()) {
        const QJsonObject c = cv.toObject();
        const double s0 = c.value(QStringLiteral("start")).toDouble(), d = c.value(QStringLiteral("dur")).toDouble();
        if (x >= xOf(s0) - 3 && x <= xOf(s0 + d) + 3) {
            hit.kind = HitKind::Clip;
            hit.id = c.value(QStringLiteral("id")).toInt();
            hit.start = s0;
            hit.dur = d;
            hit.edge = edgeOf(s0, s0 + d);
            return hit;
        }
    }
    return hit;
}

// ---------------------------------------------------------------------------------------------- mouse
void TimelineView::mousePressEvent(QMouseEvent *e)
{
    if (!m_studio) return;
    const QPointF pos = e->position();
    m_pressPos = pos;
    m_moved = false;
    m_dt = 0;
    if (rulerPress(e, m_mode == QLatin1String("motion"))) { e->accept(); return; }
    if (e->button() == Qt::MiddleButton) { m_drag = Drag::Pan; panPress(e); e->accept(); return; }
    const Hit h = hitTest(pos);
    m_press = h;
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    const QJsonObject seq = m_studio->sequence();

    if (e->button() == Qt::RightButton) {
        QVariantMap info{{QStringLiteral("t"), std::round(std::max(0.0, tOf(pos.x())))}};
        QString kind;
        switch (h.kind) {
        case HitKind::Key: {
            QList<KeyRef> items = m_studio->keySel();
            if (!items.contains(h.key)) { items = {h.key}; m_studio->setKeySel(items); }
            info.insert(QStringLiteral("items"), m_studio->keySelList());
            info.insert(QStringLiteral("ease"), h.ease);
            kind = QStringLiteral("key");
            break;
        }
        case HitKind::Clip:
            m_studio->send(QStringLiteral("select_clip"), {{QStringLiteral("track"), h.track}, {QStringLiteral("id"), h.id}, {QStringLiteral("kind"), h.clipKind}});
            info.insert(QStringLiteral("track"), h.track); info.insert(QStringLiteral("id"), h.id); info.insert(QStringLiteral("kind"), h.clipKind);
            kind = QStringLiteral("clip");
            break;
        case HitKind::Cut:
            m_studio->send(QStringLiteral("select_clip"), {{QStringLiteral("track"), h.track}, {QStringLiteral("id"), h.id}, {QStringLiteral("kind"), QStringLiteral("camera")}});
            info.insert(QStringLiteral("track"), h.track); info.insert(QStringLiteral("id"), h.id);
            kind = QStringLiteral("cut");
            break;
        case HitKind::Shot:
            setSelectedShot(h.id);
            info.insert(QStringLiteral("id"), h.id);
            kind = QStringLiteral("shot");
            break;
        case HitKind::Audio: kind = QStringLiteral("audio"); break;
        case HitKind::Path: info.insert(QStringLiteral("track"), h.track); kind = QStringLiteral("path"); break;
        case HitKind::Header:
        case HitKind::HeaderButton:
        case HitKind::Lane: {
            if (h.row < 0) { kind = QStringLiteral("lane"); break; }
            const Row &r = m_rows[h.row];
            if (r.kind == Row::Film) kind = QStringLiteral("film");
            else if (r.kind == Row::Audio) kind = QStringLiteral("audio");
            else if (r.kind == Row::Group) { kind = r.isCamera ? QStringLiteral("cameras") : QStringLiteral("group"); info.insert(QStringLiteral("addr"), r.actor); }
            else { kind = QStringLiteral("track"); info.insert(QStringLiteral("track"), r.track.value(QStringLiteral("idx")).toInt()); info.insert(QStringLiteral("kind"), r.track.value(QStringLiteral("kind")).toString()); info.insert(QStringLiteral("addr"), r.actor); }
            break;
        }
        default: kind = QStringLiteral("lane");
        }
        emit menuRequested(kind, info, pos.x(), pos.y());
        e->accept();
        return;
    }
    if (e->button() != Qt::LeftButton) { e->ignore(); return; }

    switch (h.kind) {
    case HitKind::HeaderButton: {
        const Row &r = m_rows[h.row];
        if (h.button == QLatin1String("collapse")) {
            if (m_collapsed.contains(r.key)) m_collapsed.remove(r.key); else m_collapsed.insert(r.key);
            onStateChanged();
        } else if (h.button == QLatin1String("remove")) {
            if (r.kind == Row::Audio) m_studio->cmd(QStringLiteral("seq_audio"), {{QStringLiteral("clear"), true}});
            else m_studio->cmd(QStringLiteral("remove_track"), {{QStringLiteral("idx"), r.track.value(QStringLiteral("idx")).toInt()}});
        } else if (r.kind == Row::Film) {
            m_studio->cmd(QStringLiteral("shot_add"), {});
        } else {
            emit menuRequested(r.isCamera ? QStringLiteral("cameras") : QStringLiteral("group"), {{QStringLiteral("addr"), r.actor}, {QStringLiteral("t"), std::floor(seqT())}}, pos.x(), pos.y());
        }
        break;
    }
    case HitKind::Header: {
        const Row &r = m_rows[h.row];
        if (r.kind == Row::Group && r.actor)
            m_studio->send(QStringLiteral("select_actor"), {{QStringLiteral("addr"), r.actor}, {QStringLiteral("add"), shift}});
        else if (r.kind == Row::Group) {
            if (m_collapsed.contains(r.key)) m_collapsed.remove(r.key); else m_collapsed.insert(r.key);
            onStateChanged();
        } else if (r.kind == Row::Track) {
            m_studio->setGraphTrack(r.track.value(QStringLiteral("idx")).toInt());
            if (r.actor)
                m_studio->send(QStringLiteral("select_actor"), {{QStringLiteral("addr"), r.actor}});
        }
        break;
    }
    case HitKind::Key: {
        QList<KeyRef> sel = m_studio->keySel();
        const bool in = sel.contains(h.key);
        if (shift) {
            if (in) sel.removeAll(h.key); else sel.append(h.key);
            m_studio->setKeySel(sel);
            break;
        }
        if (!in) sel = {h.key};
        m_studio->setKeySel(sel);
        m_dragKeys = sel;
        m_drag = Drag::Keys;
        m_grabT = tOf(pos.x());
        m_scaleKeys = e->modifiers() & Qt::AltModifier;
        m_scalePivot = 1e18;
        for (const KeyRef &k : sel) m_scalePivot = std::min(m_scalePivot, k.t);
        m_scaleFactor = 1;
        break;
    }
    case HitKind::Clip:
        m_studio->send(QStringLiteral("select_clip"), {{QStringLiteral("track"), h.track}, {QStringLiteral("id"), h.id}, {QStringLiteral("kind"), h.clipKind}});
        m_studio->setKeySel({});
        m_drag = Drag::Clip;
        m_grabT = tOf(pos.x());
        break;
    case HitKind::Cut:
        m_studio->send(QStringLiteral("select_clip"), {{QStringLiteral("track"), h.track}, {QStringLiteral("id"), h.id}, {QStringLiteral("kind"), QStringLiteral("camera")}});
        m_drag = Drag::Cut;
        m_grabT = tOf(pos.x());
        break;
    case HitKind::Shot:
        setSelectedShot(h.id);
        m_drag = Drag::Shot;
        m_grabT = tOf(pos.x());
        break;
    case HitKind::Audio:
        m_drag = Drag::Audio;
        m_grabT = tOf(pos.x());
        break;
    case HitKind::Path:
        m_studio->send(QStringLiteral("seq_seek"), {{QStringLiteral("t"), h.start}});
        break;
    case HitKind::Lane:
        m_drag = Drag::Rubber;
        m_rubberBase = shift ? m_studio->keySel() : QList<KeyRef>();
        m_rubber = QRectF(pos, pos);
        break;
    default: break;
    }
    e->accept();
}

void TimelineView::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_studio) return;
    if (rulerMove(e)) return;
    const QPointF pos = e->position();
    if (m_drag == Drag::Pan) { panMove(e); return; }
    if (!m_moved && (pos - m_pressPos).manhattanLength() > 3) m_moved = true;
    if (!m_moved) return;
    const double dt = std::round(tOf(pos.x()) - m_grabT);
    switch (m_drag) {
    case Drag::Keys:
        m_dt = dt;
        if (m_scaleKeys) {
            const double ref = m_press.key.t;
            m_scaleFactor = std::max(0.05, (ref + dt - m_scalePivot) / std::max(1.0, ref - m_scalePivot));
        }
        setHover(m_scaleKeys ? QStringLiteral("scale ×%1").arg(m_scaleFactor, 0, 'f', 2) : QStringLiteral("%1%2 frames").arg(dt > 0 ? "+" : "").arg(dt));
        update();
        break;
    case Drag::Clip:
    case Drag::Cut:
    case Drag::Shot:
    case Drag::Audio:
        m_dt = dt;
        setHover(QStringLiteral("%1%2 frames").arg(dt > 0 ? "+" : "").arg(dt));
        update();
        break;
    case Drag::Rubber: {
        m_rubber = QRectF(m_pressPos, pos).normalized();
        QList<KeyRef> hits = m_rubberBase;
        for (const Row &r : std::as_const(m_rows)) {
            if (r.kind != Row::Track) continue;
            const double cy = rowTop(r) + r.h / 2;
            if (cy < m_rubber.top() || cy > m_rubber.bottom()) continue;
            for (const KeyRef &k : trackKeys(r.track)) {
                const double x = xOf(k.t);
                if (x >= m_rubber.left() && x <= m_rubber.right() && !hits.contains(k))
                    hits.append(k);
            }
        }
        m_studio->setKeySel(hits);
        update();
        break;
    }
    default: break;
    }
}

void TimelineView::mouseReleaseEvent(QMouseEvent *e)
{
    if (!m_studio) return;
    if (rulerRelease(e)) return;
    if (m_drag == Drag::Pan) { panRelease(e); m_drag = Drag::None; return; }
    const Drag d = m_drag;
    m_drag = Drag::None;
    setHover(QString());
    const QJsonObject seq = m_studio->sequence();
    switch (d) {
    case Drag::Keys: {
        if (!m_moved) { m_studio->send(QStringLiteral("seq_seek"), {{QStringLiteral("t"), m_press.key.t}}); break; }
        if (m_scaleKeys && m_press.key.t > m_scalePivot) {
            m_studio->cmd(QStringLiteral("scale_keys"), {{QStringLiteral("items"), m_studio->keySelList()}, {QStringLiteral("pivot"), m_scalePivot}, {QStringLiteral("factor"), m_scaleFactor}});
            QList<KeyRef> moved;
            for (KeyRef k : m_dragKeys) { k.t = std::round(m_scalePivot + (k.t - m_scalePivot) * m_scaleFactor); moved << k; }
            m_studio->setKeySel(moved);
        } else if (m_dt != 0) {
            m_studio->cmd(QStringLiteral("move_keys"), {{QStringLiteral("items"), m_studio->keySelList()}, {QStringLiteral("dt"), m_dt}});
            QList<KeyRef> moved;
            for (KeyRef k : m_dragKeys) { k.t = std::max(0.0, k.t + m_dt); moved << k; }
            m_studio->setKeySel(moved);
        }
        break;
    }
    case Drag::Clip: {
        if (!m_moved || m_dt == 0) break;
        const int track = m_press.track, id = m_press.id;
        double speed = 1, offset = 0;
        for (const QJsonValue &tv : seq.value(QStringLiteral("tracks")).toArray())
            if (tv.toObject().value(QStringLiteral("idx")).toInt() == track)
                for (const QJsonValue &cv : tv.toObject().value(QStringLiteral("clips")).toArray())
                    if (cv.toObject().value(QStringLiteral("id")).toInt() == id) {
                        speed = cv.toObject().value(QStringLiteral("speed")).toDouble(1);
                        offset = cv.toObject().value(QStringLiteral("offset")).toDouble(0);
                    }
        QVariantMap fields;
        if (m_press.edge == Edge::Body) {
            fields.insert(QStringLiteral("start"), std::max(0.0, m_press.start + m_dt));
        } else if (m_press.edge == Edge::Left) {
            const double ns = std::clamp(m_press.start + m_dt, 0.0, m_press.start + m_press.dur - 1);
            const double delta = ns - m_press.start;
            fields.insert(QStringLiteral("start"), ns);
            fields.insert(QStringLiteral("dur"), m_press.dur - delta);
            if (m_press.clipKind == QLatin1String("anim"))
                fields.insert(QStringLiteral("offset"), std::max(0.0, offset + delta * speed));
        } else {
            fields.insert(QStringLiteral("dur"), std::max(1.0, m_press.dur + m_dt));
        }
        m_studio->cmd(QStringLiteral("update_clip"), {{QStringLiteral("track"), track}, {QStringLiteral("id"), id}, {QStringLiteral("fields"), fields}});
        break;
    }
    case Drag::Cut:
        if (m_moved && m_dt != 0)
            m_studio->cmd(QStringLiteral("update_cut"), {{QStringLiteral("track"), m_press.track}, {QStringLiteral("id"), m_press.id}, {QStringLiteral("start"), std::max(0.0, m_press.start + m_dt)}});
        break;
    case Drag::Shot: {
        if (!m_moved || m_dt == 0) break;
        const double a = m_press.start, b = m_press.start + m_press.dur;
        QVariantMap f;
        if (m_press.edge == Edge::Body) { f.insert(QStringLiteral("a"), std::max(0.0, a + m_dt)); f.insert(QStringLiteral("b"), std::max(1.0, b + m_dt)); }
        else if (m_press.edge == Edge::Left) f.insert(QStringLiteral("a"), std::clamp(a + m_dt, 0.0, b - 1));
        else f.insert(QStringLiteral("b"), std::max(a + 1, b + m_dt));
        m_studio->cmd(QStringLiteral("shot_update"), {{QStringLiteral("id"), m_press.id}, {QStringLiteral("fields"), f}});
        break;
    }
    case Drag::Audio:
        if (m_moved && m_dt != 0)
            m_studio->cmd(QStringLiteral("seq_audio"), {{QStringLiteral("offset"), m_press.start + m_dt}});
        break;
    case Drag::Rubber:
        if (!m_moved) {
            m_studio->setKeySel({});
            m_studio->send(QStringLiteral("select_clip"), {});
            setSelectedShot(-1);
        }
        break;
    default: break;
    }
    m_moved = false;
    m_dt = 0;
    update();
}

void TimelineView::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (!m_studio || e->position().y() < RulerH) return;
    const Hit h = hitTest(e->position());
    if (h.kind == HitKind::Shot) {
        m_studio->send(QStringLiteral("shot_go"), {{QStringLiteral("id"), h.id}});
    } else if (h.kind == HitKind::Lane && h.row >= 0 && m_rows[h.row].kind == Row::Film) {
        m_studio->cmd(QStringLiteral("shot_add"), {});
    } else if (h.kind == HitKind::Clip || h.kind == HitKind::Cut) {
        m_studio->send(QStringLiteral("seq_seek"), {{QStringLiteral("t"), h.start}});
    }
    e->accept();
}

void TimelineView::hoverMoveEvent(QHoverEvent *e)
{
    const QPointF pos = e->position();
    int row = -1;
    for (int i = 0; i < m_rows.size(); ++i) {
        const double top = rowTop(m_rows[i]);
        if (pos.y() >= top && pos.y() < top + m_rows[i].h && pos.y() > RulerH) { row = i; break; }
    }
    if (row != m_hoverRow) { m_hoverRow = row; update(); }
    if (m_drag != Drag::None) return;
    const Hit h = hitTest(pos);
    if ((h.kind == HitKind::Clip || h.kind == HitKind::Shot) && h.edge != Edge::Body) setCursor(Qt::SizeHorCursor);
    else if (pos.y() < RulerH && pos.x() > m_header) setCursor(Qt::IBeamCursor);
    else unsetCursor();
    QString tip;
    if (h.kind == HitKind::Key) tip = QStringLiteral("key @ %1%2 · drag: retime · Alt+drag: scale · Shift: add · right-click: easing").arg(h.key.t).arg(h.ease.isEmpty() || h.ease == QLatin1String("smooth") ? QString() : QStringLiteral(" · ") + h.ease);
    else if (h.kind == HitKind::Clip) tip = QStringLiteral("frames %1-%2 · drag: move · edges: trim · right-click: split, duplicate, delete").arg(h.start).arg(h.start + h.dur);
    else if (h.kind == HitKind::Shot) tip = QStringLiteral("shot · double-click: enter · drag: move · edges: trim");
    else if (h.kind == HitKind::Cut) tip = QStringLiteral("camera cut @ %1 · drag: retime").arg(h.start);
    else if (h.kind == HitKind::Audio) tip = QStringLiteral("audio · drag: move in time · right-click: remove");
    setHover(tip);
}

void TimelineView::hoverLeaveEvent(QHoverEvent *)
{
    m_hoverRow = -1;
    setHover(QString());
    update();
}
