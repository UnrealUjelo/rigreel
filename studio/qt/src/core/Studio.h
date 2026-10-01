// The one object QML talks to (context property `studio`). It owns the bridge to the runtime, the docked game
// window and the host services (render, import, sounds, files), and holds the editor state the C++ timeline
// views share with QML (editor mode, key selection, zoom).
#pragma once

#include <QJSValue>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QVariantMap>

#include <functional>

class QQmlEngine;
class Bridge;
class GameWindow;
class RenderJob;
class AnimImporter;
class SoundCatalog;
class GuideAudio;
class GameLibrary;
class StageRuntime;
class StageScene;
class FilmRender;

struct KeyRef {
    int track = 0;
    int id = 0;     // pose clip id (0 for xform / cammove keys)
    double t = 0;   // absolute frame
    bool operator==(const KeyRef &o) const { return track == o.track && id == o.id && std::abs(t - o.t) < 0.5; }
};

class Studio : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QJSValue state READ stateJs NOTIFY stateChanged)
    Q_PROPERTY(QJSValue data READ dataJs NOTIFY dataChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectionChanged)
    Q_PROPERTY(QVariantMap game READ game NOTIFY gameChanged)
    Q_PROPERTY(QVariantMap render READ render NOTIFY renderChanged)
    Q_PROPERTY(QVariantMap importProgress READ importProgress NOTIFY importChanged)
    Q_PROPERTY(int dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(bool gameFocused READ gameFocused NOTIFY gameFocusChanged)
    Q_PROPERTY(bool detached READ detached NOTIFY gameChanged)
    // editor state shared with the C++ timeline views
    Q_PROPERTY(QString editor READ editor WRITE setEditor NOTIFY editorChanged)            // clip | motion | graph
    Q_PROPERTY(QVariantList keySel READ keySelList WRITE setKeySelList NOTIFY keySelChanged)
    Q_PROPERTY(int keyCount READ keyCount NOTIFY keySelChanged)
    Q_PROPERTY(int graphTrack READ graphTrack WRITE setGraphTrack NOTIFY graphTrackChanged)
    Q_PROPERTY(double ppf READ ppf WRITE setPpf NOTIFY viewChanged)                        // pixels per frame
    Q_PROPERTY(double scroll READ scroll WRITE setScroll NOTIFY viewChanged)               // first visible frame
    Q_PROPERTY(QString version READ version CONSTANT)
    // which runtime animates the film: "standalone" (the Studio, reading the game's files) or "live" (inside the
    // running game, through the bridge)
    Q_PROPERTY(QString runtimeMode READ runtimeMode WRITE setRuntimeMode NOTIFY runtimeModeChanged)
    Q_PROPERTY(bool standalone READ standalone NOTIFY runtimeModeChanged)
    Q_PROPERTY(QObject *games READ gamesObject CONSTANT)
    // the 3D view (standalone): shading, navigation keys, the view's name (Blender's "User Perspective")
    Q_PROPERTY(QString shading READ shading WRITE setShading NOTIFY viewportChanged)
    Q_PROPERTY(QString navStyle READ navStyle WRITE setNavStyle NOTIFY viewportChanged)   // blender | sfm
    Q_PROPERTY(bool ortho READ ortho NOTIFY viewportChanged)
    Q_PROPERTY(QString viewName READ viewName NOTIFY viewportChanged)

public:
    Studio(QQmlEngine *engine, QObject *parent = nullptr);
    ~Studio() override;

    Bridge *bridge() const { return m_bridge; }
    GameWindow *gameWindow() const { return m_game; }
    GameLibrary *games() const { return m_games; }
    QObject *gamesObject() const;
    StageRuntime *stage() const { return m_stage; }
    QString runtimeMode() const { return m_mode; }
    void setRuntimeMode(const QString &mode);
    bool standalone() const { return m_mode == QLatin1String("standalone"); }

    // --- standalone viewport ---
    Q_INVOKABLE void attachScene(QObject *scene);
    Q_INVOKABLE void setFlying(bool on);
    Q_INVOKABLE QVariantMap flyKeys() const;
    Q_INVOKABLE QString studioSky() const;
    Q_INVOKABLE void frameSelection();
    // front back right left top bottom | ortho | flip | camera | all | orbit:left right up down
    Q_INVOKABLE void viewCommand(const QString &what);
    QString shading() const;
    void setShading(const QString &s);
    QString navStyle() const;
    void setNavStyle(const QString &s);
    bool ortho() const;
    QString viewName() const;
    Q_INVOKABLE void showGames() { emit gamesRequested(); }
    // folder picker for a game Director did not find by itself; false when no plugin recognises it
    Q_INVOKABLE bool addGameFolder();
    bool eventFilter(QObject *watched, QEvent *event) override;
    GuideAudio *guideAudio() const { return m_audio; }
    QQmlEngine *engine() const { return m_engine; }
    const QJsonObject &stateObj() const { return m_state; }
    // where rigs, imports and exports live: the game's director folder (live) or Documents/Director Studio
    QString importRoot() const;
    const QJsonObject &dataObj() const { return m_data; }
    QJsonObject sequence() const { return m_state.value(QStringLiteral("sequence")).toObject(); }

    QJSValue stateJs() const { return m_stateJs; }
    QJSValue dataJs() const { return m_dataJs; }
    bool connected() const;
    QVariantMap game() const;
    QVariantMap render() const;
    QVariantMap importProgress() const;
    int dirty() const { return m_dirty; }
    bool gameFocused() const { return m_gameFocused; }
    bool detached() const { return m_detached; }
    QString version() const { return QStringLiteral("0.1.0-alpha.1"); }

    QString editor() const { return m_editor; }
    void setEditor(const QString &e);
    QList<KeyRef> keySel() const { return m_keySel; }
    void setKeySel(const QList<KeyRef> &k);
    QVariantList keySelList() const;
    void setKeySelList(const QVariantList &l);
    int keyCount() const { return int(m_keySel.size()); }
    int graphTrack() const { return m_graphTrack; }
    void setGraphTrack(int t);
    double ppf() const { return m_ppf; }
    void setPpf(double p);
    double scroll() const { return m_scroll; }
    void setScroll(double s);

    // --- runtime commands ---
    Q_INVOKABLE qint64 send(const QString &op, const QVariantMap &args = {});
    Q_INVOKABLE void cmd(const QString &op, const QVariantMap &args = {});   // send + unsaved-changes tracking
    qint64 sendJson(const QJsonObject &c);

    // --- host services ---
    Q_INVOKABLE QString pickFile(const QString &kind);
    Q_INVOKABLE QString openFolder(const QString &which);
    Q_INVOKABLE bool startRender(const QVariantMap &opts);
    Q_INVOKABLE void cancelRender();
    Q_INVOKABLE bool importAnim(const QString &path, double addr, double start);
    Q_INVOKABLE QVariantMap searchSounds(const QString &q, const QString &category, int limit = 200);
    Q_INVOKABLE void focusGame();
    Q_INVOKABLE void focusStudio();
    Q_INVOKABLE void toggleFocus();
    Q_INVOKABLE void setDetached(bool on);
    Q_INVOKABLE QString poseThumbnail(const QString &name);
    Q_INVOKABLE QString poseThumbUrl(const QString &name) const;
    Q_INVOKABLE QString fileUrl(const QString &path) const;
    Q_INVOKABLE QStringList runtimeLog(int n = 80) const;
    Q_INVOKABLE void copyText(const QString &text);
    Q_INVOKABLE QString fmtTime(double frames) const;       // m:ss.xx
    Q_INVOKABLE QString timecode(double frames) const;      // hh:mm:ss.mmm
    Q_INVOKABLE void markSaved();
    Q_INVOKABLE void setStatus(const QString &text, int ms = 4000);
    Q_INVOKABLE void showRenderDialog();
    Q_INVOKABLE void requestMenu(const QString &name);       // QML asks the main window to open a native menu

    // a native menu at the mouse (never clipped, floats over the game): items = [{text, value, checked?, enabled?,
    // shortcut?, tip?} | {separator:true} | {header:"..."} | {text, items:[...]}]; returns the chosen value or null
    Q_INVOKABLE QVariant popup(const QVariantList &items);
    // one-line text question (rename a shot, name a pose ...); returns "" when cancelled
    Q_INVOKABLE QString prompt(const QString &title, const QString &label, const QString &text = QString());
    Q_INVOKABLE bool confirm(const QString &title, const QString &text);
    void setPopupHandler(std::function<QVariant(const QVariantList &)> h) { m_popup = std::move(h); }

    // key selection helpers used by QML menus
    Q_INVOKABLE QVariantList keySelItems() const { return keySelList(); }
    Q_INVOKABLE void clearKeySel() { setKeySel({}); }

    // host state as the control server / MCP expect it ({state, data_ver, connected, game, pending, errors, render, import})
    QJsonObject hostStateJson() const;
    QJsonObject renderJson() const;
    QJsonObject importJson() const;

    void setGameFocused(bool f);

signals:
    void stateChanged();
    void dataChanged();
    void connectionChanged();
    void gameChanged();
    void renderChanged();
    void importChanged();
    void dirtyChanged();
    void gameFocusChanged();
    void editorChanged();
    void keySelChanged();
    void graphTrackChanged();
    void viewChanged();
    void statusMessage(const QString &text, int ms);
    void renderDialogRequested();
    void menuRequested(const QString &name);
    void detachedChanged(bool on);
    void runtimeModeChanged();
    void viewportChanged();
    void gamesRequested();

private:
    void onState();
    void onData();
    void adoptState(const QJsonObject &st);
    QJSValue toJs(const QJsonObject &o) const;

    QQmlEngine *m_engine;
    Bridge *m_bridge;
    GameWindow *m_game;
    RenderJob *m_render = nullptr;
    AnimImporter *m_import;
    SoundCatalog *m_sounds;
    GuideAudio *m_audio;
    QJsonObject m_state, m_data;
    QJSValue m_stateJs, m_dataJs;
    int m_dirty = 0;
    bool m_gameFocused = false;
    bool m_detached = false;
    QString m_editor = QStringLiteral("clip");
    QList<KeyRef> m_keySel;
    int m_graphTrack = -1;
    double m_ppf = 1.0;
    double m_scroll = 0.0;
    QTimer m_focusTimer;
    std::function<QVariant(const QVariantList &)> m_popup;
    GameLibrary *m_games = nullptr;
    StageRuntime *m_stage = nullptr;
    QPointer<StageScene> m_scene;
    FilmRender *m_filmRender = nullptr;
    QString m_mode = QStringLiteral("standalone");
    bool m_flying = false;
    QSet<int> m_keysDown;
};
