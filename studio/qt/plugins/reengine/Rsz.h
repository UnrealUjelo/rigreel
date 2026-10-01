// RSZ: RE Engine's serialized object format, the body of .pfb (prefabs), .scn (scenes) and .user files.
// Instances are laid out by type templates (REasy's rsz<game>.json, 160k types for RE4); RszTypes loads them
// once and keeps a compact binary copy next to the cache so later starts are fast.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

namespace re {

struct RszField {
    enum Kind : quint8 { Raw, Bool, S8, U8, S16, U16, S32, U32, S64, U64, F32, F64, String, Resource, RuntimeType,
                         Object, UserData, MaybeObject, Vec2, Vec3, Vec4, Quat, Mat4, Guid, GameObjectRef, Struct,
                         DVec3 };              // via.Position: three doubles (world offsets of linked scenes)
    QString name;
    Kind kind = Raw;
    quint16 size = 4;
    quint8 align = 1;
    bool array = false;
    bool native = false;
    QString original;
};

struct RszType {
    quint32 hash = 0;
    QString name;
    QVector<RszField> fields;
};

class RszTypes {
public:
    bool load(const QString &jsonPath, const QString &cachePath, QString *error);
    const RszType *byHash(quint32 h) const { auto it = m_types.constFind(h); return it == m_types.cend() ? nullptr : &*it; }
    const RszType *byName(const QString &n) const { auto it = m_byName.constFind(n); return it == m_byName.cend() ? nullptr : byHash(*it); }
    int count() const { return int(m_types.size()); }

private:
    bool loadCache(const QString &path, qint64 jsonStamp);
    void saveCache(const QString &path, qint64 jsonStamp) const;
    QHash<quint32, RszType> m_types;
    QHash<QString, quint32> m_byName;
};

// An object reference inside field values.
struct RszRef {
    int index = -1;
    bool operator==(const RszRef &o) const { return index == o.index; }
};

struct RszInstance {
    quint32 type = 0;
    const RszType *def = nullptr;
    QVariantMap fields;                       // name -> value (arrays = QVariantList, refs = RszRef, structs = QVariantMap)
    QString userFile;                         // instance stored in another .user file
    QString typeName() const { return def ? def->name : QString(); }
};

class RszDocument {
public:
    enum Kind { Unknown, Scene, Prefab, User };
    struct GameObject {
        int instance = -1;                    // the via.GameObject instance
        int parent = -1;                      // index into gameObjects
        QVector<int> components;              // instance indices
        int prefab = -1;                      // scn: index into prefabs
        QByteArray guid;
    };
    struct Folder { int instance = -1; int parent = -1; };

    bool parse(const QByteArray &data, const RszTypes &types, QString *error);

    Kind kind = Unknown;
    QVector<RszInstance> instances;
    QVector<int> objectTable;
    QVector<GameObject> gameObjects;
    QVector<Folder> folders;
    QStringList resources, prefabs, userFiles;

    const RszInstance *instance(int i) const { return i >= 0 && i < instances.size() ? &instances[i] : nullptr; }
    QVariant field(int instanceIndex, const QString &name) const;
    int component(int gameObject, const QString &typeName) const;         // instance index or -1
    QString gameObjectName(int gameObject) const;
    int rootInstance() const { return objectTable.isEmpty() ? -1 : objectTable.first(); }   // user files: the data object

private:
    bool parseRsz(const QByteArray &data, qint64 base, const RszTypes &types, QString *error);
};

} // namespace re

Q_DECLARE_METATYPE(re::RszRef)
