// The film document of the standalone runtime: actors (characters and props from a game), cameras, lights and
// the sequence (tracks of clips and keys). It is the editor's own data — saved as a project file, snapshotted
// for undo — and it speaks the Lua runtime's JSON shapes so every Studio panel works on both runtimes.
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QQuaternion>
#include <QString>
#include <QVector>
#include <QVector3D>
#include <optional>

namespace film {

// ---------------------------------------------------------------- sequence
struct XKey {                       // placement (xform), camera-move (cammove) and channel keys
    double t = 0;
    double v = 0;                   // channel keys: the value
    QVector3D pos;
    QQuaternion rot;
    double fov = 0, roll = 0;       // cammove only (fov 0 = unset)
    std::optional<double> dofF, dofFocus;
    QString ease;                   // "" = smooth
};

struct AnimClip {
    int id = 0;
    double start = 0, dur = 1;
    QString path;                   // motion list (game path)
    int mot = -1;                   // motion id in the list
    QString name;
    double endframe = 0, offset = 0, speed = 1, blend = 10;
    bool loop = false;
};

struct PoseKey {
    double t = 0;                   // relative to the clip start
    QMap<QString, QQuaternion> joints;
    QString ease;
};

struct PoseClip {
    int id = 0;
    double start = 0, dur = 1;
    QVector<PoseKey> keys;
    double weight = 1, fadeIn = 10, fadeOut = 10;
    bool loop = false;
    QString mode = QStringLiteral("override");
};

struct Cut { int id = 0; double start = 0; int cam = 0; };

// a walk path (path.lua): the character walks a Catmull-Rom curve through the points at constant ground speed,
// with its walk / jog clip on the anim track kept in step
struct PathSpec {
    QVector<QVector3D> points;
    double start = 0, dur = 60;     // frames
    double speed = 1.35;            // m/s
    double ease = 0.08;             // share of the walk spent accelerating / braking
    QString gait = QStringLiteral("walk");
    int clipId = 0;                 // the walk clip on the actor's layer-0 anim track
    QString clipName;
    double clipSpeedRef = 0;        // the clip's own ground speed (m/s), measured from its root motion
    bool durPinned = false, speedPinned = false;
};

struct Track {
    QString kind;                   // anim | pose | xform | cammove | camera | path | channel
    QString name;
    qint64 actor = 0;
    QString actorName;
    int layer = 0;
    int cam = 0;                    // cammove
    QString lookatName;             // cammove: aim at this actor while moving
    QVector<AnimClip> clips;
    QVector<PoseClip> poses;
    QVector<XKey> keys;
    QVector<Cut> cuts;
    PathSpec path;                  // path
    // channel: one animated value of a light, camera, character or constraint (SFM's channel): "light:3" +
    // "intensity", "cam:2" + "dof_focus", "actor:1001" + "lookat_weight", "constraint:4" + "weight"
    QString target, field;
};

struct Shot { int id = 0; QString name; double a = 0, b = 0; int cam = 0; QJsonObject extra; };

struct Sequence {
    QString name = QStringLiteral("sequence");
    double fps = 60, length = 600;
    bool loop = false;
    QVector<Track> tracks;
    std::optional<std::pair<double, double>> range;
    std::optional<std::pair<double, double>> falloff;
    QJsonObject audio;
    QVector<Shot> shots;
};

// ---------------------------------------------------------------- scene
struct Camera {
    QString name;
    QString mode = QStringLiteral("static");
    QVector3D pos;
    QQuaternion rot;
    double fov = 50, roll = 0;
    bool dof = true;
    std::optional<double> dofF, dofFocus;
    qint64 target = 0;
    QJsonObject shake;              // { amp, rot, freq } or empty
};

struct Light {
    int id = 0;
    QString kind = QStringLiteral("point");        // point | spot | sun | area
    QString name;
    QVector3D pos;
    QQuaternion rot;
    double intensity = 1500, radius = 8, cone = 45, spread = 0.5, temperature = 6500, bounce = 1, volumetric = 1, specular = 1, size = 0.25;
    QVector3D color{1, 0.96f, 0.9f};
    bool shadows = true, enabled = true, blackbody = false;
};

// head (and some neck) turned towards the camera, another character or a point, on top of the animation
struct LookAt {
    bool enabled = false;
    QString kind = QStringLiteral("camera");       // camera | actor | point
    qint64 target = 0;
    QVector3D point;
    double weight = 1, maxDeg = 75, smooth = 0.12, neckShare = 0.35;
    bool flip = false;
};

// an object carried by a joint of a character (props in a hand)
struct Attachment {
    qint64 actor = 0;
    QString joint;
    QVector3D pos;                  // offset in the joint's frame
    QQuaternion rot;
};

// kept true every frame after the animation (constraint.lua): a hand holds something, a limb stays planted,
// the head keeps looking at something
struct Constraint {
    int id = 0;
    QString kind = QStringLiteral("ik_pin");       // ik_pin | plant | look
    qint64 actor = 0;
    QString chain = QStringLiteral("R_Hand");      // R_Hand | L_Hand | R_Foot | L_Foot
    qint64 targetActor = 0;
    QString targetJoint;
    std::optional<QVector3D> point;                // a fixed world point (plant: where the limb was)
    QVector3D offset;
    double weight = 1;
    std::optional<std::pair<double, double>> range;
    bool enabled = true;
    QString name;
};

// the light around the film: the Studio's sun and sky, and which of the map's own lightings is on
struct World {
    QString sun = QStringLiteral("auto");         // auto (on while the film has no lights of its own) | on | off
    double sunStrength = 1, sunYaw = 32, sunElevation = 38, sunTemp = 5600;
    double sky = 1;                                // sky light / reflections
    QString lighting;                              // the map's lighting: "" = its first, "none", or a variant ("chp1_3")
    double mapLights = 1;                          // strength of the map's lights
    // the look (post-processing): exposure in stops, colour, bloom, vignette, sharpening
    double exposure = 0, contrast = 1, saturation = 1, brightness = 1, bloom = 0, vignette = 0, sharpen = 0;
    QString tonemap = QStringLiteral("filmic");    // filmic | aces | hejl | linear
};

struct Actor {
    qint64 id = 0;
    QString name, displayName;
    QString kind = QStringLiteral("character");   // character | object
    QString model;                                 // plugin model id (look:..., mesh:...)
    QString castId, castTree, castCode, preset;    // characters from the cast catalog
    QVector3D pos;
    QQuaternion rot;
    QVector3D scale{1, 1, 1};
    bool hidden = false;
    bool rootLock = false;                         // animations play in place (no root motion)
    bool physics = true;                           // hair, cloth and straps swing (secondary motion)
    QString idlePath;                              // motion list of the idle loop ("" = none)
    int idleMot = -1;
    // working pose (bones edited but not keyed yet)
    QMap<QString, QQuaternion> work;
    QString workJoint;
    double workWeight = 1;
    // a motion previewed from the Asset Browser (plays in real time until the timeline takes over)
    QString livePath;
    int liveMot = -1;
    double liveSpeed = 1;
    qint64 liveStartMs = 0;
    bool livePaused = false;
    double liveFrame = 0;                          // while paused
    // loaded motion lists: bank id -> path
    QMap<int, QString> banks;
    LookAt lookat;
    std::optional<Attachment> attached;
};

struct Document {
    QVector<Actor> actors;
    QVector<Camera> cameras;        // scene cameras, 1-based in the UI (index + 1)
    Camera work;                    // the Work Camera: never keyed, rendered or saved in the film
    bool hasWork = false;
    QVector<Light> lights;
    QVector<Constraint> constraints;
    Sequence seq;
    int nextId = 1;                 // clips, cuts, shots, lights
    qint64 nextActor = 1001;
    QJsonObject overlay{{QStringLiteral("letterbox"), 0}, {QStringLiteral("thirds"), false}, {QStringLiteral("safe"), false}, {QStringLiteral("center"), false}};
    QString gameKey;                // the game this film was made with
    // the map the film plays on ("" = the empty studio stage): plugin stage id, its label and chunk number
    QString stageId, stageName;
    int stageNumber = 0;
    World world;

