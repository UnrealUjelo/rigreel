#include "SceneActor.h"
#include "GpuTexture.h"
#include "TextureStore.h"

#include <QQmlComponent>
#include <functional>
#include <QQmlEngine>
#include <QtQuick3D/QQuick3DGeometry>
#include <QtQuick3D/private/qquick3dmaterial_p.h>
#include <QtQuick3D/private/qquick3dmodel_p.h>
#include <QtQuick3D/private/qquick3dskin_p.h>

namespace {
class MeshGeometry : public QQuick3DGeometry {
public:
    explicit MeshGeometry(const PreparedPiece &p)
    {
        using A = QQuick3DGeometry::Attribute;
        setVertexData(p.vertices);
        setIndexData(p.indices);
        setStride(p.stride);
        setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
        addAttribute(A::PositionSemantic, vtx::Pos, A::F32Type);
        addAttribute(A::NormalSemantic, vtx::Normal, A::F32Type);
        addAttribute(A::TangentSemantic, vtx::Tangent, A::F32Type);
        addAttribute(A::BinormalSemantic, vtx::Binormal, A::F32Type);
        addAttribute(A::TexCoord0Semantic, vtx::Uv0, A::F32Type);
        addAttribute(A::TexCoord1Semantic, vtx::Uv1, A::F32Type);
        if (p.skinned) {
            addAttribute(A::JointSemantic, vtx::Joints, A::I32Type);
            addAttribute(A::WeightSemantic, vtx::Weights, A::F32Type);
        }
        addAttribute(A::IndexSemantic, 0, A::U32Type);
        setBounds(p.boundsMin, p.boundsMax);
        for (const PreparedSubset &s : p.subsets) addSubset(s.indexOffset, s.indexCount, s.boundsMin, s.boundsMax);
    }
};

QUrl g_materialUrl(QStringLiteral("qrc:/qt/qml/Director/qml/scene/ReMaterial.qml"));

// ReMaterial.qml, or a sibling file of it (ReMultiply.qml)
QQmlComponent *materialComponent(QQmlEngine *engine, const QString &sibling = QString())
{
    static QHash<QPair<QQmlEngine *, QString>, QQmlComponent *> cache;
    QQmlComponent *&c = cache[qMakePair(engine, sibling)];
    if (!c) c = new QQmlComponent(engine, sibling.isEmpty() ? g_materialUrl : g_materialUrl.resolved(QUrl(sibling)), engine);
    return c;
}

QVariantMap materialProperties(const MaterialDesc &d, const std::function<QObject *(const QString &)> &gpu)
{
    return {
        {QStringLiteral("baseProvider"), QVariant::fromValue(gpu(d.baseColor))},
        {QStringLiteral("normalProvider"), QVariant::fromValue(gpu(d.normal))},
        {QStringLiteral("packedProvider"), QVariant::fromValue(gpu(d.packed))},
        {QStringLiteral("emissiveProvider"), QVariant::fromValue(gpu(d.emissive))},
        {QStringLiteral("detailProvider"), QVariant::fromValue(gpu(d.detail))},
        {QStringLiteral("maskProvider"), QVariant::fromValue(gpu(d.mask))},
        {QStringLiteral("detailSource"), TextureStore::placeholder(QStringLiteral("white"))},
        {QStringLiteral("maskSource"), TextureStore::placeholder(QStringLiteral("white"))},
        {QStringLiteral("hasDetail"), d.hasDetail ? 1.0 : 0.0},
        {QStringLiteral("hasMask"), d.hasMask ? 1.0 : 0.0},
        {QStringLiteral("shading"), double(d.shading)},
        {QStringLiteral("detailTint"), d.detailTint},
        {QStringLiteral("detailUv"), d.detailUv},
        {QStringLiteral("dirtColor"), d.dirtColor},
        {QStringLiteral("maskUvSet"), double(d.maskUvSet)},
        {QStringLiteral("atlasColumns"), double(d.atlasColumns)},
        {QStringLiteral("groundTile"), double(d.groundTile)},
        {QStringLiteral("baseSource"), TextureStore::placeholder(QStringLiteral("white"))},
        {QStringLiteral("normalSource"), TextureStore::placeholder(QStringLiteral("normal"))},
        {QStringLiteral("packedSource"), TextureStore::placeholder(QStringLiteral("packed"))},
        {QStringLiteral("emissiveSource"), TextureStore::placeholder(QStringLiteral("black"))},
        {QStringLiteral("hasBase"), d.hasBase ? 1.0 : 0.0},
        {QStringLiteral("hasNormal"), d.hasNormal ? 1.0 : 0.0},
        {QStringLiteral("hasPacked"), d.hasPacked ? 1.0 : 0.0},
        {QStringLiteral("hasEmissive"), d.hasEmissive ? 1.0 : 0.0},
        {QStringLiteral("baseAlphaMode"), double(d.baseAlpha)},
        {QStringLiteral("normalRough"), d.normalRoughness ? 1.0 : 0.0},
        {QStringLiteral("normalLayout"), double(d.normalLayout)},
        {QStringLiteral("baseUv"), d.baseUv},
        {QStringLiteral("normalUv"), d.normalUv},
        {QStringLiteral("tint"), d.tint},
        {QStringLiteral("roughnessValue"), d.roughness},
        {QStringLiteral("metalValue"), d.metalness},
        {QStringLiteral("alphaCutoff"), d.alphaCutoff},
        {QStringLiteral("alphaTest"), d.alphaTest ? 1.0 : 0.0},
        {QStringLiteral("emissiveColor"), d.emissiveColor},
        {QStringLiteral("emissiveIntensity"), d.emissiveIntensity},
        {QStringLiteral("twoSided"), d.twoSided},
        {QStringLiteral("glass"), d.transparent ? 1.0 : 0.0},
        {QStringLiteral("hair"), d.role == QLatin1String("hair") && d.alphaTest ? 1.0 : 0.0},
        {QStringLiteral("debugView"), qEnvironmentVariableIntValue("DIRECTOR_DEBUG_VIEW")},
    };
}
} // namespace

