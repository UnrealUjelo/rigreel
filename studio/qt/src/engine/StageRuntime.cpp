#include "StageRuntime.h"
#include "GameLibrary.h"
#include "Maths.h"
#include "StageScene.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtConcurrent>
#include <algorithm>

using namespace film;

StageRuntime::StageRuntime(GameLibrary *games, QObject *parent) : QObject(parent), m_games(games)
{
    m_gizmo = {{QStringLiteral("enabled"), true}, {QStringLiteral("target"), QStringLiteral("actor")}, {QStringLiteral("op"), QStringLiteral("move")},
               {QStringLiteral("mode"), QStringLiteral("world")}, {QStringLiteral("snap"), 0}};
    registerOps();
    m_clock.start();
    m_timer.setInterval(16);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &StageRuntime::tick);
    m_timer.start();
    // a fresh film: the work camera looks at the stage centre from a friendly height
    m_doc.hasWork = true;
    m_doc.work.name = QStringLiteral("Work Camera");
    m_doc.work.dof = false;
    m_doc.work.fov = 60;
    m_doc.work.pos = QVector3D(2.6f, 1.7f, 3.4f);
    m_doc.work.rot = fm::lookRotation(m_doc.work.pos, QVector3D(0, 1.0f, 0));
    if (m_games) {
        // the Asset Browser shows another game: the film keeps its characters, clips and map (each from its game)
        connect(m_games, &GameLibrary::activeChanged, this, [this]() {
            if (m_doc.gameKey.isEmpty() && m_doc.actors.isEmpty() && m_doc.stageId.isEmpty()) m_doc.gameKey = m_games->gameId();
            buildCatalogs();
            for (const Actor &a : std::as_const(m_doc.actors)) ensureModel(a.id);
            ensureStage();
            publish();
        });
        connect(m_games, &GameLibrary::sourceOpened, this, &StageRuntime::sourceOpened);
        if (m_games->ready()) buildCatalogs();
    }
    initAutosave();
    connect(qApp, &QCoreApplication::aboutToQuit, this, &StageRuntime::endSession);
    evaluate(0);
    publish();
}

StageRuntime::~StageRuntime() = default;

void StageRuntime::log(const QString &level, const QString &text)
{
    const QString line = QStringLiteral("%1 [%2] %3").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), level, text);
    m_logTail.append(line);
    while (m_logTail.size() > 60) m_logTail.removeFirst();
    emit logLine(line);
    markData();
}

StageScene *StageRuntime::scene() const { return m_scene; }

void StageRuntime::setScene(StageScene *scene)
{
    m_scene = scene;
    if (!m_scene) return;
    for (auto it = m_models.cbegin(); it != m_models.cend(); ++it) m_scene->addActor(it.key(), it.value());
    evaluate(m_t);
    m_scene->sync(*this);
}

// ------------------------------------------------------------------ commands
bool StageRuntime::isDirtyOp(const QString &op) const { return m_ops.contains(op) && !m_readonly.contains(op); }

void StageRuntime::send(const QString &op, const QJsonObject &args)
{
    const qint64 cid = qint64(args.value(QStringLiteral("cid")).toDouble());
    if (cid > m_ack) m_ack = cid;
    const auto it = m_ops.constFind(op);
    if (it == m_ops.cend()) {
        if (op != QLatin1String("ping")) log(QStringLiteral("info"), QStringLiteral("%1 is not available without the game running").arg(op));
        publish();
        return;
    }
    if (!m_readonly.contains(op)) { pushUndo(op); ++m_editSerial; }
    // an animated value changed by hand is keyed where it was changed (not by ops that move keys or time)
    static const QSet<QString> noChannelKey = {QStringLiteral("undo"), QStringLiteral("redo"), QStringLiteral("project_load"), QStringLiteral("project_new"),
                                               QStringLiteral("set_key_value"), QStringLiteral("move_key"), QStringLiteral("move_keys"), QStringLiteral("scale_keys"),
                                               QStringLiteral("remove_keys"), QStringLiteral("paste_keys"), QStringLiteral("set_ease"), QStringLiteral("key_channel"),
                                               QStringLiteral("remove_channel_key"), QStringLiteral("channel_clear"), QStringLiteral("remove_track")};
    const bool watchChannels = !m_readonly.contains(op) && !noChannelKey.contains(op) && !m_playing && !m_rendering;
    const QHash<QString, double> channelsBefore = watchChannels ? channelSnapshot() : QHash<QString, double>();
    try {
        (*it)(args);
        if (watchChannels && !channelsBefore.isEmpty()) keyChangedChannels(channelsBefore);
    } catch (const std::exception &e) {
        log(QStringLiteral("error"), QStringLiteral("%1: %2").arg(op, QString::fromLocal8Bit(e.what())));
    }
    if (!m_playing) evaluate(m_t);
    publish();
}

// ------------------------------------------------------------------ history (whole-document snapshots, like history.lua)
void StageRuntime::pushUndo(const QString &label)
{
    static const QSet<QString> coalesce = {QStringLiteral("set_transform"), QStringLiteral("set_joint_euler"), QStringLiteral("cam_update"),
                                           QStringLiteral("light_update"), QStringLiteral("update_clip"), QStringLiteral("set_key_value"),
                                           QStringLiteral("seq_range"), QStringLiteral("set_scale"), QStringLiteral("pose_weight")};
    static qint64 lastMs = 0;
    const qint64 now = m_clock.elapsed();
    if (!m_past.isEmpty() && m_past.last().label == label && coalesce.contains(label) && now - lastMs < 1200) { lastMs = now; return; }
    lastMs = now;
    m_past.append({label, toJson(m_doc)});
    while (m_past.size() > 80) m_past.removeFirst();
    m_future.clear();
}

