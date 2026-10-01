// The 3D picture of the standalone runtime: owns the SceneActors under the viewport's stage node, the lights and
// the view camera, and copies the runtime's evaluated frame onto them. Declared in the viewport QML
// (Director.Engine / StageScene) and attached to the runtime by the Studio.
#pragma once

#include "ModelPrep.h"
#include "StageSet.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVariantAnimation>
#include <QVariantMap>
#include <QVector3D>

class QQuick3DNode;
class QQuick3DObject;
class SceneActor;
class StageRuntime;
namespace film { struct Document; }
class QQmlEngine;

class StageScene : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *view READ view WRITE setView NOTIFY changed)
    Q_PROPERTY(QObject *root READ root WRITE setRoot NOTIFY changed)
    Q_PROPERTY(QObject *camera READ camera WRITE setCamera NOTIFY changed)
    Q_PROPERTY(QObject *environment READ environment WRITE setEnvironment NOTIFY changed)
    Q_PROPERTY(QObject *lightRoot READ lightRoot WRITE setLightRoot NOTIFY changed)
    // the export view (same scene, output resolution): its camera and environment follow the viewport's
    Q_PROPERTY(QObject *renderCamera READ renderCamera WRITE setRenderCamera NOTIFY changed)
    Q_PROPERTY(QObject *renderEnvironment READ renderEnvironment WRITE setRenderEnvironment NOTIFY changed)
    Q_PROPERTY(int actorCount READ actorCount NOTIFY actorsChanged)
    Q_PROPERTY(QString viewLabel READ viewLabel NOTIFY frameChanged)
    Q_PROPERTY(bool userLights READ userLights NOTIFY frameChanged)
    // the Studio's sun and sky for QML: {sunOn, sunBrightness, sunEuler (vector3d), sunColor, fills, sky}
    Q_PROPERTY(QVariantMap world READ world NOTIFY frameChanged)
    Q_PROPERTY(bool hasStage READ hasStage NOTIFY stageChanged)
    // Blender-style viewing: axis views (Front, Right, Top ...), orthographic, and the shading of the picture
    Q_PROPERTY(bool ortho READ ortho WRITE setOrtho NOTIFY viewChanged)
    Q_PROPERTY(double orthoWidth READ orthoWidth NOTIFY orthoWidthChanged)       // metres across (orthographic)
    Q_PROPERTY(QString viewName READ viewName NOTIFY viewChanged)                // User | Front | Right | Top ...
    Q_PROPERTY(QString shading READ shading WRITE setShading NOTIFY viewChanged) // wire | solid | material | rendered
