#include "StageScene.h"
#include "Maths.h"
#include "SceneActor.h"
#include "StageRuntime.h"

#include <QColor>
#include <QQmlEngine>
#include <QQmlListReference>
#include <QtQuick3D/private/qquick3ddirectionallight_p.h>
#include <QtQuick3D/private/qquick3dpointlight_p.h>
#include <QtQuick3D/private/qquick3dspotlight_p.h>

StageScene::StageScene(QObject *parent) : QObject(parent) {}

StageScene::~StageScene()
{
    for (const auto &a : std::as_const(m_actors)) if (a) delete a.data();
    for (const auto &l : std::as_const(m_lights)) if (l) delete l.data();
    if (m_stage) delete m_stage.data();
}

QQmlEngine *StageScene::engine() const
{
    return m_root ? qmlEngine(m_root) : nullptr;
}

void StageScene::setRoot(QObject *r)
{
    m_root = r;
    emit changed();
    // models that finished loading before the viewport existed
    const auto pending = m_pending;
    m_pending.clear();
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) addActor(it.key(), it.value());
    if (m_pendingStage.stage) {
        const PreparedStage ps = m_pendingStage;
        m_pendingStage = {};
        setStage(ps);
    }
}

bool StageScene::setStage(const PreparedStage &ps, QString *error)
{
    auto *root = qobject_cast<QQuick3DNode *>(m_root.data());
    if (!root || !engine()) { m_pendingStage = ps; emit stageChanged(); return true; }
    clearStage();
    m_mapLights = ps.stage ? ps.stage->lights : QVector<dir::StageLight>();
    auto *set = new StageSet;
    set->setParent(root);
    set->setParentItem(root);
    QString err;
    if (!set->build(ps, engine(), &err)) {
        qWarning().noquote() << "stage" << ps.stage->name << err;
        if (error) *error = err;
        delete set;
        emit stageChanged();
        return false;
    }
    m_stage = set;
    if (m_clay) set->setClay(true);
    rebuildExtensions();
    emit stageChanged();
    return true;
}

void StageScene::clearStage()
{
    m_pendingStage = {};
    m_mapLights.clear();
    for (const auto &n : std::as_const(m_mapLightNodes)) if (n) delete n.data();
    m_mapLightNodes.clear();
    m_mapLightsKey.clear();
    if (m_stage) {
        delete m_stage.data();
        m_stage = nullptr;
        rebuildExtensions();
    }
    emit stageChanged();
}

bool StageScene::groundBelow(const QVector3D &from, float maxDrop, float *y) const
{
    return m_stage && m_stage->groundBelow(from, maxDrop, y);
}

QVector<float> StageScene::surfacesAt(float x, float z) const { return m_stage ? m_stage->surfacesAt(x, z) : QVector<float>(); }

void StageScene::addActor(qint64 id, const PreparedModel &pm)
{
    auto *root = qobject_cast<QQuick3DNode *>(m_root.data());
    if (!root || !engine()) { m_pending.insert(id, pm); return; }
    removeActor(id);
    auto *actor = new SceneActor;
    actor->setActorId(id);
    actor->setParent(root);
    actor->setParentItem(root);
    QString err;
    if (!actor->build(pm, engine(), &err)) {
        qWarning().noquote() << "scene actor" << id << err;
        delete actor;
        return;
    }
    m_actors.insert(id, actor);
    if (m_clay) actor->setClay(true);
    rebuildExtensions();
    emit actorsChanged();
}

void StageScene::removeActor(qint64 id)
{
    m_pending.remove(id);
    if (auto a = m_actors.take(id)) {
        delete a.data();
        rebuildExtensions();
        emit actorsChanged();
    }
}

void StageScene::clearActors()
{
    m_pending.clear();
    for (const auto &a : std::as_const(m_actors)) if (a) delete a.data();
    m_actors.clear();
    rebuildExtensions();
    emit actorsChanged();
}

// The View3D runs only the render extensions in its list: every actor's GPU textures
void StageScene::rebuildExtensions()
{
    if (!m_view) return;
    QQmlListReference ext(m_view, "extensions");
    if (!ext.isValid()) return;
    ext.clear();
    for (const auto &a : std::as_const(m_actors))
        if (a) for (QQuick3DObject *e : a->extensions()) ext.append(e);
    if (m_stage) for (QQuick3DObject *e : m_stage->extensions()) ext.append(e);
}