QQuick3DGeometry *scenekit::createGeometry(const PreparedPiece &piece) { return new MeshGeometry(piece); }

QQuick3DMaterial *scenekit::createMaterial(const MaterialDesc &desc, const std::function<QObject *(const QString &)> &gpu, QQmlEngine *engine,
                                           QObject *contextOwner, QString *error)
{
    const bool multiply = desc.shading == 3;
    QQmlComponent *comp = materialComponent(engine, multiply ? QStringLiteral("ReMultiply.qml") : QString());
    if (comp->isError()) { if (error) *error = comp->errorString(); return nullptr; }
    QQmlContext *ctx = contextOwner && qmlContext(contextOwner) ? qmlContext(contextOwner) : engine->rootContext();
    const QVariantMap props = multiply ? QVariantMap{{QStringLiteral("baseProvider"), QVariant::fromValue(gpu(desc.baseColor))},
                                                     {QStringLiteral("baseSource"), TextureStore::placeholder(QStringLiteral("white"))}}
                                       : materialProperties(desc, gpu);
    QObject *obj = comp->createWithInitialProperties(props, ctx);
    auto *mat = qobject_cast<QQuick3DMaterial *>(obj);
    if (!mat) { delete obj; if (error) *error = comp->errorString(); }
    return mat;
}

SceneActor::SceneActor(QQuick3DNode *parent) : QQuick3DNode(parent) {}
void SceneActor::setMaterialUrl(const QUrl &url) { g_materialUrl = url; }
SceneActor::~SceneActor() = default;