void StageRuntime::restore(const QJsonObject &snap)
{
    const QString game = m_doc.gameKey;
    Document d = fromJson(snap);
    // actors that no longer exist leave the picture; new ones load
    QSet<qint64> keep;
    for (const Actor &a : d.actors) keep.insert(a.id);
    for (const Actor &a : std::as_const(m_doc.actors))
        if (!keep.contains(a.id)) { m_models.remove(a.id); if (m_scene) m_scene->removeActor(a.id); }
    const Camera work = m_doc.work;
    m_doc = d;
    m_doc.work = work;                       // the work camera is not part of the film's history
    m_doc.hasWork = true;
    if (m_doc.gameKey.isEmpty()) m_doc.gameKey = game;
    qualifyDocument(m_doc);
    for (const Actor &a : std::as_const(m_doc.actors)) ensureModel(a.id);
    m_bindings.clear();
    if (m_t > m_doc.seq.length) m_t = m_doc.seq.length;
    markData();
}

void StageRuntime::undo()
{
    if (m_past.isEmpty()) return;
    const Snap s = m_past.takeLast();
    m_future.append({s.label, toJson(m_doc)});
    restore(s.doc);
    log(QStringLiteral("info"), QStringLiteral("undo %1").arg(s.label));
}

void StageRuntime::redo()
{
    if (m_future.isEmpty()) return;
    const Snap s = m_future.takeLast();
    m_past.append({s.label, toJson(m_doc)});
    restore(s.doc);
}

// ------------------------------------------------------------------ transport
void StageRuntime::tick()
{
    if (m_rendering) return;
    const qint64 now = m_clock.elapsed();
    const double dt = std::min(0.1, (now - m_lastTick) / 1000.0);
    m_lastTick = now;
    if (m_drive.on) driveStep(dt);
    const bool wasPlaying = m_playing;
    bool live = m_previewShown || m_clipPreviewMot >= 0 || m_drive.on;   // the hover previews and a driven character move
    for (const Actor &a : std::as_const(m_doc.actors)) if (a.liveMot >= 0) live = true;
    if (m_playing && m_filmPlay) {
        // the film: shot after shot, each from its own start, through its own camera
        m_filmT += dt * m_doc.seq.fps * m_speed;
        int shot = -1;
        double t = m_t;
        if (m_filmT >= filmLength() || !filmAt(m_filmT, &shot, &t)) {
            if (m_doc.seq.loop && filmLength() > 0) { m_filmT = 0; filmAt(0, &shot, &t); }
            else { m_playing = false; m_filmPlay = false; m_shotCam = -1; }
        }
        if (shot >= 0) { m_t = t; m_shotCam = m_doc.seq.shots[shot].cam; }
    } else if (m_playing) {
        const Sequence &s = m_doc.seq;
        m_t += dt * s.fps * m_speed;
        const double a = s.range ? s.range->first : 0, b = s.range ? std::min(s.range->second, s.length) : s.length;
        if (m_t >= b) {
            if (s.loop) m_t = a + (m_t - b);
            else { m_t = b; m_playing = false; }
        }
    }
    static qint64 lastStageNews = 0;
    if (!m_stageLoading.isEmpty() && now - lastStageNews > 250) { lastStageNews = now; publish(); }
    if (m_playing || live || m_fly || wasPlaying) {                 // (and once more when playback has just ended)
        evaluate(m_t);
        static int n = 0;
        if (++n % 2 == 0 || !m_playing) publish();       // ~30 Hz state while playing
    } else if (m_scene) {
        m_scene->sync(*this);
    }
}

// a render films through the scene cameras (cuts, shots), never the Work Camera; the view comes back afterwards
void StageRuntime::renderBegin()
{
    setPreview({}, {});
    m_clipPreviewActor = 0;
    m_clipPreviewMot = -1;
    m_rendering = true;
    m_playing = false;
    m_renderViewBack = {m_workOn, m_activeCam};
    if (!m_doc.cameras.isEmpty()) m_workOn = false;
}
void StageRuntime::renderStep(double t, int shotCamera) { m_t = t; m_shotCam = shotCamera; m_camFollow = true; evaluate(t); if (m_scene) m_scene->sync(*this); }
void StageRuntime::renderEnd()
{
    m_rendering = false;
    m_shotCam = -1;
    m_workOn = m_renderViewBack.first;
    m_activeCam = m_renderViewBack.second;
    publish();
}

void StageRuntime::publish()
{
    ensureStage();                                          // the film's map follows the document (ops, undo, load)
    m_state = buildState();
    if (m_dataDirty) {
        m_dataDirty = false;
        ++m_dataVer;
        m_data = buildData();
        m_state.insert(QStringLiteral("data_ver"), m_dataVer);
        emit dataChanged();
    }
    if (m_scene) m_scene->sync(*this);
    emit stateChanged();
}

