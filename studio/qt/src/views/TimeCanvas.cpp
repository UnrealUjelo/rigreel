#include "TimeCanvas.h"

#include "Studio.h"

#include <QJsonArray>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

TimeCanvas::TimeCanvas(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);
    setAntialiasing(true);
    setFillColor(QColor(0x2b, 0x2b, 0x2b));
}

QObject *TimeCanvas::studioObj() const { return m_studio; }

void TimeCanvas::setStudio(QObject *s)
{
    auto *st = qobject_cast<Studio *>(s);
    if (st == m_studio)
        return;
    if (m_studio)
        disconnect(m_studio, nullptr, this, nullptr);
    m_studio = st;
    if (m_studio) {
        connect(m_studio, &Studio::stateChanged, this, [this] { onStateChanged(); });
        connect(m_studio, &Studio::viewChanged, this, [this] { update(); });
        connect(m_studio, &Studio::keySelChanged, this, [this] { update(); });
        connect(m_studio, &Studio::graphTrackChanged, this, [this] { onStateChanged(); });
    }
    emit studioChanged();
    onStateChanged();
}

void TimeCanvas::setHeaderWidth(int w) { if (w != m_header) { m_header = w; emit headerWidthChanged(); update(); } }
void TimeCanvas::setVscroll(double v)
{
    v = std::clamp(v, 0.0, std::max(0.0, m_contentHeight - (height() - RulerH)));
    if (v != m_vscroll) { m_vscroll = v; emit vscrollChanged(); update(); }
}
void TimeCanvas::setContentHeight(double h)
{
    if (h != m_contentHeight) {
        m_contentHeight = h;
        emit contentHeightChanged();
        setVscroll(m_vscroll);
    }
}

double TimeCanvas::ppf() const { return m_studio ? m_studio->ppf() : 1.0; }
double TimeCanvas::scroll() const { return m_studio ? m_studio->scroll() : 0.0; }
double TimeCanvas::xOf(double t) const { return m_header + (t - scroll()) * ppf(); }
double TimeCanvas::tOf(double x) const { return scroll() + (x - m_header) / ppf(); }
double TimeCanvas::seqT() const { return m_studio ? m_studio->sequence().value(QStringLiteral("t")).toDouble() : 0; }
double TimeCanvas::seqLength() const { return m_studio ? m_studio->sequence().value(QStringLiteral("length")).toDouble(600) : 600; }
int TimeCanvas::fps() const { return m_studio ? std::max(1, m_studio->sequence().value(QStringLiteral("fps")).toInt(60)) : 60; }

bool TimeCanvas::hasRange(double *a, double *b) const
{
    if (!m_studio)
        return false;
    const QJsonArray r = m_studio->sequence().value(QStringLiteral("range")).toArray();
    if (r.size() != 2)
        return false;
    if (a) *a = r[0].toDouble();
    if (b) *b = r[1].toDouble();
    return true;
}

void TimeCanvas::falloff(double *fin, double *fout) const
{
    const QJsonArray f = m_studio ? m_studio->sequence().value(QStringLiteral("falloff")).toArray() : QJsonArray();
    *fin = f.size() == 2 ? f[0].toDouble() : 0;
    *fout = f.size() == 2 ? f[1].toDouble() : 0;
}

void TimeCanvas::zoomBy(double factor, double aroundX)
{
    if (!m_studio)
        return;
    if (aroundX < 0)
        aroundX = m_header + (width() - m_header) / 2;
    const double t = tOf(aroundX);
    m_studio->setPpf(ppf() * factor);
    m_studio->setScroll(t - (aroundX - m_header) / ppf());
}

void TimeCanvas::frameAll()
{
    if (!m_studio)
        return;
    double a = 0, b = seqLength();
    hasRange(&a, &b);
    const double w = std::max(50.0, width() - m_header - 24);
    m_studio->setPpf(w / std::max(10.0, b - a));
    m_studio->setScroll(a - 12 / ppf());
}

void TimeCanvas::wheelEvent(QWheelEvent *e)
{
    const double d = e->angleDelta().y() ? e->angleDelta().y() : e->angleDelta().x();
    if (e->modifiers() & Qt::ShiftModifier) {
        setVscroll(m_vscroll - d / 2);
    } else if (e->modifiers() & Qt::ControlModifier) {
        if (m_studio)
            m_studio->setScroll(scroll() - d / ppf() / 2);
    } else {
        zoomBy(d > 0 ? 1.18 : 1 / 1.18, e->position().x());
    }
    e->accept();
}

