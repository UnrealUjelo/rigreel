// Game-neutral assets. A game plugin turns its own files into these; the Studio renders and animates them
// without knowing which game they came from. Only Qt types cross the plugin boundary (same compiler, shared
// Qt runtime), assets are handed out as QSharedPointer and are immutable once loaded.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

namespace dir {

// ---------------------------------------------------------------- textures
struct TextureAsset {
    enum Format { Unknown, RGBA8, BGRA8, R8, RG8, RGBA16F, BC1, BC2, BC3, BC4, BC5, BC6H, BC7 };
    QString path;
    Format format = Unknown;
    bool srgb = false;
    bool signedNorm = false;      // BC4 / BC5 SNORM
    int width = 0, height = 0;
    int arraySize = 1;            // cube maps: 6
    bool cube = false;
    // a texture array handed out as one 2D atlas: layer i sits in cell (i % gridColumns, i / gridColumns)
    int gridColumns = 1, gridRows = 1;
    QVector<QByteArray> mips;     // level 0 first, tightly packed (block rows for BCn), image 0 only
    bool isBlockCompressed() const { return format >= BC1; }
    int blockBytes() const { return (format == BC1 || format == BC4) ? 8 : 16; }
};
using TexturePtr = QSharedPointer<const TextureAsset>;

// ---------------------------------------------------------------- materials
// Texture slots are normalized by the plugin so the renderer can shade any game the same way.
namespace slot {
inline const QString BaseColor = QStringLiteral("baseColor");     // rgb albedo, alpha meaning in MaterialAsset::baseAlpha
inline const QString Normal = QStringLiteral("normal");           // tangent-space normal, alpha meaning in normalAlpha
inline const QString Packed = QStringLiteral("packed");           // several masks in one texture, see packedLayout
inline const QString Emissive = QStringLiteral("emissive");
inline const QString Detail = QStringLiteral("detail");           // a second surface layer blended over the base ("layered")
inline const QString Mask = QStringLiteral("mask");               // layered: r detail, b dirt, a occlusion; terrain: layer index
inline const QString Roughness = QStringLiteral("roughness");
inline const QString Occlusion = QStringLiteral("occlusion");
}

struct MaterialAsset {
    QString name;
    QString shader;                           // source shader / template name (informational)
    QHash<QString, QString> textures;         // normalized slot -> texture path (as the source knows it)
    QHash<QString, QString> rawTextures;      // original slot name -> path (for the Element Viewer)
    QHash<QString, QVector4D> params;         // original parameter name -> value
    QVector4D baseColor{1, 1, 1, 1};                  // sRGB-encoded tint (as the game stores it); w = opacity
    QVector3D emissiveColor{0, 0, 0};                 // sRGB-encoded
    float roughness = 0.6f, metalness = 0.0f, alphaCutoff = 0.5f, emissiveIntensity = 0;
    QString baseAlpha;                        // "metal" | "dielectric" (1 - metal) | "alpha" | ""
    QString normalAlpha;                      // "roughness" | ""
    bool normalIsRG = false;                  // only x/y stored, z reconstructed
    // "nrrc": r roughness, g normal y, b cavity, a normal x (RE world surfaces); "" = rgb normal (+ alpha per normalAlpha)
    QString normalLayout;
    QVector4D baseUv{1, 1, 0, 0}, normalUv{1, 1, 0, 0};   // texture coordinate scale (xy) and offset (zw)
    // "" plain; "layered": detail layer over the base by mask.r, dirt colour by mask.b, occlusion from mask.a;
    // "terrain": baseColor / normal are atlases of ground layers, the mask (map-wide, UV0) picks a layer per spot;
    // "multiply": darkens what is behind it (1 - baseColor alpha), no lighting
    QString shading;
    float baseIntensity = 1;                          // multiplies the base colour (linear)
    QVector4D detailTint{1, 1, 1, 1}, detailUv{1, 1, 0, 0};   // tint: sRGB-encoded, w = intensity
    QVector4D dirtColor{0, 0, 0, 0};                  // sRGB-encoded rgb, w = strength (0 = no dirt)
    int maskUvSet = 0;                                // 0 = UV0, 1 = UV1
    float groundTile = 5;                             // terrain: metres per repeat of a ground layer
    QString packedLayout;                     // "atoc": r alpha, g translucency, b occlusion, a cavity
    bool alphaTest = false, transparent = false, twoSided = false, hidden = false, castsShadow = true;
    QString role;                             // "skin" | "hair" | "eye" | "cloth" | "glass" | "standard"
};

// ---------------------------------------------------------------- skeletons
struct Bone {
    QString name;
    quint32 hash = 0;                         // game-specific name hash (animation tracks bind through it)
    int parent = -1;
    int symmetry = -1;                        // mirrored partner (L <-> R), -1 = none / self
    QVector3D translation;                    // bind pose, local to parent
    QQuaternion rotation;
    QVector3D scale{1, 1, 1};
    QMatrix4x4 inverseBind;                   // model space -> bone space
};

struct Skeleton {
    QVector<Bone> bones;
    int find(const QString &name) const {
        for (int i = 0; i < bones.size(); ++i) if (bones[i].name.compare(name, Qt::CaseInsensitive) == 0) return i;
        return -1;
    }
    int findHash(quint32 h) const {
        for (int i = 0; i < bones.size(); ++i) if (bones[i].hash == h) return i;
        return -1;
    }
};

// ---------------------------------------------------------------- meshes
struct MeshPart {
    int material = 0;                         // index into MeshAsset::materialNames
    int indexOffset = 0, indexCount = 0;      // into indices
    int group = 0;                            // game "parts" id (clothing piece), for part toggles
    QVector3D boundsMin, boundsMax;
};

struct MeshAsset {
    QString path;
    QVector<QVector3D> positions;
    QVector<QVector3D> normals;
    QVector<QVector4D> tangents;              // w = handedness
    QVector<QVector2D> uv0, uv1;
    QVector<quint32> colors;                  // RGBA8, optional
    int influences = 0;                       // per vertex, 0 = rigid
    QVector<quint16> joints;                  // vertexCount * influences, indices into skinBones
    QVector<float> weights;                   // vertexCount * influences, sum ~ 1
    QVector<quint32> indices;
    QVector<MeshPart> parts;
    QStringList materialNames;
    QVector<int> skinBones;                   // skin index -> skeleton bone index (mesh's own skeleton)
    Skeleton skeleton;                        // the bones this mesh was bound to
    QVector3D boundsMin, boundsMax;
    int vertexCount() const { return int(positions.size()); }
};
using MeshPtr = QSharedPointer<const MeshAsset>;

// ---------------------------------------------------------------- models (characters, props)
struct ModelPiece {
    MeshPtr mesh;
    QVector<MaterialAsset> materials;         // same order as mesh->materialNames
    QVector<int> hiddenGroups;                // MeshPart::group values switched off by the look
    QString parentBone;                       // rigid attachment (props on a hand); empty = skinned / root
    QMatrix4x4 local;                         // placement relative to the model root (or parent bone)
    QString name;
};

struct ModelAsset {
    QString id, name;
    Skeleton skeleton;                        // merged skeleton of all skinned pieces
    QVector<ModelPiece> pieces;
    QVector3D boundsMin, boundsMax;
    QVariantMap info;                         // source details (prefab paths ...) for the Element Viewer
};
using ModelPtr = QSharedPointer<const ModelAsset>;

// ---------------------------------------------------------------- animation
template <typename T> struct Curve {
    QVector<float> times;                     // frames
    QVector<T> values;
    bool isEmpty() const { return times.isEmpty(); }
};

struct BoneTrack {
    quint32 boneHash = 0;
    QString boneName;                         // when the source knows it
    Curve<QVector3D> translation;
    Curve<QQuaternion> rotation;
    Curve<QVector3D> scale;
};

struct AnimationClip {
    QString name;
    int id = -1;                              // game motion id
    float frames = 0;                         // length in frames
    float fps = 60;
    QVector<BoneTrack> tracks;
    QHash<quint32, QString> boneNames;        // hash -> name, when the file carries a bone list
    QHash<quint32, QPair<QVector3D, QQuaternion>> restPose;  // hash -> local rest pose from the file (first frame of the list's first clip)
    QHash<quint32, quint32> restParent;       // hash -> parent hash in the file's bone list, 0 = a top bone (facial lists start at Spine_2)
};

struct AnimationSet {
    QString path, name;
    QVector<AnimationClip> clips;
};
using AnimationSetPtr = QSharedPointer<const AnimationSet>;

// ---------------------------------------------------------------- stages (maps)
struct StageInstance {
    QString name;
    QString model;                            // id for IGameSource::loadModel
    QMatrix4x4 world;
};

struct StageLight {
    enum Kind { Point, Spot, Directional } kind = Point;
    QString name;
    QMatrix4x4 world;                         // RE Engine: a spot shines along its local -Y
    QVector3D color{1, 1, 1};
    float intensity = 1;                      // lumens
    float range = 10;                         // metres it reaches
    float cone = 45, spread = 0;              // spot: full angles in degrees (spread: the soft outer edge)
    float temperature = 6500;                 // kelvin, used when blackbody
    float fadeStart = 0;                      // > 0: even light out to here, then fading to `range` (a fill light);
                                              // 0: physical, falling with the square of the distance
    bool blackbody = false, shadows = false;
    QString variant;                          // "" = always on; else one lighting of the place (RE4: "chp1_3")
};

struct StageAsset {
    QString id, name;
    QVector<StageInstance> instances;
    QVector<StageLight> lights;
    QStringList lightVariants;                // the lightings the place has (a film picks one)
    QVector3D boundsMin, boundsMax;
};
using StagePtr = QSharedPointer<const StageAsset>;

// ---------------------------------------------------------------- catalog
enum class AssetKind { Character, Prop, Animation, Stage, Sound, Other };

struct CatalogEntry {
    QString id;                               // stable within the game (a path or a look id)
    AssetKind kind = AssetKind::Other;
    QString name;                             // display name
    QString group;                            // category shown in the Asset Browser
    QString path;                             // main file
    QStringList tags;
    QVariantMap extra;
};

inline QString kindName(AssetKind k)
{
    switch (k) {
    case AssetKind::Character: return QStringLiteral("character");
    case AssetKind::Prop: return QStringLiteral("prop");
    case AssetKind::Animation: return QStringLiteral("animation");
    case AssetKind::Stage: return QStringLiteral("stage");
    case AssetKind::Sound: return QStringLiteral("sound");
    default: return QStringLiteral("other");
    }
}

} // namespace dir