// ------------------------------------------------------------------ state / data (bridge.lua build_state / build_data)
QJsonObject StageRuntime::buildState()
{
    QJsonArray actors;
    QHash<qint64, const ActorFrame *> frames;
    for (const ActorFrame &f : m_frames) frames.insert(f.id, &f);
    for (const Actor &a : std::as_const(m_doc.actors)) {
        const ActorFrame *f = frames.value(a.id);
        const QVector3D pos = f ? f->pos : a.pos;
        const QQuaternion rot = f ? f->rot : a.rot;
        const PreparedModel *pm = prepared(a.id);
        QJsonArray banks;
        for (auto it = a.banks.cbegin(); it != a.banks.cend(); ++it) {
            const dir::AnimationSetPtr set = animations(it.value());
            banks.append(QJsonObject{{QStringLiteral("id"), it.key()}, {QStringLiteral("path"), it.value()},
                                     {QStringLiteral("name"), it.value().section(QLatin1Char('/'), -1).section(QLatin1Char('.'), 0, 0)},
                                     {QStringLiteral("ready"), bool(set)}, {QStringLiteral("count"), set ? int(set->clips.size()) : 0}});
        }
        // the clip Play started (the Asset Browser marks it, Properties offers Stop)
        QJsonArray layers;
        if (a.liveMot >= 0 && !a.livePath.isEmpty()) {
            QJsonObject l{{QStringLiteral("idx"), 0}, {QStringLiteral("bank"), bankFor(a.livePath)}, {QStringLiteral("mot"), a.liveMot}, {QStringLiteral("playing"), !a.livePaused}};
            if (const dir::AnimationSetPtr set = animations(a.livePath))
                for (const dir::AnimationClip &c : set->clips)
                    if (c.id == a.liveMot && c.frames > 1) {
                        const double f = a.livePaused ? a.liveFrame : (m_clock.elapsed() - a.liveStartMs) / 1000.0 * 60.0 * a.liveSpeed;
                        l.insert(QStringLiteral("endframe"), double(c.frames));
                        l.insert(QStringLiteral("frame"), std::fmod(std::max(0.0, f), double(c.frames)));
                    }
            layers.append(l);
        }
        QJsonObject lookat{{QStringLiteral("enabled"), a.lookat.enabled}, {QStringLiteral("kind"), a.lookat.kind}, {QStringLiteral("weight"), a.lookat.weight},
                           {QStringLiteral("max_deg"), a.lookat.maxDeg}, {QStringLiteral("smooth"), a.lookat.smooth}, {QStringLiteral("neck_share"), a.lookat.neckShare},
                           {QStringLiteral("flip"), a.lookat.flip}};
        if (a.lookat.target) lookat.insert(QStringLiteral("target"), double(a.lookat.target));
        QJsonObject pose{{QStringLiteral("posed"), int(a.work.size())}, {QStringLiteral("weight"), a.workWeight}};
        if (!a.workJoint.isEmpty()) {
            pose.insert(QStringLiteral("joint"), a.workJoint);
            // the posed value, else the bone as the animation has it now
            if (const auto q = currentLocal(a, a.workJoint)) pose.insert(QStringLiteral("euler"), fm::arr(fm::toEulerDeg(*q)));
            pose.insert(QStringLiteral("posed_joint"), a.work.contains(a.workJoint));
            const QString chain = effectorChain(a.id, a.workJoint);
            if (!chain.isEmpty()) pose.insert(QStringLiteral("ik"), chain);
        }
        QJsonObject o{{QStringLiteral("id"), double(a.id)}, {QStringLiteral("name"), a.name}, {QStringLiteral("display_name"), a.displayName.isEmpty() ? a.name : a.displayName},
                      {QStringLiteral("kind"), a.kind}, {QStringLiteral("spawned"), true}, {QStringLiteral("hidden"), a.hidden},
                      {QStringLiteral("ragdoll"), false}, {QStringLiteral("has_ragdoll"), false}, {QStringLiteral("is_player"), false},
                      {QStringLiteral("puppet"), false}, {QStringLiteral("paused"), a.livePaused}, {QStringLiteral("root_lock"), a.rootLock},
                      {QStringLiteral("pos"), fm::arr(pos)}, {QStringLiteral("euler"), fm::arr(fm::toEulerUpright(rot))},
                      {QStringLiteral("joints"), pm ? int(pm->model->skeleton.bones.size()) : 0}, {QStringLiteral("layer_count"), 1},
                      {QStringLiteral("has_fsm"), false}, {QStringLiteral("layers"), layers}, {QStringLiteral("banks"), banks},
                      {QStringLiteral("lookat"), lookat},
                      {QStringLiteral("pose"), pose}, {QStringLiteral("scale"), a.scale.x()}, {QStringLiteral("scale3"), fm::arr(a.scale)},
                      {QStringLiteral("model"), a.model}, {QStringLiteral("loading"), !pm && m_loadingModels.contains(a.id)},
                      {QStringLiteral("game"), refGame(a.model)}, {QStringLiteral("game_title"), m_games ? m_games->titleForId(refGame(a.model)) : QString()}};
        if (a.attached) {
            const Actor *host = m_doc.actor(a.attached->actor);
            o.insert(QStringLiteral("attached"), QJsonObject{{QStringLiteral("actor"), double(a.attached->actor)},
                                                             {QStringLiteral("actor_name"), host ? (host->displayName.isEmpty() ? host->name : host->displayName) : QString()},
                                                             {QStringLiteral("joint"), a.attached->joint}, {QStringLiteral("pos"), fm::arr(a.attached->pos)},
                                                             {QStringLiteral("euler"), fm::arr(fm::toEulerDeg(a.attached->rot))}});
        }
        if (!a.castId.isEmpty())
            o.insert(QStringLiteral("cast"), QJsonObject{{QStringLiteral("id"), a.castId}, {QStringLiteral("code"), a.castCode}, {QStringLiteral("tree"), a.castTree},
                                                         {QStringLiteral("preset_name"), a.preset}, {QStringLiteral("name"), a.displayName},
                                                         {QStringLiteral("physics"), a.physics}, {QStringLiteral("gore"), false},
                                                         {QStringLiteral("entry"), castEntry(a)}});   // its game's entry (looks), whichever game is listed
        actors.append(o);
    }
    QJsonArray multi;
    for (qint64 id : m_multi) multi.append(double(id));
    QJsonObject sel{{QStringLiteral("camera"), m_selCamera}, {QStringLiteral("multi"), multi}};
    if (m_selActor) sel.insert(QStringLiteral("actor"), double(m_selActor));
    if (!m_selClip.isEmpty()) sel.insert(QStringLiteral("clip"), m_selClip);
    if (m_selLight) sel.insert(QStringLiteral("light"), m_selLight);
    const QString view = m_workOn || m_activeCam <= 0 ? QStringLiteral("work") : m_sceneView ? QStringLiteral("scene") : QStringLiteral("camera");
    QJsonObject seq = sequenceState(m_doc, m_t, m_playing, m_speed);
    if (m_pen == QLatin1String("path")) {
        // the path being drawn (its section offers Done)
        QJsonArray tracks = seq.value(QStringLiteral("tracks")).toArray();
        for (int i = 0; i < tracks.size(); ++i) {
            QJsonObject tr = tracks[i].toObject();
            if (tr.value(QStringLiteral("kind")).toString() == QLatin1String("path") && qint64(tr.value(QStringLiteral("actor")).toDouble()) == m_penActor) {
                tr.insert(QStringLiteral("drawing"), true);
                tracks[i] = tr;
            }
        }
        seq.insert(QStringLiteral("tracks"), tracks);
    }
    const int fc = filmCamera(m_t);
    if (fc) seq.insert(QStringLiteral("film_camera"), fc);
    const dir::GameInfo gi = m_games ? m_games->activeInfo() : dir::GameInfo();
    return {
        {QStringLiteral("runtime"), QStringLiteral("standalone")},
        {QStringLiteral("ack"), double(m_ack)}, {QStringLiteral("data_ver"), m_dataVer}, {QStringLiteral("clock"), m_clock.elapsed() / 1000.0},
        {QStringLiteral("actors"), actors}, {QStringLiteral("cameras"), camerasState(m_doc, m_activeCam)},
        {QStringLiteral("camera_override"), true}, {QStringLiteral("active_camera"), m_activeCam}, {QStringLiteral("camera_view"), view},
        {QStringLiteral("camera_fly"), QJsonObject{{QStringLiteral("on"), m_fly}, {QStringLiteral("speed"), 1.0}, {QStringLiteral("sens"), 1.0}}},
        {QStringLiteral("game_focused"), false},
        {QStringLiteral("edit"), QJsonObject{{QStringLiteral("on"), false}, {QStringLiteral("busy"), false}}},
        {QStringLiteral("autokey"), m_autokey}, {QStringLiteral("drive"), driveState()},
        {QStringLiteral("hide_weapons"), false}, {QStringLiteral("hud_hidden"), true},
        {QStringLiteral("history"), QJsonObject{{QStringLiteral("undo"), int(m_past.size())}, {QStringLiteral("redo"), int(m_future.size())},
                                                {QStringLiteral("last"), m_past.isEmpty() ? QJsonValue() : QJsonValue(m_past.last().label)},
                                                {QStringLiteral("next"), m_future.isEmpty() ? QJsonValue() : QJsonValue(m_future.last().label)}}},
        {QStringLiteral("key_clipboard"), m_keyClipboard.value(QStringLiteral("count")).toInt()},
        {QStringLiteral("overlay"), m_doc.overlay}, {QStringLiteral("lights"), lightsState(m_doc)},
        {QStringLiteral("skeleton"), m_skeleton}, {QStringLiteral("onion"), m_onion}, {QStringLiteral("motion_edit"), m_motionEdit}, {QStringLiteral("relative_edits"), m_relativeEdits},
        {QStringLiteral("stage_clean"), false}, {QStringLiteral("stage"), stageState()},
        {QStringLiteral("constraints"), constraintsState()}, {QStringLiteral("channels"), channelsState()},
        {QStringLiteral("recovery"), m_recovery.isEmpty() ? QJsonValue() : QJsonValue(m_recovery)},
        {QStringLiteral("film"), [this] {
             int shot = -1;
             double t = 0;
             const bool at = m_filmPlay && filmAt(m_filmT, &shot, &t);
             return QJsonObject{{QStringLiteral("on"), m_filmPlay}, {QStringLiteral("t"), m_filmT}, {QStringLiteral("length"), filmLength()},
                                {QStringLiteral("shot"), at ? m_doc.seq.shots[shot].id : 0}};
         }()},
        {QStringLiteral("world"), worldJson(m_doc.world)},
        {QStringLiteral("skeleton_joints"), m_skeleton || m_gizmo.value(QStringLiteral("target")).toString() == QLatin1String("bone") ? skeletonState() : QJsonArray()}, {QStringLiteral("last_export"), m_lastExport.isEmpty() ? QJsonValue() : QJsonValue(m_lastExport)}, {QStringLiteral("pick_props"), false}, {QStringLiteral("auto_rm"), false},
        {QStringLiteral("pen"), m_pen.isEmpty() ? QJsonValue() : QJsonValue(QJsonObject{{QStringLiteral("kind"), m_pen}, {QStringLiteral("actor"), double(m_penActor)},
                                                                                          {QStringLiteral("name"), m_penSpec.value(QStringLiteral("name"))}})},
        {QStringLiteral("thumb"), QJsonObject{{QStringLiteral("on"), false}}},
        {QStringLiteral("selection"), sel}, {QStringLiteral("sequence"), seq}, {QStringLiteral("gizmo"), [this] { QJsonObject g = m_gizmo; g.insert(QStringLiteral("frame"), gizmoFrame()); return g; }()},
        {QStringLiteral("game"), QJsonObject{{QStringLiteral("fov"), m_view.fov}, {QStringLiteral("project"), m_project}, {QStringLiteral("frozen"), false},
                                             {QStringLiteral("player_frozen"), false}, {QStringLiteral("standalone"), true},
                                             {QStringLiteral("title"), gi.title}, {QStringLiteral("ready"), m_games && m_games->ready()},
                                             {QStringLiteral("id"), gi.gameId}, {QStringLiteral("film"), filmGame()}, {QStringLiteral("listed"), m_catalogGame}}},
        {QStringLiteral("render_clock"), QJsonObject{{QStringLiteral("on"), m_rendering}}},
        {QStringLiteral("tests"), QJsonArray()},
    };
}

