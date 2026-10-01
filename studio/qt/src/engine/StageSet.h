// A map in the 3D view. Every unique mesh of the stage is built once (geometry + RE materials, textures shared
// across the whole stage) and drawn at all its placements through Qt Quick 3D instancing. It also answers
// "where is the ground below this point" for placing characters.
#pragma once

#include "ModelPrep.h"

#include <QtQuick3D/private/qquick3dnode_p.h>

class QQmlEngine;

struct PreparedStage {
    dir::StagePtr stage;
    QHash<QString, PreparedModel> models;                  // model id -> prepared model (textures: see below)
    QHash<QString, dir::TexturePtr> textures;              // every texture of the stage, once (cache file -> texture)
    QStringList warnings;
};

class StageSet : public QQuick3DNode {
    Q_OBJECT
public:
    explicit StageSet(QQuick3DNode *parent = nullptr);
    ~StageSet() override;

    bool build(const PreparedStage &ps, QQmlEngine *engine, QString *error);
    void setClay(bool on);                                 // Solid shading
    QString stageId() const { return m_id; }
    int placements() const { return m_placements; }
    // render extensions (GPU textures) the View3D must run
    QList<QQuick3DObject *> extensions() const { return m_extensions; }
    // the first surface below `from` (world space); false when there is none within `maxDrop` metres
    bool groundBelow(const QVector3D &from, float maxDrop, float *y) const;
    // heights of every surface crossing the vertical line through (x, z)
    QVector<float> surfacesAt(float x, float z) const;
    // the nearest surface along a ray (world space): distance in units of `dir`
    bool rayHit(const QVector3D &origin, const QVector3D &dir, float *t, QString *name = nullptr) const;   // name: the mesh hit

private:
    struct Shape {                                          // one mesh piece for ray casts (shares the vertex blob)
        QByteArray vertices, indices;
        int stride = 0;
        QVector3D lo, hi;
        QString name;                                       // the game's mesh (and material) for the status bar
    };
    struct Placement {
        int shape = -1;
        QMatrix4x4 world, inverse;
        QVector3D lo, hi;                                   // world bounds
    };
    QString m_id;
    int m_placements = 0;
    QList<QQuick3DObject *> m_extensions;
    QList<QObject *> m_materials;
    QVector<Shape> m_shapes;
    QVector<Placement> m_hits;
};