double StageScene::actorOf(QObject *picked) const
{
    for (QObject *o = picked; o; o = o->parent())
        if (auto *a = qobject_cast<SceneActor *>(o)) return double(a->actorId());
    return 0;
}

QVector3D StageScene::actorCenter(double id) const
{
    const auto a = m_actors.value(qint64(id));
    if (!a) return {};
    // the bounds are the bind pose's; root motion (a walk) carries them along with the root bone
    const QVector3D root = a->boneModelMatrix(0).column(3).toVector3D();
    const QVector3D c = (a->boundsMin() + a->boundsMax()) * 0.5f + QVector3D(root.x(), 0, root.z());
    return a->position() + a->rotation().rotatedVector(c * a->scale());
}

double StageScene::actorRadius(double id) const
{
    const auto a = m_actors.value(qint64(id));
    return a ? double((a->boundsMax() - a->boundsMin()).length() * 0.5f * a->scale().x()) : 1.0;
}

void StageScene::sync(StageRuntime &rt)
{
    for (const ActorFrame &f : rt.frames()) {
        SceneActor *a = m_actors.value(f.id);
        if (!a) continue;
        a->setPosition(f.pos);
        a->setRotation(f.rot);
        a->setScale(f.scale);
        a->setVisible(f.visible);
        if (!f.pose.isEmpty()) a->setPose(f.pose);
        a->setHighlight(f.selected && !rt.rendering() ? 1.0f : 0.0f);   // exports never show the selection
    }
    // lights
    auto *lightRoot = qobject_cast<QQuick3DNode *>(m_lightRoot ? m_lightRoot.data() : m_root.data());
    QSet<int> alive;
    const film::Document &doc = rt.document();
    for (const film::Light &l : doc.lights) {
        alive.insert(l.id);
        QPointer<QQuick3DNode> &node = m_lights[l.id];
        const QString kind = node ? node->property("directorKind").toString() : QString();
        if (node && kind != l.kind) { delete node.data(); node = nullptr; }
        if (!node && lightRoot) {
            QQuick3DAbstractLight *light = nullptr;
            if (l.kind == QLatin1String("spot")) light = new QQuick3DSpotLight;
            else if (l.kind == QLatin1String("sun")) light = new QQuick3DDirectionalLight;
            else light = new QQuick3DPointLight;
            light->setProperty("directorKind", l.kind);
            light->setParent(lightRoot);
            light->setParentItem(lightRoot);
            node = light;
        }
        if (!node) continue;
        node->setPosition(l.pos);
        node->setRotation(l.rot);
        node->setVisible(l.enabled);
        // RE lights are in lumen-like units; Qt's brightness is ~1 for a normal key light
        const double brightness = l.kind == QLatin1String("sun") ? l.intensity / 3.0 : l.intensity / 900.0;
        node->setProperty("brightness", brightness);
        const QVector3D col = l.blackbody ? fm::kelvinColor(l.temperature) : l.color;
        node->setProperty("color", QColor::fromRgbF(float(col.x()), float(col.y()), float(col.z())));
        node->setProperty("castsShadow", l.shadows);
        node->setProperty("shadowFactor", 60.0);
        node->setProperty("shadowMapQuality", 3);
        if (l.kind != QLatin1String("sun")) {
            // fade to ~5% at the light's radius: Qt scales quadraticFade by 1e-4 (it assumes centimetres) and our
            // distances are metres, so 1 / (1 + q * 1e-4 * r^2) = 0.05  ->  q = 19e4 / r^2
            const double r = std::max(0.5, l.radius);
            node->setProperty("constantFade", 1.0);
            node->setProperty("linearFade", 0.0);
            node->setProperty("quadraticFade", 19.0e4 / (r * r));
        }
        if (l.kind == QLatin1String("spot")) {
            node->setProperty("coneAngle", l.cone);
            node->setProperty("innerConeAngle", l.cone * std::clamp(1.0 - l.spread, 0.05, 0.95));
        }
    }
    for (auto it = m_lights.begin(); it != m_lights.end();) {
        if (!alive.contains(it.key())) { if (it.value()) delete it.value().data(); it = m_lights.erase(it); }
        else ++it;
    }
    // what the picture shows: Rendered = everything; Material preview / Solid / Wireframe = the Studio's own light
    // (a render is always Rendered)
    const bool full = m_shading == QLatin1String("rendered") || rt.rendering();
    const bool clay = m_shading == QLatin1String("solid") && !rt.rendering();
    if (clay != m_clay) applyClay(clay);
    for (auto it = m_lights.cbegin(); it != m_lights.cend(); ++it)
        if (it.value() && !full) it.value()->setVisible(false);
    const bool user = full && !doc.lights.isEmpty();
    {
        const double dist = std::max(0.05f, (rt.view().pos - m_pivot).length());
        const double w = 2.0 * dist * std::tan(std::max(1.0, rt.view().fov) * 0.5 * fm::kPi / 180.0);
        if (std::abs(w - m_orthoWidth) > 1e-3 * std::max(1.0, m_orthoWidth)) { m_orthoWidth = w; emit orthoWidthChanged(); }
    }
    // the sun and sky (the default rig: a sun and two fills while the film has no lights of its own)
    const film::World preview;
    const film::World &w = full ? doc.world : preview;
    const bool sunOn = w.sun == QLatin1String("on") || (w.sun != QLatin1String("off") && !user);
    QVariantMap world{{QStringLiteral("sunOn"), sunOn}, {QStringLiteral("sunBrightness"), 1.9 * w.sunStrength},
                      {QStringLiteral("sunEuler"), QVector3D(-float(w.sunElevation), float(w.sunYaw), 0)},
                      {QStringLiteral("sunColor"), [&] { const QVector3D k = fm::kelvinColor(w.sunTemp); return QColor::fromRgbF(k.x(), k.y(), k.z()); }()}, {QStringLiteral("fills"), !user && w.sun != QLatin1String("off")},
                      {QStringLiteral("sky"), w.sky}};
    if (world != m_world) { m_world = world; emit frameChanged(); }
    // the map's lights, within what is left of Qt's 15 (sun, fills, the film's own)
    const int used = (sunOn ? 1 : 0) + (world.value(QStringLiteral("fills")).toBool() ? 2 : 0) + int(doc.lights.size());
    syncMapLights(doc, rt.view().pos, full ? std::clamp(15 - used, 0, 12) : 0);
    // the view
    const ViewFrame &v = rt.view();
    for (QObject *cam : {m_camera.data(), m_renderCamera.data()}) {
        if (!cam) continue;
        cam->setProperty("position", v.pos);
        cam->setProperty("rotation", v.rot);
        cam->setProperty("fieldOfView", v.fov);
    }
    const film::World &look = full ? doc.world : preview;
    const int tonemap = look.tonemap == QLatin1String("linear") ? 1 : look.tonemap == QLatin1String("aces") ? 2
                      : look.tonemap == QLatin1String("hejl") ? 3 : 4;      // QQuick3DSceneEnvironment::QQuick3DEnvironmentTonemapModes (None 0 ... Filmic 4)
    for (QObject *env : {m_env.data(), m_renderEnv.data()}) {
        if (!env) continue;
        env->setProperty("exposure", std::pow(2.0, look.exposure));
        env->setProperty("tonemapMode", tonemap);
        const bool adjust = std::abs(look.contrast - 1) > 1e-3 || std::abs(look.saturation - 1) > 1e-3 || std::abs(look.brightness - 1) > 1e-3;
        env->setProperty("colorAdjustmentsEnabled", adjust);
        if (adjust) {
            env->setProperty("adjustmentBrightness", look.brightness);
            env->setProperty("adjustmentContrast", look.contrast);
            env->setProperty("adjustmentSaturation", look.saturation);
        }
        env->setProperty("glowEnabled", look.bloom > 0.001);
        if (look.bloom > 0.001) {
            env->setProperty("glowIntensity", 0.4 + look.bloom * 1.6);
            env->setProperty("glowStrength", 0.6 + look.bloom * 0.8);
            env->setProperty("glowBloom", look.bloom * 0.5);
            env->setProperty("glowHDRMinimumValue", 1.0 - look.bloom * 0.5);
            env->setProperty("glowQualityHigh", true);
        }
        env->setProperty("vignetteEnabled", look.vignette > 0.001);
        if (look.vignette > 0.001) {
            env->setProperty("vignetteStrength", look.vignette * 15.0);
            env->setProperty("vignetteRadius", 0.35);
        }
        env->setProperty("sharpnessAmount", look.sharpen);
        env->setProperty("depthOfFieldEnabled", v.dof);
        if (v.dof) {
            // scene units are metres; a wider aperture gives a shallower focus band and stronger blur
            env->setProperty("depthOfFieldFocusDistance", v.dofFocus);
            env->setProperty("depthOfFieldFocusRange", std::clamp(v.dofF * v.dofFocus * 0.06, 0.05, 20.0));
            env->setProperty("depthOfFieldBlurAmount", std::clamp(8.0 / v.dofF, 0.5, 12.0));
        }
    }
    if (m_viewLabel != v.label || m_userLights != user) {
        m_viewLabel = v.label;
        m_userLights = user;
        emit frameChanged();
    }
}