QJsonObject StageRuntime::buildData()
{
    QJsonObject motions;
    QJsonArray joints;
    if (const Actor *a = m_doc.actor(m_selActor)) {
        for (auto it = a->banks.cbegin(); it != a->banks.cend(); ++it) {
            const dir::AnimationSetPtr set = animations(it.value());
            if (!set) continue;
            QJsonArray list;
            for (const dir::AnimationClip &c : set->clips)
                list.append(QJsonObject{{QStringLiteral("id"), c.id}, {QStringLiteral("name"), c.name}, {QStringLiteral("endframe"), double(c.frames)}});
            motions.insert(QString::number(it.key()), list);
        }
        if (const PreparedModel *pm = prepared(a->id))
            for (const dir::Bone &b : pm->model->skeleton.bones) joints.append(b.name);
    }
    const Actor *sel = m_doc.actor(m_selActor);
    QJsonArray projects, poses;
    for (const QString &f : QDir(projectsDir()).entryList({QStringLiteral("*.film.json")}, QDir::Files, QDir::Time))
        projects.append(f.left(f.size() - 10));
    for (const QString &f : QDir(projectsDir() + QStringLiteral("/../poses")).entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name))
        poses.append(f.left(f.size() - 5));
    return {
        {QStringLiteral("ver"), m_dataVer}, {QStringLiteral("eval"), m_eval}, {QStringLiteral("cast"), m_cast},
        {QStringLiteral("scene"), QJsonArray()}, {QStringLiteral("objects"), QJsonArray()}, {QStringLiteral("meshes"), m_meshResults},
        {QStringLiteral("catalog"), QJsonObject{{QStringLiteral("results"), m_catalogResults}}},
        {QStringLiteral("groups"), m_groups}, {QStringLiteral("chars"), m_chars}, {QStringLiteral("owners"), m_owners},
        {QStringLiteral("motions"), motions}, {QStringLiteral("joints"), joints}, {QStringLiteral("poses"), poses},
        {QStringLiteral("imports"), QJsonArray::fromStringList(sel ? importsFor(rigCode(*sel)) : QStringList())},
        {QStringLiteral("rig_code"), sel ? rigCode(*sel) : QString()}, {QStringLiteral("last_rig"), m_lastRig},
        {QStringLiteral("projects"), projects}, {QStringLiteral("log"), m_logTail},
        {QStringLiteral("stages"), m_stages},
        {QStringLiteral("mesh_preview"), m_previewShown ? QJsonObject{{QStringLiteral("name"), m_previewName}, {QStringLiteral("size"), fm::arr(m_previewSize)}} : QJsonObject()},
    };
}