    Actor *actor(qint64 id);
    const Actor *actor(qint64 id) const;
    Actor *actorByName(const QString &name);
    Light *light(int id);
    Constraint *constraint(int id);
    Camera *camera(int i) { return i >= 1 && i <= cameras.size() ? &cameras[i - 1] : (i == 0 && hasWork ? &work : nullptr); }
};

// JSON: project files / undo snapshots (complete) and the Studio state (display shapes)
QJsonObject toJson(const Document &d);
QJsonObject worldJson(const World &w);
World worldFrom(const QJsonObject &o);
Document fromJson(const QJsonObject &o);
QJsonObject sequenceState(const Document &d, double t, bool playing, double speed);
QJsonArray camerasState(const Document &d, int liveCamera);
QJsonArray lightsState(const Document &d);

// evaluation helpers shared with the runtime
double clipFrame(const AnimClip &c, double t);
const AnimClip *clipAt(const Track &tr, double t);
const PoseClip *poseClipAt(const Track &tr, double t);
const Cut *cutAt(const Track &tr, double t);
double fadeWeight(double lt, double dur, double fin, double fout);
QMap<QString, QQuaternion> evalPoseKeys(const QVector<PoseKey> &keys, double t, bool loop);
bool evalKeys(const QVector<XKey> &keys, double t, XKey *out);    // interpolated key at t
bool evalChannel(const QVector<XKey> &keys, double t, double *v);   // a channel's value at t

// walk paths (path.lua): a Catmull-Rom curve with an arc-length table
struct PathCurve {
    QVector<QVector3D> pos, tan;    // samples along the curve, ground tangents
    QVector<double> dist;           // distance from the first point
    double length = 0;
};
PathCurve pathCurve(const QVector<QVector3D> &points);
bool pathAt(const PathCurve &c, double d, QVector3D *pos, QVector3D *tan);
double pathDistanceAt(const PathSpec &p, double length, double t);   // ground walked at frame t (eased in / out)
double pathDuration(const PathSpec &p, double length);               // frames at the path's speed

} // namespace film