public:
    explicit StageScene(QObject *parent = nullptr);
    ~StageScene() override;

    QObject *view() const { return m_view; }
    void setView(QObject *v) { m_view = v; emit changed(); }
    QObject *root() const { return m_root; }
    void setRoot(QObject *r);
    QObject *camera() const { return m_camera; }
    void setCamera(QObject *c) { m_camera = c; emit changed(); }
    QObject *environment() const { return m_env; }
    void setEnvironment(QObject *e) { m_env = e; emit changed(); }
    QObject *lightRoot() const { return m_lightRoot; }
    void setLightRoot(QObject *r) { m_lightRoot = r; emit changed(); }
    QObject *renderCamera() const { return m_renderCamera; }
    void setRenderCamera(QObject *c) { m_renderCamera = c; emit changed(); }
    QObject *renderEnvironment() const { return m_renderEnv; }
    void setRenderEnvironment(QObject *e) { m_renderEnv = e; emit changed(); }
    int actorCount() const { return int(m_actors.size()); }
    QString viewLabel() const { return m_viewLabel; }
    bool userLights() const { return m_userLights; }
    QVariantMap world() const { return m_world; }
    bool hasStage() const { return !m_stage.isNull() || m_pendingStage.stage; }
    bool ortho() const { return m_ortho; }
    void setOrtho(bool on);
    double orthoWidth() const { return m_orthoWidth; }
    QString viewName() const { return m_viewName; }
    QString shading() const { return m_shading; }
    void setShading(const QString &s);
    // front | back | right | left | top | bottom (Blender's numpad 1 3 7 and Ctrl+), smoothly
    Q_INVOKABLE void viewAxis(const QString &axis);
    Q_INVOKABLE void orbitStep(double yawDeg, double pitchDeg);   // numpad 4 6 8 2
    Q_INVOKABLE void flipView();                                  // numpad 9: the other side
    Q_INVOKABLE void frameAll();                                  // Home: everything in the film

    void addActor(qint64 id, const PreparedModel &pm);
    void removeActor(qint64 id);
    void clearActors();
    // the map: built under the stage root (after any previous one is removed); textures move to the GPU
    bool setStage(const PreparedStage &ps, QString *error = nullptr);
    void clearStage();
    QString stageId() const { return m_stage ? m_stage->stageId() : QString(); }
    // the first surface of the map below `from`; false with no map or nothing below
    bool groundBelow(const QVector3D &from, float maxDrop, float *y) const;
    QVector<float> surfacesAt(float x, float z) const;
    void sync(StageRuntime &rt);

    void setRuntime(StageRuntime *rt) { m_rt = rt; }

    // work-camera navigation (SFM): look (LMB drag), pan (MMB), dolly (RMB / wheel), orbit (Alt+LMB), fly (WASD+QE)
    Q_INVOKABLE void look(double dx, double dy);
    Q_INVOKABLE void pan(double dx, double dy);
    Q_INVOKABLE void dolly(double amount);
    Q_INVOKABLE void orbit(double dx, double dy);
    Q_INVOKABLE void fly(double forward, double right, double up, double seconds, bool fast);
    Q_INVOKABLE void frameSelection();
    // Drive: the keys held while a character is driven (forward / right -1..1, Shift = jog)
    Q_INVOKABLE void drive(double forward, double right, bool fast);
    Q_INVOKABLE void pickAt(QObject *picked, bool add);
    // where a ray from the camera meets the map (else the studio floor, y = 0): [x, y, z], or empty
    Q_INVOKABLE QVariantList groundHit(const QVector3D &origin, const QVector3D &dir) const;
    // the map mesh a ray from the view meets first (its game name), "" if none
    Q_INVOKABLE QString stageObjectAt(const QVector3D &origin, const QVector3D &dir) const;

    // picking: the actor a picked model belongs to (0 = none)
    Q_INVOKABLE double actorOf(QObject *pickedObject) const;
    // world position of an actor's bounds centre (framing)
    Q_INVOKABLE QVector3D actorCenter(double id) const;
    Q_INVOKABLE double actorRadius(double id) const;

signals:
    void changed();
    void actorsChanged();
    void frameChanged();
    void stageChanged();
    void viewChanged();
    void orthoWidthChanged();

private:
    void rebuildExtensions();
    QQmlEngine *engine() const;

    QPointer<QObject> m_view, m_root, m_camera, m_env, m_lightRoot, m_renderCamera, m_renderEnv;
    QHash<qint64, QPointer<SceneActor>> m_actors;
    QHash<qint64, PreparedModel> m_pending;          // arrived before the viewport existed
    QHash<int, QPointer<QQuick3DNode>> m_lights;
    // the map's own lights: all of them, and the few in the picture (Qt draws at most 15 lights at once)
    QVector<dir::StageLight> m_mapLights;
    QHash<int, QPointer<QQuick3DNode>> m_mapLightNodes;     // index in m_mapLights -> node
    QVector3D m_mapLightsFrom{1e9f, 1e9f, 1e9f};
    QString m_mapLightsKey;
    QVariantMap m_world;
    void syncMapLights(const film::Document &doc, const QVector3D &eye, int budget);
    QPointer<StageSet> m_stage;
    PreparedStage m_pendingStage;                   // arrived before the viewport existed
    QString m_viewLabel;
    StageRuntime *m_rt = nullptr;
    QVector3D m_pivot{0, 1, 0};
    bool m_userLights = false;
    bool m_ortho = false, m_autoOrtho = false;       // auto: an axis view turned it on (orbiting turns it off)
    double m_orthoWidth = 10;
    QString m_viewName = QStringLiteral("User"), m_shading = QStringLiteral("rendered");
    QVariantAnimation *m_viewAnim = nullptr;
    bool m_clay = false;                              // Solid shading applied to the materials (never while rendering)
    void applyClay(bool on);
    void animateView(const QVector3D &pos, const QQuaternion &rot, const QString &name, bool ortho);
    void leaveAxisView();
};