// the map, in bridge.lua's Stage.state() shape: {busy, status {phase, name, stage, error}, current {stage, name}}
QJsonObject StageRuntime::stageState() const
{
    QJsonObject status = m_stageStatus;
    if (!m_stageLoading.isEmpty() && m_stageTotal) {
        status.insert(QStringLiteral("done"), m_stageDone->load());
        status.insert(QStringLiteral("total"), m_stageTotal->load());
    }
    QJsonObject o{{QStringLiteral("busy"), !m_stageLoading.isEmpty()}, {QStringLiteral("standalone"), true}};
    if (!status.isEmpty()) o.insert(QStringLiteral("status"), status);
    if (!m_stageShown.isEmpty())
        o.insert(QStringLiteral("current"), QJsonObject{{QStringLiteral("stage"), m_doc.stageNumber}, {QStringLiteral("name"), m_doc.stageName},
                                                         {QStringLiteral("id"), m_doc.stageId},
                                                         {QStringLiteral("lightings"), QJsonArray::fromStringList(m_stageLightVariants)}});
    return o;
}

void StageRuntime::setPreview(const QString &modelId, const QString &name)
{
    if (modelId == m_previewModel) return;
    m_previewModel = modelId;
    m_previewName = name;
    const int serial = ++m_previewSerial;
    if (m_previewShown && m_scene) m_scene->removeActor(kPreviewId);
    m_previewShown = false;
    m_previewSize = {};
    markData();
    dir::IGameSource *src = modelId.isEmpty() ? nullptr : sourceOf(modelId);
    if (!src) { publish(); return; }
    const QString gameId = refGame(modelId), local = refLocal(modelId);
    auto *w = new QFutureWatcher<PreparedModel>(this);
    connect(w, &QFutureWatcherBase::finished, this, [this, w, serial]() {
        const PreparedModel pm = w->result();
        w->deleteLater();
        if (serial != m_previewSerial || !pm.model) return;
        if (m_scene) m_scene->addActor(kPreviewId, pm);
        m_previewShown = true;
        m_previewStartMs = m_clock.elapsed();
        m_previewSize = pm.model->boundsMax - pm.model->boundsMin;
        m_previewCenter = (pm.model->boundsMax + pm.model->boundsMin) * 0.5f;
        markData();
        evaluate(m_t);
        publish();
    });
    w->setFuture(QtConcurrent::run([src, gameId, local]() {
        QString err;
        const dir::ModelPtr m = src->loadModel(local, &err);
        return m ? prepareModel(m, src, gameId, 1024) : PreparedModel();
    }));
}

bool StageRuntime::groundBelow(const QVector3D &from, float maxDrop, float *y) const
{
    return m_scene && m_scene->groundBelow(from, maxDrop, y);
}

