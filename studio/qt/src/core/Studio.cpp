#include "Studio.h"

#include "AnimImporter.h"
#include "Bridge.h"
#include "Capture.h"
#include "GameWindow.h"
#include "GuideAudio.h"
#include "Paths.h"
#include "RenderJob.h"
#include "SoundCatalog.h"

#include "FilmRender.h"
#include "GameLibrary.h"
#include "StageRuntime.h"
#include "StageScene.h"
#include "TextureStore.h"

#include <QStandardPaths>
#include <QApplication>
#include <QDateTime>
#include <QKeyEvent>
#include <QSettings>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlEngine>
#include <QSet>
#include <QRegularExpression>
#include <QUrl>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace {

// commands that do not change the film (no "unsaved changes" dot)
const QSet<QString> &quietOps()
{
    static const QSet<QString> s{
        QStringLiteral("ping"), QStringLiteral("catalog_search"), QStringLiteral("mesh_search"), QStringLiteral("mesh_preview"),
        QStringLiteral("mesh_preview_clear"), QStringLiteral("cast_preview"), QStringLiteral("cast_preview_clear"), QStringLiteral("want_objects"),
        QStringLiteral("select_actor"), QStringLiteral("select_clip"), QStringLiteral("cam_select"), QStringLiteral("select_joint"), QStringLiteral("gizmo"),
        QStringLiteral("refresh"), QStringLiteral("seq_seek"), QStringLiteral("sound_preview"), QStringLiteral("sound_stop"), QStringLiteral("stage_list"),
        QStringLiteral("stage_cancel"), QStringLiteral("actor_visible"), QStringLiteral("pick_props"), QStringLiteral("ragdoll_strength"),
        QStringLiteral("seq_play"), QStringLiteral("seq_pause"), QStringLiteral("seq_toggle"), QStringLiteral("seq_stop"), QStringLiteral("seq_speed"),
        QStringLiteral("copy_keys"), QStringLiteral("undo"), QStringLiteral("redo"), QStringLiteral("light_select"), QStringLiteral("select_clear"),
        QStringLiteral("cam_live"), QStringLiteral("cam_work"), QStringLiteral("cam_scene"), QStringLiteral("cam_fly"), QStringLiteral("edit_mode"),
        QStringLiteral("load_motlist"), QStringLiteral("play"), QStringLiteral("hud"), QStringLiteral("overlay_skeleton"), QStringLiteral("overlay_onion"),
        QStringLiteral("eval"), QStringLiteral("game_freeze"), QStringLiteral("autokey"), QStringLiteral("motion_edit"), QStringLiteral("shot_go"),
        QStringLiteral("project_load"), QStringLiteral("project_new")};
    return s;
}

} // namespace