// ---------------------------------------------------------------------------------------------- ruler
void TimeCanvas::paintRuler(QPainter *p, bool showFalloff)
{
    const double w = width();
    p->fillRect(QRectF(m_header, 0, w - m_header, RulerH), QColor(0x30, 0x30, 0x30));
    p->setPen(QColor(0x1d, 0x1d, 0x1d));
    p->drawLine(QPointF(0, RulerH - 0.5), QPointF(w, RulerH - 0.5));
    p->save();
    p->setClipRect(QRectF(m_header, 0, w - m_header, height()));
    // time selection bar: hold region solid, falloff ramps (SFM style trapezoid)
    double a, b;
    if (hasRange(&a, &b)) {
        double fin = 0, fout = 0;
        falloff(&fin, &fout);
        const double y0 = RulerH - 9, y1 = RulerH - 2;
        QPainterPath path;
        path.moveTo(xOf(a - (showFalloff ? fin : 0)), y1);
        path.lineTo(xOf(a), y0);
        path.lineTo(xOf(b), y0);
        path.lineTo(xOf(b + (showFalloff ? fout : 0)), y1);
        path.closeSubpath();
        p->fillPath(path, QColor(0x3d, 0x8b, 0xd9, 150));
        p->setPen(QPen(QColor(0x7f, 0xbf, 0xff), 2));
        p->drawLine(QPointF(xOf(a), 2), QPointF(xOf(a), RulerH - 2));
        p->drawLine(QPointF(xOf(b), 2), QPointF(xOf(b), RulerH - 2));
        if (showFalloff) {
            p->setPen(QPen(QColor(0x7f, 0xbf, 0xff, 170), 1, Qt::DashLine));
            p->drawLine(QPointF(xOf(a - fin), 8), QPointF(xOf(a - fin), RulerH - 2));
            p->drawLine(QPointF(xOf(b + fout), 8), QPointF(xOf(b + fout), RulerH - 2));
        }
    }
    // ticks: a label every >= 80 px, minor ticks between
    int step = 1;
    const int steps[] = {1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1200, 3000, 6000};
    for (int s : steps) { step = s; if (s * ppf() >= 80) break; }
    const int minor = step >= 10 ? step / 5 : 1;
    QFont f = p->font();
    f.setPixelSize(10);
    p->setFont(f);
    const double t0 = std::floor(tOf(m_header) / minor) * minor;
    for (double t = t0; xOf(t) < w; t += minor) {
        if (t < 0) continue;
        const double x = std::round(xOf(t)) + 0.5;
        const bool major = std::fmod(t, step) == 0;
        p->setPen(major ? QColor(0x70, 0x70, 0x70) : QColor(0x45, 0x45, 0x45));
        p->drawLine(QPointF(x, major ? RulerH - 8 : RulerH - 4), QPointF(x, RulerH - 1));
        if (major) {
            // frame numbers centred on their tick, like Blender
            p->setPen(QColor(0xa5, 0xa5, 0xa5));
            const QString label = QString::number(int(t));
            p->drawText(QRectF(x - 30, 1, 60, RulerH - 8), Qt::AlignHCenter | Qt::AlignVCenter, label);
        }
    }
    // end of the sequence
    p->setPen(QPen(QColor(0xe0, 0x60, 0x60, 180), 1));
    p->drawLine(QPointF(xOf(seqLength()), 0), QPointF(xOf(seqLength()), RulerH));
    p->restore();
}

void TimeCanvas::paintRangeShade(QPainter *p, int top)
{
    double a, b;
    if (!hasRange(&a, &b))
        return;
    p->save();
    p->setClipRect(QRectF(m_header, top, width() - m_header, height() - top));
    const QColor shade(0, 0, 0, 90);
    p->fillRect(QRectF(m_header, top, std::max(0.0, xOf(a) - m_header), height() - top), shade);
    p->fillRect(QRectF(xOf(b), top, std::max(0.0, width() - xOf(b)), height() - top), shade);
    p->restore();
}

void TimeCanvas::paintPlayhead(QPainter *p)
{
    const double x = std::round(xOf(seqT())) + 0.5;
    if (x < m_header || x > width())
        return;
    const QColor c(0x47, 0x72, 0xb3);                  // Blender's playhead
    p->setPen(QPen(c, 2));
    p->drawLine(QPointF(x, RulerH), QPointF(x, height()));
    // head with the frame number
    const QString lbl = QString::number(int(std::floor(seqT())));
    QFont f = p->font();
    f.setPixelSize(10);
    f.setBold(true);
    p->setFont(f);
    const double tw = std::max(22.0, QFontMetricsF(f).horizontalAdvance(lbl) + 12);
    QRectF head(x - tw / 2, 2, tw, RulerH - 4);
    p->setPen(Qt::NoPen);
    p->setBrush(c);
    p->drawRoundedRect(head, 5, 5);
    p->setPen(QColor(0xff, 0xff, 0xff));
    p->drawText(head, Qt::AlignCenter, lbl);
}