void StageRuntime::ensureStage()
{
    const QString want = m_doc.stageId;
    if (want.isEmpty()) {
        if (m_stageShown.isEmpty() && m_stageLoading.isEmpty()) return;
        ++m_stageSerial;
        if (m_stageCurrent) m_stageCurrent->store(m_stageSerial);
        m_stageShown.clear();
        m_stageLoading.clear();
        m_stageStatus = {};
        if (m_scene) m_scene->clearStage();
        return;
    }
    if (want == m_stageLoading || (want == m_stageShown && m_stageLoading.isEmpty())) return;
    dir::IGameSource *src = sourceOf(want);
    if (!src) return;                                          // its game is opening (sourceOpened asks again)
    const QString gameId = refGame(want), local = refLocal(want);
    const int serial = ++m_stageSerial;
    if (!m_stageCurrent) m_stageCurrent = std::make_shared<std::atomic_int>(0);
    m_stageCurrent->store(serial);
    m_stageLoading = want;
    m_stageDone = std::make_shared<std::atomic_int>(0);
    m_stageTotal = std::make_shared<std::atomic_int>(0);
    m_stageStatus = {{QStringLiteral("phase"), QStringLiteral("loading")}, {QStringLiteral("name"), m_doc.stageName}, {QStringLiteral("stage"), m_doc.stageNumber}};
    log(QStringLiteral("info"), QStringLiteral("loading the map %1").arg(m_doc.stageName));
    auto done = m_stageDone, total = m_stageTotal, current = m_stageCurrent;
    QElapsedTimer clock;
    clock.start();
    auto *w = new QFutureWatcher<QPair<PreparedStage, QString>>(this);
    connect(w, &QFutureWatcherBase::finished, this, [this, w, serial, want, clock]() {
        const auto r = w->result();
        w->deleteLater();
        if (serial != m_stageSerial) return;                  // replaced or cancelled meanwhile
        m_stageLoading.clear();
        const QString name = m_doc.stageName;
        if (!r.first.stage) {
            m_stageStatus = {{QStringLiteral("phase"), QStringLiteral("failed")}, {QStringLiteral("name"), name}, {QStringLiteral("stage"), m_doc.stageNumber},
                             {QStringLiteral("error"), r.second}};
            log(QStringLiteral("error"), QStringLiteral("the map %1 did not load: %2").arg(name, r.second));
            publish();
            return;
        }
        QString err;
        if (m_scene && !m_scene->setStage(r.first, &err)) {
            m_stageStatus = {{QStringLiteral("phase"), QStringLiteral("failed")}, {QStringLiteral("name"), name}, {QStringLiteral("error"), err}};
            log(QStringLiteral("error"), QStringLiteral("the map %1 could not be shown: %2").arg(name, err));
            publish();
            return;
        }
        m_stageShown = want;
        m_stageShownName = name;
        m_stageLightVariants = r.first.stage->lightVariants;
        // a map without named places (RE2's rooms): the Work Camera goes to the middle of what it places
        if (m_stageArriveCenter && !r.first.stage->instances.isEmpty()) {
            m_stageArriveCenter = false;
            QVector<float> xs, ys, zs;
            for (const dir::StageInstance &si : r.first.stage->instances) {
                const QVector3D p = si.world.column(3).toVector3D();
                xs << p.x();
                ys << p.y();
                zs << p.z();
            }
            auto median = [](QVector<float> &v) { std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end()); return v[v.size() / 2]; };
            m_stageArrive = QVector3D(median(xs), median(ys), median(zs));
            m_camFollow = false;
            m_sceneView = false;
            setActiveCamera(0);
        }
        // the Work Camera stands at eye height on the ground under the named place (map marker heights are rough)
        if (m_stageArrive) {
            const QVector3D at = *m_stageArrive;
            m_stageArrive.reset();
            // of the surfaces under the named place (ground, floors, roofs), the one nearest the marker's height
            auto nearest = [this](float x, float z, float hint, float *y) {
                const QVector<float> s = m_scene ? m_scene->surfacesAt(x, z) : QVector<float>();
                if (s.isEmpty()) return false;
                *y = *std::min_element(s.cbegin(), s.cend(), [hint](float a, float b) { return std::abs(a - hint) < std::abs(b - hint); });
                return true;
            };
            float ground = 0;
            if (nearest(at.x(), at.z(), at.y(), &ground)) {
                const QVector3D target(at.x(), ground + 1.1f, at.z());
                QVector3D f = fm::forward(m_doc.work.rot);
                f.setY(0);
                if (f.length() < 0.01f) f = QVector3D(0, 0, -1);
                f.normalize();
                QVector3D eye = target - f * 4.5f + QVector3D(0, 0.6f, 0);
                float eyeGround = 0;
                if (nearest(eye.x(), eye.z(), ground, &eyeGround)) eye.setY(std::max(eye.y(), eyeGround + 1.7f));
                m_doc.work.pos = eye;
                m_doc.work.rot = fm::lookRotation(eye, target);
                evaluate(m_t);
            }
        }
        m_stageShownNumber = m_doc.stageNumber;
        m_stageStatus = {{QStringLiteral("phase"), QStringLiteral("done")}, {QStringLiteral("name"), name}, {QStringLiteral("stage"), m_doc.stageNumber}};
        for (const QString &warn : r.first.warnings.mid(0, 3)) log(QStringLiteral("warn"), warn);
        log(QStringLiteral("info"), QStringLiteral("%1: %2 objects, %3 meshes, %4 textures in %5 s").arg(name).arg(r.first.stage->instances.size())
                                        .arg(r.first.models.size()).arg(r.first.textures.size()).arg(clock.elapsed() / 1000.0, 0, 'f', 1));
        publish();
    });
    w->setFuture(QtConcurrent::run([src, gameId, local, done, total, current, serial]() {
        QString err;
        PreparedStage ps;
        ps.stage = src->loadStage(local, &err);
        if (!ps.stage) return qMakePair(PreparedStage(), err);
        QStringList ids;
        QSet<QString> seen;
        for (const dir::StageInstance &si : ps.stage->instances)
            if (!seen.contains(si.model)) { seen.insert(si.model); ids << si.model; }
        total->store(int(ids.size()));
        TextureShare share;
        QMutex mutex;
        QtConcurrent::blockingMap(ids, [&](QString &id) {
            if (current->load() != serial) return;
            QString e;
            PreparedModel pm;
            if (const dir::ModelPtr m = src->loadModel(id, &e)) pm = prepareModel(m, src, gameId, 1024, &share);
            pm.textures.clear();                              // the stage keeps one table for all
            QMutexLocker lock(&mutex);
            if (pm.model) ps.models.insert(id, pm);
            else ps.warnings << QStringLiteral("%1: %2").arg(id.section(QLatin1Char('|'), 0, 0).section(QLatin1Char('/'), -1), e);
            done->fetch_add(1);
        });
        if (current->load() != serial) return qMakePair(PreparedStage(), QStringLiteral("cancelled"));
        ps.textures = share.textures;
        return qMakePair(ps, QString());
    }));
}

// ------------------------------------------------------------------ assets
const PreparedModel *StageRuntime::prepared(qint64 actorId) const
{
    const auto it = m_models.constFind(actorId);
    return it == m_models.cend() ? nullptr : &*it;
}

void StageRuntime::ensureModel(qint64 actorId)
{
    const Actor *a = m_doc.actor(actorId);
    if (!a || m_models.contains(actorId) || m_loadingModels.contains(actorId)) return;
    dir::IGameSource *src = sourceOf(a->model);
    if (!src) return;                                          // its game is opening (sourceOpened asks again)
    const QString gameId = refGame(a->model), modelId = refLocal(a->model);
    m_loadingModels.insert(actorId);
    auto *w = new QFutureWatcher<QPair<PreparedModel, QString>>(this);
    connect(w, &QFutureWatcherBase::finished, this, [this, w, actorId]() {
        const auto r = w->result();
        w->deleteLater();
        m_loadingModels.remove(actorId);
        if (!m_doc.actor(actorId)) return;                       // deleted while loading
        if (!r.first.model) { log(QStringLiteral("error"), QStringLiteral("could not load %1: %2").arg(m_doc.actor(actorId)->name, r.second)); markData(); publish(); return; }
        m_models.insert(actorId, r.first);
        for (const QString &warn : r.first.warnings.mid(0, 3)) log(QStringLiteral("warn"), warn);
        if (m_scene) m_scene->addActor(actorId, r.first);
        m_models[actorId].textures.clear();                     // the GPU textures own their data now
        // idle loop for characters that should breathe
        if (Actor *x = m_doc.actor(actorId); x && !x->idlePath.isEmpty()) ensureAnimations(x->idlePath);
        if (const Actor *x = m_doc.actor(actorId)) ensureGameCast(refGame(x->model));   // its looks and general list
        markData();
        evaluate(m_t);
        publish();
    });
    w->setFuture(QtConcurrent::run([src, gameId, modelId]() {
        QString err;
        const dir::ModelPtr m = src->loadModel(modelId, &err);
        if (!m) return qMakePair(PreparedModel(), err);
        return qMakePair(prepareModel(m, src, gameId, 2048), QString());
    }));
}