Studio::Studio(QQmlEngine *engine, QObject *parent)
    : QObject(parent), m_engine(engine)
{
    m_bridge = new Bridge(Paths::bridgeDir(), this);
    m_game = new GameWindow(this);
    m_render = new RenderJob(m_bridge, m_game, this);
    m_import = new AnimImporter(this, this);
    m_sounds = new SoundCatalog();
    m_sounds->preload();
    m_audio = new GuideAudio(this);

    // the games the user owns (plugins/games/*.dll) and the standalone runtime that animates with them
    m_games = new GameLibrary(this);
    m_games->loadPlugins(QCoreApplication::applicationDirPath() + QStringLiteral("/plugins/games"));
    m_games->refresh();
    m_stage = new StageRuntime(m_games, this);
    if (QSettings().value(QStringLiteral("runtime/mode")).toString() == QLatin1String("live")) m_mode = QStringLiteral("live");
    m_filmRender = new FilmRender(this);
    {
        // reopen the last game, else the first one installed
        const QString last = QSettings().value(QStringLiteral("games/active")).toString();
        QString pick;
        for (const QVariant &g : m_games->gamesJs()) {
            const QString key = g.toMap().value(QStringLiteral("key")).toString();
            if (key == last || pick.isEmpty()) pick = key;
            if (key == last) break;
        }
        if (!pick.isEmpty()) m_games->activate(pick);
    }
    connect(m_games, &GameLibrary::activeChanged, this, &Studio::connectionChanged);
    connect(m_filmRender, &FilmRender::progressChanged, this, &Studio::renderChanged);
    connect(m_stage, &StageRuntime::stateChanged, this, [this] { if (standalone()) adoptState(m_stage->state()); });
    connect(m_stage, &StageRuntime::dataChanged, this, [this] {
        if (!standalone()) return;
        m_data = Bridge::normalizeData(m_stage->data());
        m_dataJs = toJs(m_data);
        emit dataChanged();
    });
    qApp->installEventFilter(this);

    // panels always get a valid (possibly empty) state, never null
    m_state = Bridge::normalizeState(m_stage->state());
    m_stateJs = toJs(m_state);
    m_data = Bridge::normalizeData(m_stage->data());
    m_dataJs = toJs(m_data);
    connect(m_bridge, &Bridge::stateChanged, this, &Studio::onState);
    connect(m_bridge, &Bridge::dataChanged, this, &Studio::onData);
    connect(m_bridge, &Bridge::connectionChanged, this, [this](bool) {
        if (!standalone() && !m_bridge->connected()) { // never show a frozen picture of the last session
            m_state = Bridge::normalizeState(QJsonObject());
            m_stateJs = toJs(m_state);
            emit stateChanged();
        }
        emit connectionChanged();
    });
    connect(m_render, &RenderJob::progressChanged, this, &Studio::renderChanged);
    connect(m_import, &AnimImporter::progressChanged, this, &Studio::importChanged);
    connect(m_audio, &GuideAudio::durationKnown, this, [this](double secs) {
        const QJsonObject a = sequence().value(QStringLiteral("audio")).toObject();
        if (!a.isEmpty() && std::abs(a.value(QStringLiteral("duration")).toDouble() - secs) > 0.05)
            send(QStringLiteral("seq_audio"), {{QStringLiteral("duration"), secs}});
    });

    // focus + cursor housekeeping: the runtime must know whether keys go to the game; RE4 clips the cursor
    // to its window, and the fly camera needs the cursor kept centred to keep receiving mouse motion
    connect(&m_focusTimer, &QTimer::timeout, this, [this] {
        if (standalone()) return;
        const bool f = m_game->isForeground();
        setGameFocused(f);
        if (f) {
            const bool fly = m_state.value(QStringLiteral("camera_fly")).toObject().value(QStringLiteral("on")).toBool()
                || m_state.value(QStringLiteral("grab")).isObject();
            if (fly && m_bridge->connected())
                m_game->keepCursorCentred();
            else
                m_game->unclipIfClipped();
        }
    });
    m_focusTimer.start(8);
}

Studio::~Studio()
{
    m_game->release();
    delete m_sounds;
}

bool Studio::connected() const { return standalone() ? m_games->ready() : m_bridge->connected(); }

QObject *Studio::gamesObject() const { return m_games; }

void Studio::setRuntimeMode(const QString &mode)
{
    const QString m = mode == QLatin1String("live") ? QStringLiteral("live") : QStringLiteral("standalone");
    if (m == m_mode) return;
    m_mode = m;
    QSettings().setValue(QStringLiteral("runtime/mode"), m_mode);
    if (standalone()) {
        m_game->release();
        adoptState(m_stage->state());
        m_data = Bridge::normalizeData(m_stage->data());
    } else {
        m_state = m_bridge->connected() ? m_bridge->state() : Bridge::normalizeState(QJsonObject());
        m_stateJs = toJs(m_state);
        m_data = m_bridge->data();
        emit stateChanged();
    }
    m_dataJs = toJs(m_data);
    emit dataChanged();
    emit runtimeModeChanged();
    emit connectionChanged();
    emit gameChanged();
}

void Studio::attachScene(QObject *scene)
{
    m_scene = qobject_cast<StageScene *>(scene);
    if (!m_scene) return;
    m_scene->setShading(QSettings().value(QStringLiteral("view/shading"), QStringLiteral("rendered")).toString());
    connect(m_scene, &StageScene::viewChanged, this, &Studio::viewportChanged, Qt::UniqueConnection);
    m_scene->setRuntime(m_stage);
    m_stage->setScene(m_scene);
    m_filmRender->setScene(m_scene);
}

void Studio::setFlying(bool on)
{
    m_flying = on;
    if (!on) m_keysDown.clear();
}

QVariantMap Studio::flyKeys() const
{
    auto k = [this](int key) { return m_keysDown.contains(key) ? 1 : 0; };
    return {{QStringLiteral("forward"), k(Qt::Key_W) - k(Qt::Key_S)}, {QStringLiteral("right"), k(Qt::Key_D) - k(Qt::Key_A)},
            {QStringLiteral("up"), k(Qt::Key_E) - k(Qt::Key_Q)}, {QStringLiteral("fast"), bool(k(Qt::Key_Shift))}};
}

