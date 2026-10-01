// Base of the painted time views (Clip / Motion editor lanes and the Graph editor): the shared time ruler with
// the playhead, the In/Out time selection with its falloff handles, zoom and pan. Zoom and scroll live in the
// Studio, so switching editors keeps the same view of time.
//   wheel: zoom around the mouse · Ctrl+wheel / middle-drag: pan · Shift+wheel: scroll the tracks
#pragma once

#include <QCursor>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

class Studio;

class TimeCanvas : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QObject *core READ studioObj WRITE setStudio NOTIFY studioChanged)   // the Studio object (named "core": a property called "studio" would shadow the context property in QML)
    Q_PROPERTY(int headerWidth READ headerWidth WRITE setHeaderWidth NOTIFY headerWidthChanged)
    Q_PROPERTY(double contentHeight READ contentHeight NOTIFY contentHeightChanged)
    Q_PROPERTY(double vscroll READ vscroll WRITE setVscroll NOTIFY vscrollChanged)
public:
    static constexpr int RulerH = 24;
    explicit TimeCanvas(QQuickItem *parent = nullptr);

    QObject *studioObj() const;
    void setStudio(QObject *s);
    int headerWidth() const { return m_header; }
    void setHeaderWidth(int w);
    double contentHeight() const { return m_contentHeight; }
    double vscroll() const { return m_vscroll; }
    void setVscroll(double v);

    Q_INVOKABLE void frameAll();          // fit the whole sequence (or the time selection) into view
    Q_INVOKABLE void zoomBy(double factor, double aroundX = -1);

signals:
    void studioChanged();
    void headerWidthChanged();
    void contentHeightChanged();
    void vscrollChanged();

protected:
    double xOf(double t) const;
    double tOf(double x) const;
    double ppf() const;
    double scroll() const;
    double seqT() const;
    double seqLength() const;
    int fps() const;
    bool hasRange(double *a = nullptr, double *b = nullptr) const;
    void falloff(double *fin, double *fout) const;
    void setContentHeight(double h);
    Studio *studio() const { return m_studio; }

    void paintRuler(QPainter *p, bool showFalloff);
    void paintPlayhead(QPainter *p);
    void paintRangeShade(QPainter *p, int top);
    // returns true when the ruler consumed the press
    bool rulerPress(QMouseEvent *e, bool showFalloff);
    bool rulerMove(QMouseEvent *e);
    bool rulerRelease(QMouseEvent *e);
    bool panPress(QMouseEvent *e);
    bool panMove(QMouseEvent *e);
    bool panRelease(QMouseEvent *e);
    void wheelEvent(QWheelEvent *e) override;
    virtual void onStateChanged() { update(); }

    QPointer<Studio> m_studio;
    int m_header = 220;
    double m_contentHeight = 0;
    double m_vscroll = 0;

private:
    enum class RulerDrag { None, Seek, RangeA, RangeB, FalloffIn, FalloffOut, Pan };
    RulerDrag m_rdrag = RulerDrag::None;
    double m_lastSent = -1e9;
    QPointF m_panStart;
    double m_panScroll = 0, m_panV = 0;
};