void StageRuntime::ensureAnimations(const QString &path, std::function<void()> then)
{
    if (path.isEmpty()) return;
    const QString key = path.toLower();
    if (m_anims.contains(key)) { if (then) then(); return; }
    static QHash<QString, QVector<std::function<void()>>> waiting;
    if (then) waiting[key] << then;
    if (m_loadingAnims.contains(key)) return;
    dir::IGameSource *src = sourceOf(path);
    if (!src) { m_animsWaiting.insert(path); return; }         // its game is opening
    m_loadingAnims.insert(key);
    const QString local = refLocal(path);
    auto *w = new QFutureWatcher<QPair<dir::AnimationSetPtr, QString>>(this);
    connect(w, &QFutureWatcherBase::finished, this, [this, w, key, path]() {
        const auto r = w->result();
        w->deleteLater();
        m_loadingAnims.remove(key);
        if (!r.first) { log(QStringLiteral("error"), QStringLiteral("animations %1: %2").arg(path.section(QLatin1Char('/'), -1), r.second)); waiting.remove(key); return; }
        m_anims.insert(key, r.first);
        for (const auto &fn : waiting.take(key)) fn();
        markData();
        m_bindings.clear();
        if (!m_playing) evaluate(m_t);
        publish();
    });
    w->setFuture(QtConcurrent::run([src, local]() {
        QString err;
        const dir::AnimationSetPtr s = src->loadAnimations(local, &err);
        return qMakePair(s, err);
    }));
}

const ClipBinding *StageRuntime::binding(qint64 actorId, const QString &path, int mot)
{
    const PreparedModel *pm = prepared(actorId);
    const dir::AnimationSetPtr set = animations(path);
    if (!pm || !set) return nullptr;
    const QString key = QStringLiteral("%1|%2|%3").arg(actorId).arg(path.toLower()).arg(mot);
    auto it = m_bindings.find(key);
    if (it == m_bindings.end()) {
        const dir::AnimationClip *clip = nullptr;
        for (const dir::AnimationClip &c : set->clips) if (c.id == mot) { clip = &c; break; }
        if (!clip && mot >= 0 && mot < set->clips.size() && set->clips[mot].id < 0) clip = &set->clips[mot];
        // a list made for another character plays as offsets from that character's bind pose (its face, its
        // proportions); one from another game plays on that game's skeleton and maps onto this one bone by bone.
        // Wait for that skeleton unless it cannot be had
        Retarget rt;
        const Actor *a = m_doc.actor(actorId);
        const QString owner = ownerOf(path), game = refGame(path);
        rt.sameRig = !owner.isEmpty() && a && owner == a->castCode && a && refGame(a->model) == game;
        if (!rt.sameRig && !owner.isEmpty()) {
            const QString rig = qualify(game, owner);
            ensureRig(rig);
            if (m_rigs.contains(rig)) rt.source = m_rigs.value(rig).data();
            else if (!m_rigFailed.contains(rig)) return nullptr;
        }
        it = m_bindings.insert(key, ClipBinding(clip, pm->model->skeleton, rt));
    }
    return it->isValid() ? &*it : nullptr;
}

// `code` is game::owner ("re4::cha0"): the first look of that owner in its game
void StageRuntime::ensureRig(const QString &code)
{
    if (code.isEmpty() || m_rigs.contains(code) || m_rigLoading.contains(code) || m_rigFailed.contains(code)) return;
    const QString game = refGame(code);
    if (!m_castByGame.contains(game)) { ensureGameCast(game); return; }      // its characters are being listed
    const QString modelId = m_rigModel.value(code);
    if (modelId.isEmpty()) { m_rigFailed.insert(code); return; }
    dir::IGameSource *src = sourceOf(modelId);
    if (!src) return;
    const QString local = refLocal(modelId);
    m_rigLoading.insert(code);
    using Rig = QPair<QSharedPointer<const dir::Skeleton>, QString>;
    auto *w = new QFutureWatcher<Rig>(this);
    connect(w, &QFutureWatcherBase::finished, this, [this, w, code]() {
        const Rig r = w->result();
        w->deleteLater();
        m_rigLoading.remove(code);
        if (r.first) m_rigs.insert(code, r.first);
        else { m_rigFailed.insert(code); log(QStringLiteral("warn"), QStringLiteral("skeleton of %1: %2 (its clips play unadapted)").arg(code, r.second)); }
        if (!m_playing) evaluate(m_t);
        publish();
    });
    w->setFuture(QtConcurrent::run([src, local]() {
        QString err;
        const dir::ModelPtr m = src->loadModel(local, &err);
        return m ? Rig(QSharedPointer<const dir::Skeleton>::create(m->skeleton), QString()) : Rig({}, err);
    }));
}

int StageRuntime::bankFor(const QString &path)
{
    const QString key = path.toLower();
    auto it = m_banks.constFind(key);
    if (it != m_banks.cend()) return *it;
    const int id = int(m_banks.size()) + 1;
    m_banks.insert(key, id);
    m_bankPaths.insert(id, path);
    return id;
}