QString Studio::studioSky() const { return TextureStore::studioSky().toString(); }

void Studio::frameSelection() { if (m_scene) m_scene->frameSelection(); }

void Studio::viewCommand(const QString &what)
{
    if (!m_scene || !standalone()) return;
    if (what == QLatin1String("ortho")) m_scene->setOrtho(!m_scene->ortho());
    else if (what == QLatin1String("flip")) m_scene->flipView();
    else if (what == QLatin1String("all")) m_scene->frameAll();
    else if (what == QLatin1String("selected")) m_scene->frameSelection();
    else if (what == QLatin1String("orbit:left")) m_scene->orbitStep(-15, 0);
    else if (what == QLatin1String("orbit:right")) m_scene->orbitStep(15, 0);
    else if (what == QLatin1String("orbit:up")) m_scene->orbitStep(0, 15);
    else if (what == QLatin1String("orbit:down")) m_scene->orbitStep(0, -15);
    else if (what == QLatin1String("camera")) {
        // numpad 0: through the film's camera, and back to the Work Camera
        const QJsonObject st = m_state;
        const bool cams = !st.value(QStringLiteral("cameras")).toArray().isEmpty();
        const bool work = st.value(QStringLiteral("camera_view")).toString() == QLatin1String("work") || st.value(QStringLiteral("active_camera")).toInt() == 0;
        if (work && cams) send(QStringLiteral("cam_scene"), {});
        else send(QStringLiteral("cam_work"), {{QStringLiteral("value"), true}});
    } else m_scene->viewAxis(what);
}

QString Studio::shading() const { return m_scene ? m_scene->shading() : QSettings().value(QStringLiteral("view/shading"), QStringLiteral("rendered")).toString(); }

void Studio::setShading(const QString &s)
{
    QSettings().setValue(QStringLiteral("view/shading"), s);
    if (m_scene) m_scene->setShading(s);
    emit viewportChanged();
}

QString Studio::navStyle() const { return QSettings().value(QStringLiteral("view/nav"), QStringLiteral("blender")).toString(); }

void Studio::setNavStyle(const QString &s)
{
    QSettings().setValue(QStringLiteral("view/nav"), s);
    emit viewportChanged();
}

bool Studio::ortho() const { return m_scene && m_scene->ortho(); }
QString Studio::viewName() const { return m_scene ? m_scene->viewName() : QStringLiteral("User"); }

bool Studio::addGameFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(nullptr, tr("The game's install folder"), QString());
    if (dir.isEmpty()) return false;
    if (m_games->addFolder(dir)) {
        setStatus(tr("Added %1").arg(QDir::toNativeSeparators(dir)));
        return true;
    }
    QMessageBox::information(nullptr, tr("Add a Game Folder"), tr("No game plugin recognises this folder.\n\nPick the folder that holds the game's .exe and its .pak archives."));
    return false;
}