// the map's own lights of the film's lighting: the ones that matter most where the camera is (bright, near,
// reaching it) get the few lights Qt draws; picked again when the camera has moved a metre
void StageScene::syncMapLights(const film::Document &doc, const QVector3D &eye, int budget)
{
    auto *lightRoot = qobject_cast<QQuick3DNode *>(m_lightRoot ? m_lightRoot.data() : m_root.data());
    const film::World &w = doc.world;
    QString variant = w.lighting;
    if (variant.isEmpty())
        for (const dir::StageLight &l : std::as_const(m_mapLights)) if (!l.variant.isEmpty() && (variant.isEmpty() || l.variant < variant)) variant = l.variant;

    const QString key = QStringLiteral("%1|%2|%3").arg(variant).arg(budget).arg(w.mapLights);
    if (key == m_mapLightsKey && (eye - m_mapLightsFrom).length() < 1.0f) return;
    m_mapLightsKey = key;
    m_mapLightsFrom = eye;
    // rank
    QVector<QPair<float, int>> ranked;
    if (variant != QLatin1String("none") && w.mapLights > 0.001)
        for (int i = 0; i < m_mapLights.size(); ++i) {
            const dir::StageLight &l = m_mapLights[i];
            if (!l.variant.isEmpty() && l.variant != variant) continue;
            if (l.kind == dir::StageLight::Directional) continue;              // the Studio's sun stands in
            if (l.range > 150) continue;       // RE4's "fog lights": 1e9 lm, km away, they colour the distant fog we do not draw
            const float d = (l.world.column(3).toVector3D() - eye).length();
            const float reach = std::max(0.5f, l.range);
            if (d > reach * 4 + 30) continue;
            ranked.append({l.intensity / (1.f + (d / reach) * (d / reach)), i});
        }
    std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    QSet<int> want;
    for (int k = 0; k < std::min<int>(budget, ranked.size()); ++k) want.insert(ranked[k].second);
    for (auto it = m_mapLightNodes.begin(); it != m_mapLightNodes.end();) {
        if (!want.contains(it.key())) { if (it.value()) delete it.value().data(); it = m_mapLightNodes.erase(it); }
        else ++it;
    }
    if (qEnvironmentVariableIsSet("DIRECTOR_LIGHTS_LOG"))
        for (int i : std::as_const(want))
            qInfo().noquote() << "map light" << m_mapLights[i].name << (m_mapLights[i].kind == dir::StageLight::Spot ? "spot" : "point") << "lm" << m_mapLights[i].intensity
                              << "range" << m_mapLights[i].range << "fadeStart" << m_mapLights[i].fadeStart << "dist" << (m_mapLights[i].world.column(3).toVector3D() - eye).length()
                              << "colour" << m_mapLights[i].color;
    for (int i : std::as_const(want)) {
        const dir::StageLight &l = m_mapLights[i];
        QPointer<QQuick3DNode> &node = m_mapLightNodes[i];
        if (!node && lightRoot) {
            QQuick3DAbstractLight *light = l.kind == dir::StageLight::Spot ? static_cast<QQuick3DAbstractLight *>(new QQuick3DSpotLight)
                                                                          : static_cast<QQuick3DAbstractLight *>(new QQuick3DPointLight);
            light->setParent(lightRoot);
            light->setParentItem(lightRoot);
            node = light;
        }
        if (!node) continue;
        node->setPosition(l.world.column(3).toVector3D());
        QMatrix3x3 r;
        for (int c = 0; c < 3; ++c) {
            const QVector3D axis = l.world.column(c).toVector3D().normalized();
            r(0, c) = axis.x(); r(1, c) = axis.y(); r(2, c) = axis.z();
        }
        // RE Engine lights shine along their local -Y (most map spots have +Y up, pointing down); Qt's along -Z
        node->setRotation((QQuaternion::fromRotationMatrix(r) * QQuaternion::fromAxisAndAngle(1, 0, 0, -90)).normalized());
        // lumens -> Qt brightness: ~candela / 25 at a metre (a 150 cd lamp is bright up close and gone in a few
        // metres; a chapter's 8 cd fill lights the square evenly and dimly, like the game's night)
        node->setProperty("brightness", l.intensity / 12.566 / 25.0 * w.mapLights);
        const QVector3D c = l.blackbody ? fm::kelvinColor(l.temperature) : l.color;
        node->setProperty("color", QColor::fromRgbF(std::clamp(c.x(), 0.f, 1.f), std::clamp(c.y(), 0.f, 1.f), std::clamp(c.z(), 0.f, 1.f)));
        node->setProperty("castsShadow", false);
        // Qt's fade is 1 / (constant + linear * d / 100 + quadratic * d^2 / 1e4) (it assumes centimetres; we are in
        // metres): a physical lamp falls as 1 / (1 + d^2); a fill stays even to fadeStart and is at 10 % at its range
        const double reach = std::max(0.5, double(l.range));
        node->setProperty("constantFade", 1.0);
        node->setProperty("linearFade", 0.0);
        node->setProperty("quadraticFade", l.fadeStart > 0 ? 9.0e4 / (reach * reach) : 1.0e4);
        if (l.kind == dir::StageLight::Spot) {
            const double outer = std::clamp(double(std::max(l.cone, l.spread)), 5.0, 175.0), inner = std::clamp(double(std::min(l.cone, l.spread > 0 ? l.spread : l.cone)), 1.0, outer);
            node->setProperty("coneAngle", outer);
            node->setProperty("innerConeAngle", std::min(inner * 0.85, outer - 1));
        }
    }
}