void StageRuntime::buildCatalogs()
{
    m_cast = {};
    m_animCatalog = {};
    m_props.clear();
    m_owners = {};
    m_stages = {};
    m_catalogGame.clear();
    markData();
    if (!m_games || !m_games->ready()) return;
    dir::IGameSource *src = m_games->source();
    const QString game = m_games->gameId();
    struct Cats { QList<dir::CatalogEntry> chars, anims, props, stages; };
    auto *w = new QFutureWatcher<Cats>(this);
    connect(w, &QFutureWatcherBase::finished, this, [this, w, game]() {
        const Cats c = w->result();
        w->deleteLater();
        // characters (their looks are also the skeletons clips of other characters adapt to)
        const QJsonArray cast = storeCast(game, c.chars);
        m_castLoading.remove(game);
        if (game != browseGame()) return;                       // another game was chosen meanwhile
        m_catalogGame = game;
        m_cast = cast;
        QJsonObject chars;
        for (const QJsonValue &v : cast) {
            const QString code = v.toObject().value(QStringLiteral("code")).toString();
            if (!code.isEmpty() && !chars.contains(code)) chars.insert(code, v.toObject().value(QStringLiteral("name")));
        }
        m_chars = chars;
        // animation catalog: (index, path, name, owner code, group)
        QMap<QString, int> owners;
        QSet<QString> groups;
        static const QRegularExpression ownerRx(QStringLiteral("/animation/(?:ch|player|enemy|npc)/([a-z0-9]+)/"), QRegularExpression::CaseInsensitiveOption);
        int i = 0;
        for (const dir::CatalogEntry &e : c.anims) {
            const auto m = ownerRx.match(e.path);
            const QString owner = m.hasMatch() ? m.captured(1).toLower() : QString();
            // the Asset Browser's groups (Names.js groupLabel): gameplay, cutscene, facial, weapon, prop, gimmick, other
            const QString lp = e.path.toLower();
            QString g = QStringLiteral("other");
            if (e.tags.contains(QStringLiteral("facial"))) g = QStringLiteral("facial");
            else if (lp.contains(QLatin1String("/event/")) || lp.contains(QLatin1String("/cutscene/"))) g = QStringLiteral("cutscene");
            else if (lp.contains(QLatin1String("/animation/ch/")) || lp.contains(QLatin1String("/animation/player/")) || lp.contains(QLatin1String("/animation/enemy/"))
                     || lp.contains(QLatin1String("/animation/npc/"))) g = QStringLiteral("gameplay");
            else if (lp.contains(QLatin1String("/animation/wp/"))) g = QStringLiteral("weapon");
            else if (lp.contains(QLatin1String("/animation/sm/"))) g = QStringLiteral("prop");
            else if (lp.contains(QLatin1String("/appsystem/")) || lp.contains(QLatin1String("/environment/")) || lp.contains(QLatin1String("/gimmick/"))) g = QStringLiteral("gimmick");
            if (lp.contains(QLatin1String("_anotherorder/"))) g += QStringLiteral("(sw)");
            else if (lp.contains(QLatin1String("_mercenaries/"))) g += QStringLiteral("(merc)");
            if (!owner.isEmpty()) owners[owner]++;
            groups.insert(g);
            m_animCatalog.append(QJsonObject{{QStringLiteral("i"), ++i}, {QStringLiteral("p"), qualify(game, e.path)}, {QStringLiteral("n"), e.name},
                                             {QStringLiteral("c"), owner}, {QStringLiteral("g"), g}});
        }
        m_owners = {};
        for (auto it = owners.cbegin(); it != owners.cend(); ++it) m_owners.append(QJsonObject{{QStringLiteral("code"), it.key()}, {QStringLiteral("count"), it.value()}});
        QStringList gl(groups.cbegin(), groups.cend());
        gl.sort();
        m_groups = QJsonArray::fromStringList(gl);
        m_props = c.props;
        // maps: one row per named place (several places can share an area), then the unnamed areas
        m_stageCatalog = c.stages;
        m_stages = {};
        QJsonArray unnamed;
        for (const dir::CatalogEntry &e : c.stages) {
            const int number = e.extra.value(QStringLiteral("stage")).toInt();
            const QVariantList points = e.extra.value(QStringLiteral("points")).toList();
            for (const QVariant &pv : points) {
                const QVariantMap p = pv.toMap();
                m_stages.append(QJsonObject{{QStringLiteral("id"), qualify(game, e.id)}, {QStringLiteral("stage"), number}, {QStringLiteral("loc"), number / 1000 * 1000},
                                            {QStringLiteral("code"), e.extra.value(QStringLiteral("code")).toString()},
                                            {QStringLiteral("name"), p.value(QStringLiteral("name")).toString()}, {QStringLiteral("map"), e.group},
                                            {QStringLiteral("pos"), QJsonArray::fromVariantList(p.value(QStringLiteral("pos")).toList())}});
            }
            if (points.isEmpty())
                unnamed.append(QJsonObject{{QStringLiteral("id"), qualify(game, e.id)}, {QStringLiteral("stage"), number}, {QStringLiteral("loc"), number / 1000 * 1000},
                                           {QStringLiteral("name"), e.name}, {QStringLiteral("map"), e.group}, {QStringLiteral("pos"), QJsonArray()},
                                           {QStringLiteral("unnamed"), true}});
        }
        for (const QJsonValue &v : std::as_const(unnamed)) m_stages.append(v);
        ensureStage();
        log(QStringLiteral("info"), QStringLiteral("%1: %2 looks, %3 animation lists, %4 props").arg(m_games->titleForId(game)).arg(c.chars.size()).arg(c.anims.size()).arg(c.props.size()));
        send(QStringLiteral("catalog_search"), {});
        send(QStringLiteral("mesh_search"), {});
    });
    m_castLoading.insert(game);
    w->setFuture(QtConcurrent::run([src]() {
        Cats c;
        c.chars = src->catalog(dir::AssetKind::Character);
        c.anims = src->catalog(dir::AssetKind::Animation);
        c.props = src->catalog(dir::AssetKind::Prop);
        c.stages = src->catalog(dir::AssetKind::Stage);
        return c;
    }));
}

QString StageRuntime::projectsDir() const
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/Director Studio/films");
    QDir().mkpath(d);
    QDir().mkpath(d + QStringLiteral("/../poses"));
    return d;
}