bool TimeCanvas::rulerPress(QMouseEvent *e, bool showFalloff)
{
    const QPointF pos = e->position();
    if (pos.y() > RulerH || pos.x() < m_header || !m_studio)
        return false;
    if (e->button() == Qt::MiddleButton)
        return panPress(e);
    if (e->button() != Qt::LeftButton)
        return true;
    double a, b;
    m_rdrag = RulerDrag::Seek;
    if (hasRange(&a, &b)) {
        double fin, fout;
        falloff(&fin, &fout);
        if (showFalloff && std::abs(pos.x() - xOf(a - fin)) < 6 && fin > 0) m_rdrag = RulerDrag::FalloffIn;
        else if (showFalloff && std::abs(pos.x() - xOf(b + fout)) < 6 && fout > 0) m_rdrag = RulerDrag::FalloffOut;
        else if (std::abs(pos.x() - xOf(a)) < 6) m_rdrag = (showFalloff && (e->modifiers() & Qt::AltModifier)) ? RulerDrag::FalloffIn : RulerDrag::RangeA;
        else if (std::abs(pos.x() - xOf(b)) < 6) m_rdrag = (showFalloff && (e->modifiers() & Qt::AltModifier)) ? RulerDrag::FalloffOut : RulerDrag::RangeB;
    }
    m_lastSent = -1e9;
    rulerMove(e);
    return true;
}

bool TimeCanvas::rulerMove(QMouseEvent *e)
{
    if (m_rdrag == RulerDrag::None || !m_studio)
        return false;
    if (m_rdrag == RulerDrag::Pan)
        return panMove(e);
    const double t = std::clamp(tOf(e->position().x()), 0.0, seqLength());
    const double ft = std::round(t);
    double a = 0, b = 0, fin = 0, fout = 0;
    hasRange(&a, &b);
    falloff(&fin, &fout);
    switch (m_rdrag) {
    case RulerDrag::Seek:
        if (std::abs(t - m_lastSent) > 0.2) { m_lastSent = t; m_studio->send(QStringLiteral("seq_seek"), {{QStringLiteral("t"), (e->modifiers() & Qt::ShiftModifier) ? t : ft}}); }
        break;
    case RulerDrag::RangeA:
        if (ft != m_lastSent) { m_lastSent = ft; m_studio->send(QStringLiteral("seq_range"), {{QStringLiteral("a"), std::min(ft, b - 1)}}); }
        break;
    case RulerDrag::RangeB:
        if (ft != m_lastSent) { m_lastSent = ft; m_studio->send(QStringLiteral("seq_range"), {{QStringLiteral("b"), std::max(ft, a + 1)}}); }
        break;
    case RulerDrag::FalloffIn: {
        const double v = std::max(0.0, a - ft);
        if (v != m_lastSent) { m_lastSent = v; m_studio->send(QStringLiteral("seq_range"), {{QStringLiteral("fin"), v}}); }
        break;
    }
    case RulerDrag::FalloffOut: {
        const double v = std::max(0.0, ft - b);
        if (v != m_lastSent) { m_lastSent = v; m_studio->send(QStringLiteral("seq_range"), {{QStringLiteral("fout"), v}}); }
        break;
    }
    default: break;
    }
    return true;
}

bool TimeCanvas::rulerRelease(QMouseEvent *e)
{
    if (m_rdrag == RulerDrag::None)
        return false;
    if (m_rdrag == RulerDrag::Pan)
        panRelease(e);
    m_rdrag = RulerDrag::None;
    return true;
}

bool TimeCanvas::panPress(QMouseEvent *e)
{
    m_rdrag = RulerDrag::Pan;
    m_panStart = e->position();
    m_panScroll = scroll();
    m_panV = m_vscroll;
    setCursor(Qt::ClosedHandCursor);
    return true;
}

bool TimeCanvas::panMove(QMouseEvent *e)
{
    if (m_rdrag != RulerDrag::Pan || !m_studio)
        return false;
    const QPointF d = e->position() - m_panStart;
    m_studio->setScroll(m_panScroll - d.x() / ppf());
    setVscroll(m_panV - d.y());
    return true;
}

bool TimeCanvas::panRelease(QMouseEvent *)
{
    if (m_rdrag != RulerDrag::Pan)
        return false;
    m_rdrag = RulerDrag::None;
    unsetCursor();
    return true;
}