// ------------------------------------------------------------------ navigation
// ------------------------------------------------------------------ Blender-style viewing
void StageScene::setOrtho(bool on)
{
    if (on == m_ortho) return;
    m_ortho = on;
    m_autoOrtho = false;
    emit viewChanged();
}

void StageScene::setShading(const QString &s)
{
    if (s == m_shading) return;
    m_shading = s;
    m_mapLightsKey.clear();
    emit viewChanged();
    if (m_rt) sync(*m_rt);
}

// Solid shading: every material light grey (the export view shares them, so never while a movie renders)
void StageScene::applyClay(bool on)
{
    m_clay = on;
    for (const auto &a : std::as_const(m_actors)) if (a) a->setClay(on);
    if (m_stage) m_stage->setClay(on);
}

// turning the view by hand leaves an axis view (and the orthographic it switched on by itself)
void StageScene::leaveAxisView()
{
    if (m_viewAnim) m_viewAnim->stop();
    if (m_viewName == QLatin1String("User") && !m_autoOrtho) return;
    m_viewName = QStringLiteral("User");
    if (m_autoOrtho) { m_ortho = false; m_autoOrtho = false; }
    emit viewChanged();
}

void StageScene::animateView(const QVector3D &pos, const QQuaternion &rot, const QString &name, bool ortho)
{
    if (!m_rt) return;
    // an axis view is the Work Camera's (looking through a scene camera, it leaves that camera where it is)
    if (!m_rt->workView()) m_rt->send(QStringLiteral("cam_work"), {{QStringLiteral("value"), true}, {QStringLiteral("from_view"), true}});
    const ViewFrame &v = m_rt->view();
    const QVector3D p0 = v.pos;
    const QQuaternion r0 = v.rot;
    if (!m_viewAnim) {
        m_viewAnim = new QVariantAnimation(this);
        m_viewAnim->setDuration(220);
        m_viewAnim->setEasingCurve(QEasingCurve::OutCubic);
        m_viewAnim->setStartValue(0.0);
        m_viewAnim->setEndValue(1.0);
    }
    m_viewAnim->stop();
    m_viewAnim->disconnect(this);
    connect(m_viewAnim, &QVariantAnimation::valueChanged, this, [this, p0, r0, pos, rot](const QVariant &k) {
        const float s = k.toFloat();
        if (m_rt) m_rt->navigate(p0 + (pos - p0) * s, QQuaternion::slerp(r0, rot, s).normalized());
    });
    connect(m_viewAnim, &QVariantAnimation::finished, this, [this, pos, rot, name, ortho]() {
        if (m_rt) m_rt->navigate(pos, rot);
        m_viewName = name;
        if (ortho && !m_ortho) { m_ortho = true; m_autoOrtho = true; }
        emit viewChanged();
    });
    m_viewAnim->start();
}