bool SceneActor::build(const PreparedModel &pm, QQmlEngine *engine, QString *error)
{
    if (!pm.model) { if (error) *error = QStringLiteral("no model"); return false; }
    m_model = pm.model;
    m_skeleton = pm.model->skeleton;
    m_boundsMin = pm.model->boundsMin;
    m_boundsMax = pm.model->boundsMax;

    // joints: one node per bone, parented like the skeleton
    m_joints.resize(m_skeleton.bones.size());
    for (int i = 0; i < m_skeleton.bones.size(); ++i) {
        const dir::Bone &b = m_skeleton.bones[i];
        auto *n = new QQuick3DNode;
        n->setObjectName(b.name);
        QQuick3DNode *parent = b.parent >= 0 && b.parent < i ? m_joints[b.parent] : this;
        n->setParent(parent);
        n->setParentItem(parent);
        n->setPosition(b.translation);
        n->setRotation(b.rotation);
        n->setScale(b.scale);
        m_joints[i] = n;
    }
    m_rest = bindPose(m_skeleton);
    m_pose = m_rest;

    QQmlComponent *comp = materialComponent(engine);
    if (comp->isError()) { if (error) *error = comp->errorString(); return false; }

    // one GPU texture per file, shared by every material of the actor
    QHash<QString, GpuTexture *> gpuTextures;
    auto gpu = [&](const QString &key) -> QObject * {
        if (key.isEmpty() || !pm.textures.contains(key)) return nullptr;
        GpuTexture *&g = gpuTextures[key];
        if (!g) {
            g = new GpuTexture(pm.textures.value(key));
            g->setParent(this);
            g->setParentItem(this);
            m_extensions << g;
        }
        return g;
    };

    for (const PreparedPiece &p : pm.pieces) {
        QQuick3DNode *parent = p.parentBone >= 0 ? m_joints.value(p.parentBone, this) : this;
        auto *model = new QQuick3DModel;
        model->setObjectName(p.name);
        model->setPickable(true);
        model->setParent(parent);
        model->setParentItem(parent);
        if (!p.skinned && p.parentBone < 0) {
            model->setPosition(p.local.column(3).toVector3D());
        }
        auto *geom = new MeshGeometry(p);
        geom->setParent(model);
        geom->setParentItem(model);
        model->setGeometry(geom);
        if (p.skinned) {
            auto *skin = new QQuick3DSkin;
            skin->setParent(model);
            skin->setParentItem(model);
            QQmlListProperty<QQuick3DNode> joints = skin->joints();
            for (int jb : p.jointBones) joints.append(&joints, m_joints.value(jb, this));
            skin->setInverseBindPoses(p.inverseBinds);
            model->setSkin(skin);
        }
        Piece piece;
        piece.model = model;
        // one material object per mesh material, shared by its subsets
        QVector<QQuick3DMaterial *> mats(p.materials.size(), nullptr);
        QQmlListProperty<QQuick3DMaterial> list = model->materials();
        for (const PreparedSubset &s : p.subsets) {
            if (!mats[s.material]) {
                if (qEnvironmentVariableIsSet("DIRECTOR_DEBUG_TEX")) {
                    const MaterialDesc &md = p.materials[s.material];
                    qInfo().noquote() << "material" << md.name << "base" << md.baseColor.section(QLatin1Char('/'), -1) << pm.textures.contains(md.baseColor)
                                      << "normal" << md.normal.section(QLatin1Char('/'), -1) << pm.textures.contains(md.normal);
                }
                QQuick3DMaterial *mat = scenekit::createMaterial(p.materials[s.material], gpu, engine, this, error);
                if (!mat) return false;
                mat->setParent(model);
                mats[s.material] = mat;
                piece.materials << mat;
            }
            list.append(&list, mats[s.material]);
        }
        m_pieces << piece;
    }
    return true;
}

void SceneActor::setPose(const Pose &local)
{
    const int n = std::min(int(local.size()), int(m_joints.size()));
    for (int i = 0; i < n; ++i) {
        QQuick3DNode *j = m_joints[i];
        const Xform &x = local[i];
        if (j->position() != x.t) j->setPosition(x.t);
        if (j->rotation() != x.r) j->setRotation(x.r);
        if (j->scale() != x.s) j->setScale(x.s);
    }
    m_pose = local;
}

QMatrix4x4 SceneActor::boneModelMatrix(int bone) const
{
    if (bone < 0 || bone >= m_skeleton.bones.size()) return {};
    QMatrix4x4 m;
    for (int b = bone; b >= 0; b = m_skeleton.bones[b].parent) {
        m = (b < m_pose.size() ? m_pose[b].matrix() : QMatrix4x4()) * m;
        if (m_skeleton.bones[b].parent >= b) break;
    }
    return m;
}

void SceneActor::setClay(bool on)
{
    for (const Piece &p : m_pieces)
        for (QObject *m : p.materials) m->setProperty("clay", on ? 1.0 : 0.0);
}

void SceneActor::setHighlight(float amount)
{
    if (qFuzzyCompare(amount + 1, m_highlight + 1)) return;
    m_highlight = amount;
    for (const Piece &p : m_pieces)
        for (QObject *m : p.materials) m->setProperty("highlight", amount);
}

void SceneActor::setPieceVisible(int piece, bool on)
{
    if (piece >= 0 && piece < m_pieces.size()) m_pieces[piece].model->setVisible(on);
}
