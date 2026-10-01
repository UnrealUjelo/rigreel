// Turns a neutral model into GPU-ready pieces on a worker thread: interleaved vertex buffers (4 strongest bone
// weights, Qt Quick 3D's limit), index buffers per visible material, and materials with cached texture files.
#pragma once

#include <director/GamePlugin.h>
#include <QMutex>
#include <QUrl>

struct MaterialDesc {
    QString name, role;
    QString baseColor, normal, packed, emissive, detail, mask;   // keys into PreparedModel::textures
    bool hasBase = false, hasNormal = false, hasPacked = false, hasEmissive = false, hasDetail = false, hasMask = false;
    int shading = 0;                   // 0 plain, 1 layered (detail + dirt + occlusion by mask), 2 terrain (layer atlas by mask), 3 multiply
    QVector4D detailTint{1, 1, 1, 1}, detailUv{1, 1, 0, 0}, dirtColor;   // linear colours
    int maskUvSet = 0, atlasColumns = 1;
    float groundTile = 5;
    int baseAlpha = 0;                 // 0 none, 1 dielectric (metal = 1 - a), 2 metal, 3 alpha
    bool normalRoughness = false;
    int normalLayout = 0;              // 0 rgb normal (+ alpha roughness), 1 NRRC (r roughness, g y, b cavity, a x)
    QVector4D baseUv{1, 1, 0, 0}, normalUv{1, 1, 0, 0};
    QVector4D tint{1, 1, 1, 1};
    QVector3D emissiveColor;
    float emissiveIntensity = 0, roughness = 0.6f, metalness = 0, alphaCutoff = 0.5f;
    bool alphaTest = false, transparent = false, twoSided = false, castsShadow = true;
};

struct PreparedSubset {
    int indexOffset = 0, indexCount = 0, material = 0;
    QVector3D boundsMin, boundsMax;
};

struct PreparedPiece {
    QString name;
    QByteArray vertices, indices;
    int stride = 0;
    bool skinned = false;
    QVector<PreparedSubset> subsets;
    QVector<int> jointBones;           // skin joint -> model skeleton bone
    QList<QMatrix4x4> inverseBinds;
    int parentBone = -1;               // rigid attachment to a model bone
    QMatrix4x4 local;
    QVector<MaterialDesc> materials;
    QVector3D boundsMin, boundsMax;
};

struct PreparedModel {
    dir::ModelPtr model;
    QVector<PreparedPiece> pieces;
    QHash<QString, dir::TexturePtr> textures;             // cache file -> texture (with all mips)
    QStringList warnings;
};

namespace vtx {
constexpr int Pos = 0, Normal = 12, Tangent = 24, Binormal = 36, Uv0 = 48, Uv1 = 56, Joints = 64, Weights = 80;
constexpr int Rigid = 64, Skinned = 96;
}

// Textures shared by many models prepared together (a stage): each file is read once and every model points at it.
struct TextureShare {
    QMutex mutex;
    QHash<QString, dir::TexturePtr> textures;              // cache file -> texture
};

PreparedModel prepareModel(const dir::ModelPtr &model, dir::IGameSource *src, const QString &gameId, int maxTexture, TextureShare *share = nullptr);
