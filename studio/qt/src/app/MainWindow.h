// The Studio window, laid out like Source Filmmaker:
//   left  : Animation Set Editor | Element Viewer | Asset Browser (tabs)
//   centre: Primary Viewport - the Studio's own 3D picture of the film (standalone), or the docked game (live),
//           between its timecode header and tool / transport bar
//   right : Properties
//   bottom: Timeline (Clip Editor F2 · Motion Editor F3 · Graph Editor F4) | Console
// Every panel is a dock: drag it anywhere, tab it, float it, close it; Windows menu brings it back and
// "Reset Layout" restores this arrangement. Panel contents are QML sharing one engine.
#pragma once

#include <QMainWindow>
#include <QPointer>
#include <QTimer>

class QQmlEngine;
class QQuickWidget;
class QDockWidget;
class QLabel;
class QStackedWidget;
class QMenu;
class Studio;

class GameHost : public QWidget
{
    Q_OBJECT
public:
    explicit GameHost(Studio *studio, QWidget *parent = nullptr);
    QRect gameRect() const;             // physical screen pixels, largest 16:9 box that fits
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
private:
    Studio *m_studio;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(QQmlEngine *engine, Studio *studio, QWidget *parent = nullptr);
    ~MainWindow() override;
    QVariant popup(const QVariantList &items, const QPoint &globalPos);

protected:
    void closeEvent(QCloseEvent *e) override;
    bool nativeEvent(const QByteArray &type, void *message, qintptr *result) override;
    void showEvent(QShowEvent *e) override;

private:
    QQuickWidget *quick(const QString &qml, QWidget *parent = nullptr);
    QDockWidget *dock(const QString &title, const QString &name, const QString &qml);
    void buildMenus();
    void buildDocks();
    void defaultLayout();
    void syncGame();
    void updateStatus();
    void showQmlDialog(const QString &title, const QString &qml, const QSize &size);
    void key(const QString &what);      // timeline / edit shortcuts that depend on the selection
    double frame() const;
    void promptProjectName(bool saveAs);
    void openProject();
    void showGames();
    void applyRuntimeMode();
    void connectToGame();

    QQmlEngine *m_engine;
    Studio *m_studio;
    GameHost *m_host = nullptr;
    QStackedWidget *m_centre = nullptr;
    QQuickWidget *m_viewport = nullptr;
    QList<QDockWidget *> m_docks;
    QDockWidget *m_ase = nullptr, *m_elem = nullptr, *m_assets = nullptr, *m_props = nullptr, *m_timeline = nullptr, *m_console = nullptr;
    QLabel *m_conn = nullptr, *m_frameLbl = nullptr, *m_renderLbl = nullptr, *m_projLbl = nullptr;
    QMenu *m_windows = nullptr;
    QList<QAction *> m_liveOnly;        // commands that only make sense with the running game
    QList<QAction *> m_standaloneOnly;  // and those only for the Studio's own renderer
    QTimer m_gameTimer, m_statusTimer;
    QString m_lastEditor = QStringLiteral("clip");
    QVariantMap m_clipClip;             // copied clip {track, id}
    bool m_lastCopyKeys = false;
    QByteArray m_defaultState;
};
