// The standalone runtime: the film is animated inside RigReel Studio, with assets read from the game the user
// owns through its plugin. It accepts the same commands and publishes the same state / data as the in-game Lua
// runtime (bridge.lua), so the Studio's panels work unchanged on either one.
#pragma once

#include "Film.h"
#include "ModelPrep.h"
#include "Pose.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>

class GameLibrary;
class StageScene;

// What the renderer shows for one actor this frame.
struct ActorFrame {
    qint64 id = 0;
    QVector3D pos, scale{1, 1, 1};
    QQuaternion rot;
    Pose pose;                      // local transforms, one per skeleton bone (empty: rest pose)
    bool visible = true, selected = false;
};

struct ViewFrame {
    QVector3D pos;
    QQuaternion rot;                // roll included
    double fov = 50;                // horizontal degrees
    bool dof = false;
    double dofF = 2.8, dofFocus = 3;
    QString label;
};

class StageRuntime : public QObject {
    Q_OBJECT
public:
    explicit StageRuntime(GameLibrary *games, QObject *parent = nullptr);
    ~StageRuntime() override;

    // the Studio's contract (same as the bridge)
    void send(const QString &op, const QJsonObject &args);
    QJsonObject state() const { return m_state; }
    QJsonObject data() const { return m_data; }
    bool isDirtyOp(const QString &op) const;          // does this op change the film (undo step, unsaved mark)?

    // viewport
    void setScene(StageScene *scene);
    StageScene *scene() const;
    bool rendering() const { return m_rendering; }
    const ViewFrame &view() const { return m_view; }
    const QVector<ActorFrame> &frames() const { return m_frames; }
    void navigate(const QVector3D &pos, const QQuaternion &rot);   // viewport navigation moves the camera being looked through
    void pick(qint64 actorId, bool add);                            // click in the picture
    const PreparedModel *prepared(qint64 actorId) const;
    film::Document &document() { return m_doc; }
    double time() const { return m_t; }
    bool playing() const { return m_playing; }
    bool workView() const { return m_workOn || m_activeCam <= 0; }      // looking through the Work Camera

    // render: step the film deterministically (no wall clock)
    void renderBegin();
    void renderStep(double t, int shotCamera = -1);     // shotCamera: the shot's camera (a film render), -1 = the cuts
    void renderEnd();

    QString projectsDir() const;
    // Drive: the keys held in the viewport (forward / right in -1..1, Shift = jog)
    void setDriveInput(float forward, float right, bool fast) { m_driveIn = {forward, right, fast}; }
    QString projectName() const { return m_project; }

signals:
    void stateChanged();
    void dataChanged();
    void logLine(const QString &line);

private:
    using Op = std::function<void(const QJsonObject &)>;
    void registerOps();
    void registerRigOps();                              // StageRig.cpp
    void registerDriveOps();                            // StageDrive.cpp
    void registerImportOps();                           // StageImport.cpp
    void registerGizmoOps();                            // StageGizmo.cpp
    QJsonObject gizmoFrame() const;                     // where the viewport draws the handles
    // channels (StageChannels.cpp): any light / camera / character / constraint value keyed over time
    void registerChannelOps();
    // autosave and crash recovery (StageAutosave.cpp)
    void registerAutosaveOps();
    QString autosavePath() const;
    void initAutosave();
    void autosave();
    void dropAutosave();
    void endSession();
    std::optional<double> channelGet(const QString &target, const QString &field) const;
    bool channelSet(const QString &target, const QString &field, double v);
    QString channelLabel(const QString &target, const QString &field) const;
    film::Track *channelTrack(const QString &target, const QString &field);
    void keyChannel(const QString &target, const QString &field, double t, std::optional<double> v);
    void applyChannels(double t);
    QHash<QString, double> channelSnapshot() const;
    void keyChangedChannels(const QHash<QString, double> &before);
    QJsonObject channelsState() const;
    // external animations (StageImport.cpp): files under Documents/Director Studio, named "director/..." in commands
    QString studioDir() const;
    QString studioPath(const QString &path) const;
    QString rigCode(const film::Actor &a) const;
    QStringList importsFor(const QString &code) const;
    void tick();
    void evaluate(double t);
    void publish();
    void markData() { m_dataDirty = true; }
    QJsonObject buildState();
    QJsonObject buildData();
    void log(const QString &level, const QString &text);

