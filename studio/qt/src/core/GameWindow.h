// The RE4 window, made borderless and OWNED by the Studio window (not a child): it stays above the Studio,
// hides and minimizes with it, and - unlike a child window - keeps normal keyboard and mouse focus. The Studio
// positions it over the viewport every time the viewport moves. An unclean exit can leave the game owned by a
// dead window; the next embed simply takes it over again, and release() puts back the original frame.
#pragma once

#include <QObject>
#include <QRect>
#include <QVariantMap>
#include <QMutex>

class GameWindow : public QObject
{
    Q_OBJECT
public:
    explicit GameWindow(QObject *parent = nullptr);
    ~GameWindow() override;

    quintptr find();                          // the game's top-level window (0 = not running)
    bool embed(quintptr owner, const QRect &screenRect);
    void setRect(const QRect &screenRect, bool force = false);
    void release();
    // render mode: exactly w x h in the top-left corner, above everything, so frame grabs are 1:1
    QRect renderMode(bool on, int w = 1920, int h = 1080);
    bool rendering() const { return m_rendering; }

    void focus();
    bool isForeground() const;
    void unclipIfClipped();
    void keepCursorCentred();                 // fly camera: the game only reports motion while the cursor can move

    bool embedded() const { return m_embedded; }
    quintptr hwnd() const { return m_hwnd; }
    QRect rect() const { return m_rect; }
    QVariantMap status() const;

    static void forceForeground(quintptr hwnd);

private:
    mutable QMutex m_lock;
    quintptr m_hwnd = 0;
    quintptr m_owner = 0;
    qint64 m_origStyle = 0, m_origExStyle = 0, m_origOwner = 0;
    QRect m_origRect;
    bool m_haveOrig = false;
    bool m_embedded = false;
    bool m_rendering = false;
    QRect m_rect;
};
