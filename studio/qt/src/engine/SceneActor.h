// A character or prop in the 3D view: Qt Quick 3D models per piece, one joint node per skeleton bone (skins
// follow them), RE materials. Built on the GUI thread from a PreparedModel; posed every frame with setPose().
#pragma once

#include "ModelPrep.h"
#include "Pose.h"

#include <QPointer>
#include <functional>
#include <QtQuick3D/private/qquick3dnode_p.h>

class QQmlEngine;
class QQuick3DGeometry;
class QQuick3DMaterial;
class QQuick3DModel;

// Building blocks shared by actors and stages
namespace scenekit {
QQuick3DGeometry *createGeometry(const PreparedPiece &piece);
// an RE material (ReMaterial.qml); gpu(key) hands out the texture provider of a PreparedModel texture key
QQuick3DMaterial *createMaterial(const MaterialDesc &desc, const std::function<QObject *(const QString &)> &gpu, QQmlEngine *engine,
                                 QObject *contextOwner, QString *error);
}

class SceneActor : public QQuick3DNode {
    Q_OBJECT
public:
    explicit SceneActor(QQuick3DNode *parent = nullptr);
    ~SceneActor() override;

    bool build(const PreparedModel &pm, QQmlEngine *engine, QString *error);
    static void setMaterialUrl(const QUrl &url);     // ReMaterial.qml (default: the Director QML module)
    dir::ModelPtr model() const { return m_model; }
    qint64 actorId() const { return m_actorId; }
    void setActorId(qint64 id) { m_actorId = id; }
    const dir::Skeleton &skeleton() const { return m_skeleton; }
    int boneCount() const { return int(m_skeleton.bones.size()); }

    void setPose(const Pose &local);
    const Pose &pose() const { return m_pose; }
    const Pose &restPose() const { return m_rest; }
    QQuick3DNode *joint(int bone) const { return bone >= 0 && bone < m_joints.size() ? m_joints[bone] : nullptr; }
    QMatrix4x4 boneModelMatrix(int bone) const;     // relative to the actor
    QVector3D boundsMin() const { return m_boundsMin; }
    QVector3D boundsMax() const { return m_boundsMax; }

    void setHighlight(float amount);                // selection glow
    void setClay(bool on);                          // Solid shading
    void setPieceVisible(int piece, bool on);
    int pieceCount() const { return int(m_pieces.size()); }
    // render extensions (GPU textures) the View3D must run: add them to its "extensions" list
    QList<QQuick3DObject *> extensions() const { return m_extensions; }

private:
    struct Piece { QQuick3DModel *model = nullptr; QList<QObject *> materials; };
    dir::ModelPtr m_model;
    dir::Skeleton m_skeleton;
    QVector<QQuick3DNode *> m_joints;
    QVector<Piece> m_pieces;
    Pose m_pose, m_rest;
    QVector3D m_boundsMin, m_boundsMax;
    float m_highlight = 0;
    qint64 m_actorId = 0;
    QList<QQuick3DObject *> m_extensions;
};