    // history
    void pushUndo(const QString &label);
    void undo();
    void redo();
    void restore(const QJsonObject &snap);

    // assets
    void ensureModel(qint64 actorId);
    void ensureAnimations(const QString &path, std::function<void()> then = {});
    dir::AnimationSetPtr animations(const QString &path) const { return m_anims.value(path.toLower()); }
    const ClipBinding *binding(qint64 actorId, const QString &path, int mot);
    // the skeleton a list's owner has (a clip from another character plays as offsets from its bind pose)
    void ensureRig(const QString &code);
    int bankFor(const QString &path);
    QString pathForBank(int bank) const { return m_bankPaths.value(bank); }
    void buildCatalogs();
    // several games in one film (StageGames.cpp): references carry their game ("re2rt::look:pl0000/0"), each
    // loads from that game; the Asset Browser lists one game at a time (browseGame)
    QString filmGame() const;
    QString browseGame() const;
    QString refGame(const QString &ref) const;
    static QString refLocal(const QString &ref);
    static QString qualify(const QString &gameId, const QString &local);
    dir::IGameSource *sourceOf(const QString &ref) const;
    void qualifyDocument(film::Document &d) const;
    QString ownerOf(const QString &ref) const;
    void ensureGameCast(const QString &gameId);
    QJsonArray storeCast(const QString &gameId, const QList<dir::CatalogEntry> &chars);
    QJsonArray castOf(const film::Actor &a) const;       // the characters of the actor's game
    QJsonObject castEntry(const film::Actor &a) const;
    void sourceOpened(const QString &gameId);
    // the film's map: loads m_doc.stageId (or removes the map) on worker threads, with progress in the state
    void ensureStage();
    // hover preview (Asset Browser): a model turning in front of the camera, not part of the film
    static constexpr qint64 kPreviewId = -7;
    void setPreview(const QString &modelId, const QString &name);
    QJsonObject stageState() const;
    bool groundBelow(const QVector3D &from, float maxDrop, float *y) const;

    // helpers
    film::Actor *selectedActor();
    film::Actor *argActor(const QJsonObject &c);
    film::Track *trackAt(int idx);
    film::Track &track(const QString &kind, qint64 actor, int layer, bool *created = nullptr);
    film::Track &cammoveTrack(int cam);
    film::Track &cameraTrack();
    int newId() { return m_doc.nextId++; }
    void grow(double end);
    ViewFrame currentView() const;
    QVector3D spawnPoint(double distance, QQuaternion *facing) const;
    void keyTransform(film::Actor &a, double t, const QVector3D *pos = nullptr, const QQuaternion *rot = nullptr);
    void keyCamera(int cam, double t);
    void keyframePose(film::Actor &a, double t, const QMap<QString, QQuaternion> &joints);
    void commitPoseEdit(film::Actor &a);
    void commitRelativeEdit(film::Actor &a);            // the edit as an offset on the motion over the time selection
    void dropUnkeyedEdits();
    int filmCamera(double t) const;
    // the film: the shots in their order, each playing its own stretch of the scene through its own camera (SFM's
    // shots as clips: the same action covered from several cameras, edited in any order)
    double filmLength() const;
    bool filmAt(double filmT, int *shot, double *sceneT) const;
    Pose basePose(const film::Actor &a, double t, bool includeWork) const;
    QVector<const film::XKey *> resolveKeys(const QJsonArray &items, QVector<film::XKey *> *mut = nullptr);
    void setActiveCamera(int i);
    void spawnModel(const QString &modelId, const QString &name, const QString &kind, const QJsonObject &extra);
    void removeActor(qint64 id);