// While the mouse flies the Work Camera, W A S D Q E belong to the flight, not to the tool shortcuts (SFM).
bool Studio::eventFilter(QObject *watched, QEvent *event)
{
    if (m_flying && (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)) {
        auto *ke = static_cast<QKeyEvent *>(event);
        static const QSet<int> fly{Qt::Key_W, Qt::Key_A, Qt::Key_S, Qt::Key_D, Qt::Key_Q, Qt::Key_E, Qt::Key_Shift};
        if (fly.contains(ke->key())) {
            if (event->type() == QEvent::KeyPress) m_keysDown.insert(ke->key());
            else if (event->type() == QEvent::KeyRelease && !ke->isAutoRepeat()) m_keysDown.remove(ke->key());
            event->accept();
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

void Studio::adoptState(const QJsonObject &st)
{
    m_state = Bridge::normalizeState(st);
    m_stateJs = toJs(m_state);
    m_audio->sync(sequence());
    emit stateChanged();
    emit gameChanged();
}
QVariantMap Studio::game() const { return m_game->status(); }
QVariantMap Studio::render() const { return renderJson().toVariantMap(); }
QVariantMap Studio::importProgress() const { return m_import->progress().toVariantMap(); }
QJsonObject Studio::renderJson() const
{
    return (standalone() || m_filmRender->running()) && !m_render->running() ? m_filmRender->progress() : m_render->progress();
}
QJsonObject Studio::importJson() const { return m_import->progress(); }

QJSValue Studio::toJs(const QJsonObject &o) const
{
    // one JS object per update: bindings read `studio.state.sequence.t` without copying the whole map again
    return m_engine->toScriptValue(o);
}

void Studio::onState()
{
    if (standalone()) return;
    m_state = m_bridge->state();
    m_stateJs = toJs(m_state);
    m_audio->sync(sequence());
    // drop selected keys that no longer exist
    if (!m_keySel.isEmpty()) {
        QList<KeyRef> all;
        for (const QJsonValue &tv : sequence().value(QStringLiteral("tracks")).toArray()) {
            const QJsonObject t = tv.toObject();
            const int idx = t.value(QStringLiteral("idx")).toInt();
            const QString kind = t.value(QStringLiteral("kind")).toString();
            if (kind == QLatin1String("xform") || kind == QLatin1String("cammove")) {
                for (const QJsonValue &k : t.value(QStringLiteral("keys")).toArray())
                    all.append({idx, 0, k.toObject().value(QStringLiteral("t")).toDouble()});
            } else if (kind == QLatin1String("pose")) {
                for (const QJsonValue &cv : t.value(QStringLiteral("clips")).toArray()) {
                    const QJsonObject c = cv.toObject();
                    for (const QJsonValue &k : c.value(QStringLiteral("keys")).toArray())
                        all.append({idx, c.value(QStringLiteral("id")).toInt(), c.value(QStringLiteral("start")).toDouble() + k.toDouble()});
                }
            }
        }
        QList<KeyRef> kept;
        for (const KeyRef &k : m_keySel)
            if (all.contains(k))
                kept.append(k);
        if (kept.size() != m_keySel.size())
            setKeySel(kept);
    }
    emit stateChanged();
    emit gameChanged();
}

void Studio::onData()
{
    if (standalone()) return;
    m_data = m_bridge->data();
    m_dataJs = toJs(m_data);
    emit dataChanged();
}

qint64 Studio::sendJson(const QJsonObject &c)
{
    if (standalone()) {
        static qint64 cid = QDateTime::currentMSecsSinceEpoch();
        QJsonObject x = c;
        x.insert(QStringLiteral("cid"), double(++cid));
        m_stage->send(c.value(QStringLiteral("op")).toString(), x);
        return cid;
    }
    return m_bridge->send(c);
}

qint64 Studio::send(const QString &op, const QVariantMap &args)
{
    QJsonObject c = QJsonObject::fromVariantMap(args);
    c.insert(QStringLiteral("op"), op);
    return sendJson(c);
}

QString Studio::importRoot() const
{
    return standalone() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/Director Studio") : Paths::directorDir();
}

void Studio::cmd(const QString &op, const QVariantMap &args)
{
    send(op, args);
    if (op == QLatin1String("project_save") || op == QLatin1String("project_load") || op == QLatin1String("project_new")) {
        m_dirty = 0;
        emit dirtyChanged();
    } else if (!quietOps().contains(op)) {
        ++m_dirty;
        emit dirtyChanged();
    }
}

void Studio::markSaved() { m_dirty = 0; emit dirtyChanged(); }

void Studio::setGameFocused(bool f)
{
    m_bridge->setGameFocused(f);
    if (f != m_gameFocused) {
        m_gameFocused = f;
        emit gameFocusChanged();
    }
}

// ---------------------------------------------------------------------------------------------- editor state
void Studio::setEditor(const QString &e)
{
    if (e == m_editor || (e != QLatin1String("clip") && e != QLatin1String("motion") && e != QLatin1String("graph")))
        return;
    m_editor = e;
    emit editorChanged();
}

void Studio::setKeySel(const QList<KeyRef> &k)
{
    if (k == m_keySel)
        return;
    m_keySel = k;
    emit keySelChanged();
}

QVariantList Studio::keySelList() const
{
    QVariantList out;
    for (const KeyRef &k : m_keySel) {
        QVariantMap m{{QStringLiteral("track"), k.track}, {QStringLiteral("t"), k.t}};
        if (k.id)
            m.insert(QStringLiteral("id"), k.id);
        out.append(m);
    }
    return out;
}

void Studio::setKeySelList(const QVariantList &l)
{
    QList<KeyRef> out;
    for (const QVariant &v : l) {
        const QVariantMap m = v.toMap();
        out.append({m.value(QStringLiteral("track")).toInt(), m.value(QStringLiteral("id")).toInt(), m.value(QStringLiteral("t")).toDouble()});
    }
    setKeySel(out);
}

void Studio::setGraphTrack(int t) { if (t != m_graphTrack) { m_graphTrack = t; emit graphTrackChanged(); } }
void Studio::setPpf(double p) { p = std::clamp(p, 0.05, 12.0); if (p != m_ppf) { m_ppf = p; emit viewChanged(); } }
void Studio::setScroll(double s) { s = std::max(-20.0, s); if (s != m_scroll) { m_scroll = s; emit viewChanged(); } }

// ---------------------------------------------------------------------------------------------- host services
QString Studio::pickFile(const QString &kind)
{
    QString filter = QStringLiteral("All files (*.*)");
    if (kind == QLatin1String("audio"))
        filter = QStringLiteral("Audio (*.mp3 *.wav *.ogg *.m4a *.flac *.aac)");
    else if (kind == QLatin1String("anim"))
        filter = QStringLiteral("Animation (*.fbx *.bvh *.glb *.gltf *.dae *.json)");
    return QFileDialog::getOpenFileName(QApplication::activeWindow(), kind == QLatin1String("anim") ? tr("Import animation") : tr("Choose a file"), QString(), filter);
}

QString Studio::openFolder(const QString &which)
{
    // "renders" = where Render writes (Downloads); everything else is a director/ data folder (export, projects, import, poses)
    const QString p = which == QLatin1String("renders") ? Paths::downloads()
        : which == QLatin1String("bridge") ? Paths::bridgeDir()
        : Paths::directorDir() + QLatin1Char('/') + which;
    QDir().mkpath(p);
    QDesktopServices::openUrl(QUrl::fromLocalFile(p));
    return p;
}

bool Studio::startRender(const QVariantMap &o)
{
    if (standalone()) return m_filmRender->start(o, m_stage);
    RenderJob::Options opts;
    opts.out = o.value(QStringLiteral("out")).toString();
    if (opts.out.isEmpty()) {
        QString name = o.value(QStringLiteral("name"), QStringLiteral("director_shot")).toString().trimmed();
        name.remove(QRegularExpression(QStringLiteral("\\.mp4$"), QRegularExpression::CaseInsensitiveOption));
        name.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*]")), QStringLiteral("_"));
        if (name.isEmpty())
            name = QStringLiteral("director_shot");
        opts.out = Paths::downloads() + QLatin1Char('/') + name + QStringLiteral(".mp4");
    }
    opts.fps = o.value(QStringLiteral("fps"), 30).toInt();
    if (o.contains(QStringLiteral("from")) && !o.value(QStringLiteral("from")).isNull())
        opts.start = o.value(QStringLiteral("from")).toDouble();
    if (o.contains(QStringLiteral("to")) && !o.value(QStringLiteral("to")).isNull())
        opts.end = o.value(QStringLiteral("to")).toDouble();
    opts.width = o.value(QStringLiteral("width"), 1920).toInt();
    opts.height = o.value(QStringLiteral("height"), 1080).toInt();
    opts.quality = o.value(QStringLiteral("quality"), QStringLiteral("final")).toString();
    opts.pngSeq = o.value(QStringLiteral("png_seq")).toBool();
    opts.format = o.value(QStringLiteral("output_format"), QStringLiteral("mp4")).toString();
    opts.gameAudio = o.value(QStringLiteral("game_audio")).toBool();
    return m_render->start(opts);
}

void Studio::cancelRender()
{
    if (m_filmRender->running()) m_filmRender->cancel();
    else m_render->cancel();
}

bool Studio::importAnim(const QString &path, double addr, double start) { return m_import->start(path, addr, start); }

QVariantMap Studio::searchSounds(const QString &q, const QString &category, int limit) { return m_sounds->search(q, category, limit); }

// the picture takes the keyboard: the game's window in live mode, the 3D viewport in the Studio renderer
void Studio::focusGame()
{
    if (standalone()) emit menuRequested(QStringLiteral("focus:viewport"));
    else m_game->focus();
}

void Studio::focusStudio()
{
    QWidget *w = QApplication::activeWindow();
    if (!w)
        for (QWidget *t : QApplication::topLevelWidgets())
            if (t->isWindow() && t->isVisible() && t->inherits("QMainWindow")) { w = t; break; }
    if (w) {
        GameWindow::forceForeground(quintptr(w->winId()));
        w->activateWindow();
    }
}

void Studio::toggleFocus()
{
    if (m_game->isForeground())
        focusStudio();
    else
        focusGame();
}

void Studio::setDetached(bool on)
{
    if (on == m_detached)
        return;
    m_detached = on;
    emit detachedChanged(on);
    emit gameChanged();
}

QString Studio::poseThumbnail(const QString &name)
{
    const QImage img = Capture::window(m_game->find());
    if (img.isNull())
        return {};
    const QString dir = Paths::directorDir() + QStringLiteral("/poses");
    QDir().mkpath(dir);
    const QString out = dir + QLatin1Char('/') + name + QStringLiteral(".png");
    img.scaled(320, 180, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation).save(out);
    return out;
}

QString Studio::poseThumbUrl(const QString &name) const
{
    const QString p = Paths::directorDir() + QStringLiteral("/poses/") + name + QStringLiteral(".png");
    return QFile::exists(p) ? QUrl::fromLocalFile(p).toString() : QString();
}

QString Studio::fileUrl(const QString &path) const { return QUrl::fromLocalFile(path).toString(); }

QStringList Studio::runtimeLog(int n) const
{
    QFile f(Paths::directorDir() + QStringLiteral("/director_log.txt"));
    if (!f.open(QIODevice::ReadOnly))
        return {QStringLiteral("(log unavailable)")};
    const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    return lines.mid(std::max<qsizetype>(0, lines.size() - n));
}

void Studio::copyText(const QString &text) { QGuiApplication::clipboard()->setText(text); }

QString Studio::fmtTime(double frames) const
{
    const double fps = std::max(1.0, sequence().value(QStringLiteral("fps")).toDouble(60));
    const double s = frames / fps;
    const int m = int(std::floor(s / 60));
    return QStringLiteral("%1:%2").arg(m).arg(std::fmod(s, 60.0), 5, 'f', 2, QLatin1Char('0'));
}

QString Studio::timecode(double frames) const
{
    const double fps = std::max(1.0, sequence().value(QStringLiteral("fps")).toDouble(60));
    const qint64 ms = qint64(std::llround(std::max(0.0, frames) / fps * 1000.0));
    return QStringLiteral("%1:%2:%3.%4")
        .arg(ms / 3600000, 2, 10, QLatin1Char('0'))
        .arg((ms / 60000) % 60, 2, 10, QLatin1Char('0'))
        .arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

QVariant Studio::popup(const QVariantList &items) { return m_popup ? m_popup(items) : QVariant(); }

QString Studio::prompt(const QString &title, const QString &label, const QString &text)
{
    bool ok = false;
    const QString r = QInputDialog::getText(QApplication::activeWindow(), title, label, QLineEdit::Normal, text, &ok);
    return ok ? r.trimmed() : QString();
}

bool Studio::confirm(const QString &title, const QString &text)
{
    return QMessageBox::question(QApplication::activeWindow(), title, text) == QMessageBox::Yes;
}

void Studio::setStatus(const QString &text, int ms) { emit statusMessage(text, ms); }
void Studio::showRenderDialog() { emit renderDialogRequested(); }
void Studio::requestMenu(const QString &name) { emit menuRequested(name); }

QJsonObject Studio::hostStateJson() const
{
    const bool c = connected();
    QJsonObject out;
    out.insert(QStringLiteral("runtime"), m_mode);
    if (standalone()) out.insert(QStringLiteral("games"), QJsonValue::fromVariant(m_games->gamesJs()));
    out.insert(QStringLiteral("state"), c ? QJsonValue(m_state) : QJsonValue(QJsonValue::Null));
    out.insert(QStringLiteral("data_ver"), double(m_bridge->dataVersion()));
    out.insert(QStringLiteral("connected"), c);
    out.insert(QStringLiteral("game"), QJsonObject::fromVariantMap(m_game->status()));
    out.insert(QStringLiteral("pending"), standalone() ? 0 : m_bridge->pending());
    out.insert(QStringLiteral("errors"), m_bridge->errors());
    out.insert(QStringLiteral("render"), m_render->progress());
    out.insert(QStringLiteral("import"), m_import->progress());
    return out;
}
