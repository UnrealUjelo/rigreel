// Clip Editor and Motion Editor (SFM's F2 / F3), painted in C++.
//   clip mode  : the Film track (shots), camera cuts, every character's clips (animation, pose, walk path),
//                position / camera keys, and the audio guide with its waveform. Drag clips and shots to move,
//                drag their edges to trim, double-click a shot to enter it.
//   motion mode: the dope sheet - only keyed channels, big keys, and the time selection with falloff handles
//                (Alt+drag an In/Out handle, or drag the dashed ones) that shapes how pose edits blend in and out.
// Left column = track headers (collapse, add, remove). Right-click anything for its menu.
#pragma once

#include "Studio.h"
#include "TimeCanvas.h"

#include <QJsonObject>
#include <QSet>
#include <QVector>

class TimelineView : public TimeCanvas
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(int selectedShot READ selectedShot WRITE setSelectedShot NOTIFY selectedShotChanged)
    Q_PROPERTY(QString hoverText READ hoverText NOTIFY hoverTextChanged)
public:
    explicit TimelineView(QQuickItem *parent = nullptr);
    QString mode() const { return m_mode; }
    void setMode(const QString &m);
    int selectedShot() const { return m_selShot; }
    void setSelectedShot(int id);
    QString hoverText() const { return m_hover; }

    void paint(QPainter *p) override;
    Q_INVOKABLE void collapseAll(bool collapsed);
    Q_INVOKABLE QVariantList allKeys() const;   // every selectable key (Ctrl+A)

signals:
    void modeChanged();
    void selectedShotChanged();
    void hoverTextChanged();
    // QML shows the matching menu at (x, y) in item coordinates
    void menuRequested(const QString &kind, const QVariantMap &info, double x, double y);

protected:
    void onStateChanged() override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void hoverMoveEvent(QHoverEvent *e) override;
    void hoverLeaveEvent(QHoverEvent *e) override;
    void geometryChange(const QRectF &n, const QRectF &o) override;

private:
    struct Row {
        enum Kind { Film, Group, Track, Audio } kind = Track;
        QString key, label, sub, icon;
        double actor = 0;
        bool isCamera = false, collapsed = false, selected = false;
        QJsonObject track;
        QVector<double> summary;
        double y = 0, h = 24;
    };
    enum class HitKind { None, Header, HeaderButton, Shot, Cut, Clip, Key, Audio, Path, Lane };
    enum class Edge { Body, Left, Right };
    struct Hit {
        HitKind kind = HitKind::None;
        int row = -1;
        QString button;
        int track = 0, id = 0;
        QString clipKind;
        Edge edge = Edge::Body;
        KeyRef key;
        QString ease;
        double start = 0, dur = 0;
    };

    void buildRows();
    QList<KeyRef> trackKeys(const QJsonObject &t) const;
    Hit hitTest(const QPointF &p) const;
    double rowTop(const Row &r) const { return RulerH + r.y - m_vscroll; }
    void paintHeader(QPainter *p, const Row &r, int index);
    void paintLane(QPainter *p, const Row &r);
    void paintKey(QPainter *p, double x, double cy, const QColor &c, bool sel, const QString &ease, double size);
    QString cameraName(int i) const;
    QString actorLabel(double addr) const;
    bool keySelected(const KeyRef &k) const;
    double ghostT(const KeyRef &k) const;
    void setHover(const QString &s);

    QString m_mode = QStringLiteral("clip");
    QVector<Row> m_rows;
    QSet<QString> m_collapsed;
    int m_selShot = -1;
    int m_hoverRow = -1;
    QString m_hover;

    // drags
    enum class Drag { None, Keys, Clip, Cut, Shot, Audio, Rubber, Pan } m_drag = Drag::None;
    Hit m_press;
    QPointF m_pressPos;
    double m_grabT = 0;
    bool m_moved = false;
    double m_dt = 0;              // frames (keys / clips / cuts / shots / audio)
    bool m_scaleKeys = false;
    double m_scalePivot = 0, m_scaleFactor = 1;
    QList<KeyRef> m_dragKeys;
    QRectF m_rubber;
    QList<KeyRef> m_rubberBase;
};
