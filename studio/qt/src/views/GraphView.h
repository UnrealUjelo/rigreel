// Graph Editor (SFM's F4): the curves of keyed channels over time.
// Left: every keyed track with its channels (click a track to show it, click a channel to hide / show it).
// Right: the curves, evaluated with the runtime's own easing. Drag a key up / down to change its value, left /
// right to retime it (Shift: value only, Ctrl: time only); drag on empty space to box-select; right-click a key for
// its interpolation. "Normalized" draws every channel in its own range; off, they share one value axis
// (Alt+wheel zooms it, Alt+middle-drag pans it). F frames everything.
#pragma once

#include "Studio.h"
#include "TimeCanvas.h"

#include <QJsonObject>
#include <QSet>

class GraphView : public TimeCanvas
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool normalized READ normalized WRITE setNormalized NOTIFY normalizedChanged)
    Q_PROPERTY(QString hoverText READ hoverText NOTIFY hoverTextChanged)
public:
    explicit GraphView(QQuickItem *parent = nullptr);
    bool normalized() const { return m_normalized; }
    void setNormalized(bool n);
    QString hoverText() const { return m_hover; }
    void paint(QPainter *p) override;
    Q_INVOKABLE void frameValues();

signals:
    void normalizedChanged();
    void hoverTextChanged();
    void menuRequested(const QString &kind, const QVariantMap &info, double x, double y);

protected:
    void onStateChanged() override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void hoverMoveEvent(QHoverEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    struct Channel { QString field, label; QColor color; QString unit; };
    struct Side { int track; bool header; QString field; double y, h; };
    struct KeyHit { bool ok = false; KeyRef ref; QString field; double value = 0; QString ease; };

    QList<QJsonObject> keyedTracks() const;
    QJsonObject currentTrack() const;
    QList<Channel> channelsOf(const QString &kind) const;
    void computeRanges();
    double yOf(const QString &f, double v) const;
    double vOf(const QString &f, double y) const;
    KeyHit hitKey(const QPointF &p) const;
    QString trackLabel(const QJsonObject &t) const;
    void setHover(const QString &s) { if (s != m_hover) { m_hover = s; emit hoverTextChanged(); } }

    bool m_normalized = true;
    QSet<QString> m_hidden;                  // "track:field"
    QHash<QString, QPair<double, double>> m_ranges;
    double m_vmin = -1, m_vmax = 1;          // shared axis (not normalized)
    QVector<Side> m_side;
    QString m_hover;

    enum class Drag { None, Key, Rubber, Pan, VPan } m_drag = Drag::None;
    KeyHit m_press;
    QPointF m_pressPos;
    bool m_moved = false;
    double m_dt = 0, m_dv = 0;
    QRectF m_rubber;
    QList<KeyRef> m_rubberBase;
    double m_vpanMin = 0, m_vpanMax = 0;
};