    // rig pass (StageRig.cpp): walk paths place characters before posing; look-at, constraints and props on
    // joints after
    void placePaths(double t, QHash<qint64, QPair<QVector3D, QQuaternion>> *out);
    void solveRigs(double t);
    bool jointWorld(const ActorFrame &f, const QString &joint, QVector3D *pos, QQuaternion *rot) const;
    const ActorFrame *frameOf(qint64 id) const;
    QVector3D constraintTarget(const film::Constraint &c, bool *ok) const;
    film::Track *pathTrack(qint64 actorId);
    void refreshPath(qint64 actorId);
    void attachPathClip(qint64 actorId, const QString &gait);
    QString generalFor(const film::Actor &a, bool anyone) const;
    QJsonArray constraintsState() const;
    // posing (StageRig.cpp): the bones a person poses, where they are on screen, IK for hands and feet
    QJsonArray skeletonState() const;
    QString effectorChain(qint64 actorId, const QString &joint) const;          // "L_Hand" ... or empty
    bool solveIkPose(qint64 actorId, const Pose &start, const QString &chain, const QVector3D &goalWorld, QMap<QString, QQuaternion> *out) const;
    std::optional<QQuaternion> currentLocal(const film::Actor &a, const QString &joint) const;   // on screen now
    // Drive & Record (StageDrive.cpp)
    // secondary motion (StagePhysics.cpp): "_chain" bones (hair, cloth, straps) on springs while time runs
    struct ChainSim {
        QString model;
        QVector<int> bones, parent, child;             // skeleton bone; parent / first child particle (-1: none)
        QVector<float> len, stiff;
        QVector<QVector3D> p, prev;                    // world positions now / a step ago
        double lastT = -1;
    };
    QHash<qint64, ChainSim> m_chains;
    void simulateChains(double t);
    void driveStart(qint64 actorId);
    void driveStop();
    void driveRecord(bool on);
    void driveOpenClip();
    void driveCloseClip();
    void driveStep(double dt);
    QJsonObject driveState() const;

    GameLibrary *m_games = nullptr;
    QPointer<StageScene> m_scene;
    film::Document m_doc;
    QHash<QString, Op> m_ops;
    QSet<QString> m_readonly;

    // transport
    double m_t = 0, m_speed = 1;
    bool m_playing = false, m_camFollow = false, m_sceneView = false, m_workOn = true, m_fly = false;
    int m_activeCam = 0;                               // 0 = work camera
    QElapsedTimer m_clock;
    qint64 m_lastTick = 0;
    QTimer m_timer;
    bool m_rendering = false;
    bool m_filmPlay = false;                            // playing the film (shots in order) rather than the scene
    double m_filmT = 0;
    int m_shotCam = -1;                                 // the camera the current shot forces (film playback / render)
    QPair<bool, int> m_renderViewBack{true, 0};         // Work Camera on / active camera before a render
    QTimer m_autosaveTimer;
    quint64 m_editSerial = 0, m_autosavedSerial = 0;    // film edits so far / at the last autosave
    QJsonObject m_recovery;                             // an autosave left by a session that crashed
    QStringList m_stageLightVariants;                   // the lightings of the map in the picture

    // selection & editor state
    qint64 m_selActor = 0;
    int m_selCamera = 0, m_selLight = 0;
    QJsonObject m_selClip;
    QSet<qint64> m_multi;
    bool m_relativeEdits = true;                        // Motion Editor edits offset the motion (SFM) rather than hold a pose
    bool m_autokey = false, m_motionEdit = false, m_skeleton = false, m_onion = false, m_showCameras = true;
    QJsonObject m_gizmo;
    QJsonObject m_keyClipboard;
    QString m_project = QStringLiteral("untitled");

    // history
    struct Snap { QString label; QJsonObject doc; };
    QVector<Snap> m_past, m_future;