void StageScene::viewAxis(const QString &axis)
{
    if (!m_rt) return;
    // the view's right, up and back (it looks along -back); characters face +Z, so Front looks at their faces
    struct Axis { const char *key, *name; QVector3D back, right, up; };
    static const Axis axes[] = {
        {"front", "Front", {0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {"back", "Back", {0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
        {"right", "Right", {1, 0, 0}, {0, 0, -1}, {0, 1, 0}}, {"left", "Left", {-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {"top", "Top", {0, 1, 0}, {1, 0, 0}, {0, 0, -1}},     {"bottom", "Bottom", {0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
    };
    for (const Axis &a : axes) {
        if (axis != QLatin1String(a.key)) continue;
        const float dist = std::max(1.0f, (m_rt->view().pos - m_pivot).length());
        animateView(m_pivot + a.back * dist, QQuaternion::fromAxes(a.right, a.up, a.back).normalized(), QString::fromLatin1(a.name), true);
        return;
    }
}

void StageScene::orbitStep(double yawDeg, double pitchDeg)
{
    if (!m_rt) return;
    const ViewFrame &v = m_rt->view();
    const QQuaternion turn = QQuaternion::fromAxisAndAngle(0, 1, 0, float(yawDeg)) * QQuaternion::fromAxisAndAngle(fm::right(v.rot), float(pitchDeg));
    const QVector3D pos = m_pivot + turn.rotatedVector(v.pos - m_pivot);
    leaveAxisView();
    animateView(pos, (turn * v.rot).normalized(), QStringLiteral("User"), false);
}

void StageScene::flipView()
{
    if (!m_rt) return;
    static const QHash<QString, QString> other{{QStringLiteral("Front"), QStringLiteral("back")}, {QStringLiteral("Back"), QStringLiteral("front")},
                                              {QStringLiteral("Right"), QStringLiteral("left")}, {QStringLiteral("Left"), QStringLiteral("right")},
                                              {QStringLiteral("Top"), QStringLiteral("bottom")}, {QStringLiteral("Bottom"), QStringLiteral("top")}};
    if (other.contains(m_viewName)) { viewAxis(other.value(m_viewName)); return; }
    orbitStep(180, 0);
}

void StageScene::frameAll()
{
    if (!m_rt || m_actors.isEmpty()) { frameSelection(); return; }
    QVector3D lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f);
    for (auto it = m_actors.cbegin(); it != m_actors.cend(); ++it) {
        const QVector3D c = actorCenter(double(it.key()));
        const float r = float(actorRadius(double(it.key())));
        lo = QVector3D(std::min(lo.x(), c.x() - r), std::min(lo.y(), c.y() - r), std::min(lo.z(), c.z() - r));
        hi = QVector3D(std::max(hi.x(), c.x() + r), std::max(hi.y(), c.y() + r), std::max(hi.z(), c.z() + r));
    }
    const QVector3D c = (lo + hi) * 0.5f;
    const float r = std::max(0.8f, (hi - lo).length() * 0.5f);
    m_pivot = c;
    const ViewFrame &v = m_rt->view();
    const double vHalf = std::atan(std::tan(std::max(10.0, v.fov) * 0.5 * fm::kPi / 180.0) / (16.0 / 9.0));
    const QVector3D pos = c - fm::forward(v.rot) * (r / float(std::sin(vHalf)) * 1.1f);
    animateView(pos, v.rot, m_viewName, false);
}

void StageScene::look(double dx, double dy)
{
    if (!m_rt) return;
    leaveAxisView();
    const ViewFrame &v = m_rt->view();
    const QVector3D f = fm::forward(v.rot);
    double yaw = std::atan2(-f.x(), -f.z()) * 180 / fm::kPi, pitch = std::asin(std::clamp(double(f.y()), -1.0, 1.0)) * 180 / fm::kPi;
    yaw -= dx * 0.25;
    pitch = std::clamp(pitch - dy * 0.25, -89.0, 89.0);
    const QQuaternion rot = QQuaternion::fromAxisAndAngle(0, 1, 0, float(yaw)) * QQuaternion::fromAxisAndAngle(1, 0, 0, float(pitch));
    m_rt->navigate(v.pos, rot.normalized());
}

void StageScene::pan(double dx, double dy)
{
    if (!m_rt) return;
    const ViewFrame &v = m_rt->view();
    const float k = 0.004f * std::max(1.f, (v.pos - m_pivot).length() * 0.35f);
    m_rt->navigate(v.pos - fm::right(v.rot) * float(dx) * k + fm::up(v.rot) * float(dy) * k, v.rot);
}

void StageScene::dolly(double amount)
{
    if (!m_rt) return;
    const ViewFrame &v = m_rt->view();
    const float dist = std::max(0.5f, (v.pos - m_pivot).length());
    m_rt->navigate(v.pos + fm::forward(v.rot) * float(amount) * dist * 0.1f, v.rot);
}

void StageScene::orbit(double dx, double dy)
{
    if (!m_rt) return;
    leaveAxisView();
    const ViewFrame &v = m_rt->view();
    const QVector3D off = v.pos - m_pivot;
    const QQuaternion yaw = QQuaternion::fromAxisAndAngle(0, 1, 0, float(-dx * 0.3));
    const QQuaternion pitch = QQuaternion::fromAxisAndAngle(fm::right(v.rot), float(-dy * 0.3));
    QVector3D noff = (yaw * pitch).rotatedVector(off);
    if (std::abs(QVector3D::dotProduct(noff.normalized(), QVector3D(0, 1, 0))) > 0.985f) noff = yaw.rotatedVector(off);
    const QVector3D pos = m_pivot + noff;
    m_rt->navigate(pos, fm::lookRotation(pos, m_pivot));
}

void StageScene::fly(double forward, double right, double up, double seconds, bool fast)
{
    if (!m_rt) return;
    const ViewFrame &v = m_rt->view();
    const float speed = float((fast ? 6.0 : 2.0) * seconds);
    const QVector3D d = fm::forward(v.rot) * float(forward) + fm::right(v.rot) * float(right) + QVector3D(0, float(up), 0);
    m_rt->navigate(v.pos + d * speed, v.rot);
    m_pivot += d * speed;
}

void StageScene::drive(double forward, double right, bool fast)
{
    if (m_rt) m_rt->setDriveInput(float(forward), float(right), fast);
}

void StageScene::frameSelection()
{
    if (!m_rt) return;
    const ViewFrame &v = m_rt->view();
    QVector3D c(0, 1, 0);
    float r = 1.2f;
    bool any = false;
    for (const ActorFrame &f : m_rt->frames()) {
        if (!f.selected) continue;
        c = actorCenter(double(f.id));
        r = std::max(0.6f, float(actorRadius(double(f.id))));
        any = true;
        break;
    }
    if (!any && !m_actors.isEmpty()) {
        const auto first = m_actors.begin();
        c = actorCenter(double(first.key()));
        r = std::max(0.6f, float(actorRadius(double(first.key()))));
    }
    m_pivot = c;
    // the fov is horizontal: fit the bounding sphere into the narrower vertical one
    const double w = m_view ? m_view->property("width").toDouble() : 16, h = m_view ? m_view->property("height").toDouble() : 9;
    const double aspect = h > 1 ? w / h : 16.0 / 9.0;
    const double vHalf = std::atan(std::tan(std::max(10.0, v.fov) * 0.5 * fm::kPi / 180.0) / std::max(1.0, aspect));
    const float dist = r / float(std::sin(vHalf)) * 1.1f;
    const QVector3D pos = c - fm::forward(v.rot) * dist;
    m_rt->navigate(pos, fm::lookRotation(pos, c));
}

QString StageScene::stageObjectAt(const QVector3D &origin, const QVector3D &dir) const
{
    float t = 0;
    QString name;
    if (m_stage && m_stage->rayHit(origin, dir, &t, &name)) return name;
    return {};
}

QVariantList StageScene::groundHit(const QVector3D &origin, const QVector3D &dir) const
{
    float t = 0;
    QVector3D p;
    if (m_stage && m_stage->rayHit(origin, dir, &t)) p = origin + dir * t;
    else if (dir.y() < -1e-6f && origin.y() > 0) p = origin + dir * (-origin.y() / dir.y());
    else return {};
    return {double(p.x()), double(p.y()), double(p.z())};
}

void StageScene::pickAt(QObject *picked, bool add)
{
    if (!m_rt) return;
    const double id = actorOf(picked);
    if (id) m_pivot = actorCenter(id);
    m_rt->pick(qint64(id), add);
}