    // assets
    QHash<qint64, PreparedModel> m_models;             // by actor
    QHash<QString, dir::ModelPtr> m_modelCache;        // by model id
    QSet<qint64> m_loadingModels;
    QHash<QString, dir::AnimationSetPtr> m_anims;       // by motion list path (lower case)
    QSet<QString> m_loadingAnims;
    QHash<QString, ClipBinding> m_bindings;            // actor|path|mot
    QHash<QString, QString> m_rigModel;                // game::owner code -> a look with that skeleton
    QHash<QString, QSharedPointer<const dir::Skeleton>> m_rigs;
    QSet<QString> m_rigLoading, m_rigFailed;
    QHash<QString, QJsonArray> m_castByGame;            // each open game's characters (cast.json shape, references qualified)
    QSet<QString> m_castLoading;
    QSet<QString> m_animsWaiting;                       // motion lists whose game is still opening
    QString m_catalogGame;                              // the game the catalogs below list
    // hover preview of a clip (Asset Browser): plays on one actor over everything, never part of the film
    qint64 m_clipPreviewActor = 0;
    QString m_clipPreviewPath;
    int m_clipPreviewMot = -1;
    double m_clipPreviewSpeed = 1;
    qint64 m_clipPreviewStartMs = 0;
    QSet<qint64> m_pathLocked;                          // characters a walk path moves (their clips play in place)
    QHash<qint64, QPair<double, QQuaternion>> m_lookCur; // look-at smoothing: film time and the current turn
    // the viewport pen: clicks on the floor add path points (path) or place a character (place)
    QString m_pen;
    qint64 m_penActor = 0;
    QJsonObject m_penSpec;
    QString m_lastRig, m_lastExport;                    // export_rig / export_anim results
    struct GizmoDrag {                                  // a manipulator drag in progress (start values)
        bool on = false;
        qint64 actor = 0;
        QString op, joint;
        int axis = 0;
        QVector3D dir, pos, scale{1, 1, 1};
        QQuaternion rot, jointWorld, parentWorld;
        QString ikChain;                                // a hand / foot moved by IK
        QVector3D ikStart;
        Pose startPose;
    } m_gizmoDrag;
    struct DriveTake {
        bool on = false, recording = false;
        qint64 actor = 0;
        QString state, list;                            // idle | walk | jog; the locomotion motion list
        int idle = -1, walk = -1, jog = -1;
        double walkSpeed = 1.35, jogSpeed = 3.1, lastKey = -99;
        int recClip = 0;                                // the clip being recorded
    } m_drive;
    struct DriveInput { float forward = 0, right = 0; bool fast = false; } m_driveIn;
    QHash<int, QString> m_bankPaths;
    QHash<QString, int> m_banks;
    QJsonArray m_cast, m_animCatalog, m_meshResults, m_catalogResults, m_groups, m_owners, m_logTail;
    QJsonObject m_chars;
    QList<dir::CatalogEntry> m_props;
    QList<dir::CatalogEntry> m_stageCatalog;
    QJsonArray m_stages;                                // Asset Browser rows (bridge.lua Stage.list shape)
    QString m_stageShown, m_stageLoading;               // stage id in the picture / being loaded
    QString m_stageShownName;
    std::optional<QVector3D> m_stageArrive;             // named place to stand the Work Camera on once the map is in
    bool m_stageArriveCenter = false;                   // no named place: the middle of what the map places
    QString m_previewModel, m_previewName;
    int m_previewSerial = 0;
    bool m_previewShown = false;
    qint64 m_previewStartMs = 0;
    QVector3D m_previewSize, m_previewCenter;
    int m_stageShownNumber = 0;
    int m_stageSerial = 0;
    QJsonObject m_stageStatus;                          // {phase: loading|done|failed, name, stage, error, done, total}
    std::shared_ptr<std::atomic_int> m_stageDone, m_stageTotal, m_stageCurrent;
    QJsonObject m_eval;

    // outputs
    QVector<ActorFrame> m_frames;
    ViewFrame m_view;
    QJsonObject m_state, m_data;
    bool m_dataDirty = true;
    int m_dataVer = 0;
    qint64 m_ack = 0;
};
