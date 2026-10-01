#include "Rsz.h"
#include "Binary.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QSaveFile>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

namespace re {

namespace {
constexpr quint32 kCacheMagic = 0x325A5352;   // "RSZ2" (bump when field kinds change)

RszField::Kind kindOf(const QString &typeIn, int size, bool native, const QString &original)
{
    const QString t = typeIn.toLower();
    using K = RszField;
    if (t == QLatin1String("bool")) return K::Bool;
    if (t == QLatin1String("s8")) return K::S8;
    if (t == QLatin1String("u8")) return K::U8;
    if (t == QLatin1String("s16")) return K::S16;
    if (t == QLatin1String("u16")) return K::U16;
    if (t == QLatin1String("s32") || t == QLatin1String("int") || t == QLatin1String("enum")) return K::S32;
    if (t == QLatin1String("u32") || t == QLatin1String("uint")) return K::U32;
    if (t == QLatin1String("s64")) return K::S64;
    if (t == QLatin1String("u64")) return K::U64;
    if (t == QLatin1String("f32") || t == QLatin1String("float")) return K::F32;
    if (t == QLatin1String("f64")) return K::F64;
    if (t == QLatin1String("string")) return K::String;
    if (t == QLatin1String("resource")) return K::Resource;
    if (t == QLatin1String("runtimetype")) return K::RuntimeType;
    if (t == QLatin1String("object")) return K::Object;
    if (t == QLatin1String("userdata")) return K::UserData;
    if (t == QLatin1String("struct")) return K::Struct;
    if (t == QLatin1String("guid")) return K::Guid;
    if (t == QLatin1String("gameobjectref") || (t == QLatin1String("uri") && original.contains(QLatin1String("GameObjectRef")))) return K::GameObjectRef;
    if (t == QLatin1String("vec2") || t == QLatin1String("float2")) return K::Vec2;
    if (t == QLatin1String("vec3") || t == QLatin1String("float3")) return K::Vec3;
    if (t == QLatin1String("vec4") || t == QLatin1String("float4") || t == QLatin1String("keyframe")) return size >= 16 ? K::Vec4 : K::Raw;
    if (t == QLatin1String("quaternion")) return K::Quat;
    if (t == QLatin1String("position") && size == 24) return K::DVec3;
    if (t == QLatin1String("mat4")) return K::Mat4;
    if (t == QLatin1String("data")) {
        if (size == 4 && native) return K::MaybeObject;
        if (size == 64) return K::Mat4;
        if (size == 16 && !native) return K::Vec4;
        if (size == 1) return K::U8;
    }
    return K::Raw;
}
} // namespace

// ------------------------------------------------------------------ types
bool RszTypes::load(const QString &jsonPath, const QString &cachePath, QString *error)
{
    const QFileInfo fi(jsonPath);
    if (!fi.exists()) { if (error) *error = QStringLiteral("type list missing: %1").arg(jsonPath); return false; }
    const qint64 stamp = fi.lastModified().toMSecsSinceEpoch() ^ fi.size();
    if (!cachePath.isEmpty() && loadCache(cachePath, stamp)) return true;

    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly)) { if (error) *error = QStringLiteral("cannot read %1").arg(jsonPath); return false; }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (doc.isNull()) { if (error) *error = pe.errorString(); return false; }
    const QJsonObject root = doc.object();
    m_types.clear();
    m_byName.clear();
    m_types.reserve(root.size());
    for (auto it = root.begin(); it != root.end(); ++it) {
        bool ok = false;
        const quint32 hash = it.key().toUInt(&ok, 16);
        if (!ok) continue;
        const QJsonObject o = it.value().toObject();
        RszType t;
        t.hash = hash;
        t.name = o.value(QLatin1String("name")).toString();
        for (const QJsonValue &fv : o.value(QLatin1String("fields")).toArray()) {
            const QJsonObject fo = fv.toObject();
            RszField fd;
            fd.name = fo.value(QLatin1String("name")).toString();
            fd.size = quint16(fo.value(QLatin1String("size")).toInt(4));
            fd.align = quint8(qMax(1, fo.value(QLatin1String("align")).toInt(1)));
            fd.array = fo.value(QLatin1String("array")).toBool();
            fd.native = fo.value(QLatin1String("native")).toBool();
            fd.original = fo.value(QLatin1String("original_type")).toString();
            fd.kind = kindOf(fo.value(QLatin1String("type")).toString(), fd.size, fd.native, fd.original);
            t.fields << fd;
        }
        if (!t.name.isEmpty()) m_byName.insert(t.name, hash);
        m_types.insert(hash, std::move(t));
    }
    if (!cachePath.isEmpty()) saveCache(cachePath, stamp);
    return !m_types.isEmpty();
}

bool RszTypes::loadCache(const QString &path, qint64 stamp)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QDataStream in(&f);
    in.setVersion(QDataStream::Qt_6_5);
    quint32 magic;
    qint64 s;
    qint32 n;
    in >> magic >> s >> n;
    if (magic != kCacheMagic || s != stamp || n <= 0) return false;
    m_types.clear();
    m_byName.clear();
    m_types.reserve(n);
    for (int i = 0; i < n && in.status() == QDataStream::Ok; ++i) {
        RszType t;
        qint32 fc;
        in >> t.hash >> t.name >> fc;
        t.fields.resize(fc);
        for (RszField &fd : t.fields) {
            quint8 kind, flags;
            in >> fd.name >> kind >> fd.size >> fd.align >> flags >> fd.original;
            fd.kind = RszField::Kind(kind);
            fd.array = flags & 1;
            fd.native = flags & 2;
        }
        if (!t.name.isEmpty()) m_byName.insert(t.name, t.hash);
        m_types.insert(t.hash, std::move(t));
    }
    return in.status() == QDataStream::Ok && !m_types.isEmpty();
}

void RszTypes::saveCache(const QString &path, qint64 stamp) const
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    QDataStream out(&f);
    out.setVersion(QDataStream::Qt_6_5);
    out << kCacheMagic << stamp << qint32(m_types.size());
    for (const RszType &t : m_types) {
        out << t.hash << t.name << qint32(t.fields.size());
        for (const RszField &fd : t.fields)
            out << fd.name << quint8(fd.kind) << fd.size << fd.align << quint8((fd.array ? 1 : 0) | (fd.native ? 2 : 0)) << fd.original;
    }
    f.commit();
}

// ------------------------------------------------------------------ documents
namespace {

struct Parser {
    const RszTypes &types;
    Reader r;
    int current = 0;

    static qint64 align(qint64 p, int a) { return a > 1 && p % a ? p + (a - p % a) : p; }

    QString utf16(qint64 &pos)
    {
        pos = align(pos, 4);
        const quint32 n = r.at<quint32>(pos);
        pos += 4;
        if (n == 0 || !r.has(pos, qint64(n) * 2)) return {};
        QString s = QString::fromUtf16(reinterpret_cast<const char16_t *>(r.ptr(pos)), n);
        pos += qint64(n) * 2;
        while (s.endsWith(QChar(0))) s.chop(1);
        return s;
    }

    QVariant value(const RszField &f, qint64 &pos)
    {
        using K = RszField;
        switch (f.kind) {
        case K::String: case K::Resource: return utf16(pos);
        case K::RuntimeType: {
            pos = align(pos, 4);
            const quint32 n = r.at<quint32>(pos);
            pos += 4;
            const QString s = r.has(pos, n) ? QString::fromUtf8(reinterpret_cast<const char *>(r.ptr(pos)), n) : QString();
            pos += n;
            return s;
        }
        default: break;
        }
        pos = align(pos, f.align);
        const qint64 at = pos;
        pos += f.size;
        switch (f.kind) {
        case K::Bool: return r.at<quint8>(at) != 0;
        case K::S8: return int(r.at<qint8>(at));
        case K::U8: return uint(r.at<quint8>(at));
        case K::S16: return int(r.at<qint16>(at));
        case K::U16: return uint(r.at<quint16>(at));
        case K::S32: return r.at<qint32>(at);
        case K::U32: return r.at<quint32>(at);
        case K::S64: return r.at<qint64>(at);
        case K::U64: return r.at<quint64>(at);
        case K::F32: return r.at<float>(at);
        case K::F64: return r.at<double>(at);
        case K::Object: case K::UserData: return QVariant::fromValue(RszRef{r.at<qint32>(at)});
        case K::MaybeObject: {
            const qint32 v = r.at<qint32>(at);
            if (v > 0 && v < current) return QVariant::fromValue(RszRef{v});
            return v;
        }
        case K::Vec2: return QVector2D(r.at<float>(at), r.at<float>(at + 4));
        case K::Vec3: return QVector3D(r.at<float>(at), r.at<float>(at + 4), r.at<float>(at + 8));
        case K::DVec3: return QVector3D(float(r.at<double>(at)), float(r.at<double>(at + 8)), float(r.at<double>(at + 16)));
        case K::Vec4: return QVector4D(r.at<float>(at), r.at<float>(at + 4), r.at<float>(at + 8), r.at<float>(at + 12));
        case K::Quat: return QQuaternion(r.at<float>(at + 12), r.at<float>(at), r.at<float>(at + 4), r.at<float>(at + 8));
        case K::Mat4: {
            QMatrix4x4 m;
            for (int i = 0; i < 16; ++i) m.data()[i] = r.at<float>(at + i * 4);
            return m;
        }
        case K::Guid: case K::GameObjectRef: return r.bytesAt(at, 16).toHex();
        default: return r.bytesAt(at, f.size);
        }
    }

    qint64 fields(const RszType &t, qint64 pos, QVariantMap &out, int depth = 0)
    {
        for (const RszField &f : t.fields) {
            if (!r.ok() || pos > r.size()) break;
            if (f.array) {
                pos = align(pos, 4);
                const quint32 count = r.at<quint32>(pos);
                pos += 4;
                if (count > 1000000 || !r.has(pos, 0)) { r.fail(); break; }
                QVariantList list;
                list.reserve(int(qMin<quint32>(count, 4096)));
                if (f.kind == RszField::Struct) {
                    const RszType *st = types.byName(f.original);
                    if (st && count) {
                        pos = align(pos, f.align);
                        for (quint32 i = 0; i < count && depth < 8; ++i) {
                            QVariantMap m;
                            pos = fields(*st, pos, m, depth + 1);
                            list << m;
                        }
                    }
                } else {
                    for (quint32 i = 0; i < count; ++i) list << value(f, pos);
                }
                out.insert(f.name, list);
            } else if (f.kind == RszField::Struct) {
                const RszType *st = types.byName(f.original);
                QVariantMap m;
                if (st && depth < 8) { pos = align(pos, f.align); pos = fields(*st, pos, m, depth + 1); }
                out.insert(f.name, m);
            } else {
                out.insert(f.name, value(f, pos));
            }
        }
        return pos;
    }
};
} // namespace

bool RszDocument::parseRsz(const QByteArray &data, qint64 base, const RszTypes &types, QString *error)
{
    Reader r(data);
    r.seek(base);
    if (r.u32() != 0x005A5352) { if (error) *error = QStringLiteral("no RSZ block"); return false; }   // "RSZ\0"
    const quint32 version = r.u32();
    const int objectCount = r.i32();
    const int instanceCount = r.i32();
    int userCount = 0;
    if (version >= 4) { userCount = r.i32(); r.i32(); }
    const qint64 instanceOffset = r.i64();
    const qint64 dataOffset = r.i64();
    const qint64 userOffset = version >= 4 ? r.i64() : 0;
    if (!r.ok() || instanceCount < 0 || instanceCount > 4000000 || objectCount < 0) { if (error) *error = QStringLiteral("bad RSZ header"); return false; }
    objectTable.resize(objectCount);
    for (int i = 0; i < objectCount; ++i) objectTable[i] = r.i32();

    instances.resize(instanceCount);
    const int infoSize = version < 4 ? 16 : 8;
    for (int i = 0; i < instanceCount; ++i) {
        instances[i].type = r.at<quint32>(base + instanceOffset + qint64(i) * infoSize);
        instances[i].def = types.byHash(instances[i].type);
    }
    for (int i = 0; i < userCount; ++i) {
        const qint64 at = base + userOffset + qint64(i) * 16;
        const quint32 inst = r.at<quint32>(at);
        const qint64 strOff = r.at<qint64>(at + 8);
        if (inst < quint32(instanceCount)) instances[int(inst)].userFile = r.wstringAt(base + strOff);
    }

    Parser p{types, Reader(data)};
    qint64 pos = base + dataOffset;
    for (int i = 1; i < instanceCount; ++i) {
        RszInstance &in = instances[i];
        if (in.type == 0 || !in.userFile.isEmpty()) continue;
        if (!in.def) {
            if (error) *error = QStringLiteral("unknown type %1 (instance %2) - the type list does not match this game version").arg(in.type, 8, 16, QLatin1Char('0')).arg(i);
            return false;
        }
        p.current = i;
        pos = p.fields(*in.def, pos, in.fields);
        if (!p.r.ok()) { if (error) *error = QStringLiteral("RSZ data ends early at instance %1 (%2)").arg(i).arg(in.def->name); return false; }
    }
    return true;
}

bool RszDocument::parse(const QByteArray &data, const RszTypes &types, QString *error)
{
    Reader r(data);
    const quint32 magic = r.u32();
    qint64 rszBase = 0;
    auto strings = [&](qint64 table, int count, int stride, int strOffAt, bool is64, QStringList *out) {
        for (int i = 0; i < count; ++i) {
            const qint64 at = table + qint64(i) * stride + strOffAt;
            const qint64 off = is64 ? r.at<qint64>(at) : qint64(r.at<quint32>(at));
            *out << r.wstringAt(off);
        }
    };
    if (magic == 0x00424650) {                       // "PFB\0"
        kind = Prefab;
        const int goCount = r.i32(), resCount = r.i32(), refCount = r.i32(), userCount = r.i32();
        r.i32();
        r.i64();                                     // go ref info table
        const qint64 resTbl = r.i64(), userTbl = r.i64();
        rszBase = r.i64();
        Q_UNUSED(refCount);
        const qint64 goTbl = 56;
        strings(resTbl, resCount, 8, 0, false, &resources);
        strings(userTbl, userCount, 16, 8, true, &userFiles);
        if (!parseRsz(data, rszBase, types, error)) return false;
        QHash<int, int> byObjectId;
        for (int i = 0; i < goCount; ++i) {
            const qint64 at = goTbl + qint64(i) * 12;
            GameObject go;
            const int id = r.at<qint32>(at);
            const int parentId = r.at<qint32>(at + 4);
            const int comps = r.at<qint32>(at + 8);
            go.instance = objectTable.value(id, -1);
            go.parent = parentId;                      // resolved below
            for (int c = 1; c <= comps; ++c) go.components << objectTable.value(id + c, -1);
            byObjectId.insert(id, i);
            gameObjects << go;
        }
        for (GameObject &go : gameObjects) go.parent = go.parent >= 0 ? byObjectId.value(go.parent, -1) : -1;
        return true;
    }
    if (magic == 0x00525355) {                       // "USR\0"
        kind = User;
        const int resCount = r.i32(), userCount = r.i32();
        r.i32();
        const qint64 resTbl = r.i64(), userTbl = r.i64();
        rszBase = r.i64();
        strings(resTbl, resCount, 8, 0, false, &resources);
        strings(userTbl, userCount, 16, 8, true, &userFiles);
        return parseRsz(data, rszBase, types, error);
    }
    if (magic == 0x004E4353) {                       // "SCN\0"
        kind = Scene;
        const int goCount = r.i32(), resCount = r.i32(), folderCount = r.i32(), prefabCount = r.i32(), userCount = r.i32();
        const qint64 folderTbl = r.i64(), resTbl = r.i64(), prefabTbl = r.i64(), userTbl = r.i64();
        rszBase = r.i64();
        strings(resTbl, resCount, 8, 0, false, &resources);
        strings(prefabTbl, prefabCount, 8, 0, false, &prefabs);
        strings(userTbl, userCount, 16, 8, true, &userFiles);
        if (!parseRsz(data, rszBase, types, error)) return false;
        const qint64 goTbl = 64;
        QHash<int, int> goById, folderById;
        for (int i = 0; i < folderCount; ++i) {
            Folder fo;
            const int id = r.at<qint32>(folderTbl + qint64(i) * 8);
            fo.instance = objectTable.value(id, -1);
            fo.parent = r.at<qint32>(folderTbl + qint64(i) * 8 + 4);
            folderById.insert(id, i);
            folders << fo;
        }
        for (int i = 0; i < goCount; ++i) {
            const qint64 at = goTbl + qint64(i) * 32;
            GameObject go;
            go.guid = r.bytesAt(at, 16).toHex();
            const int id = r.at<qint32>(at + 16);
            const int parentId = r.at<qint32>(at + 20);
            const int comps = r.at<quint16>(at + 24);
            go.prefab = r.at<qint32>(at + 28);
            go.instance = objectTable.value(id, -1);
            go.parent = parentId;
            for (int c = 1; c <= comps; ++c) go.components << objectTable.value(id + c, -1);
            goById.insert(id, i);
            gameObjects << go;
        }
        for (GameObject &go : gameObjects) go.parent = go.parent >= 0 ? goById.value(go.parent, -1) : -1;
        for (Folder &fo : folders) fo.parent = fo.parent >= 0 ? folderById.value(fo.parent, -1) : -1;
        return true;
    }
    if (error) *error = QStringLiteral("not an RSZ file");
    return false;
}

QVariant RszDocument::field(int i, const QString &name) const
{
    const RszInstance *in = instance(i);
    return in ? in->fields.value(name) : QVariant();
}

int RszDocument::component(int go, const QString &typeName) const
{
    if (go < 0 || go >= gameObjects.size()) return -1;
    for (int c : gameObjects[go].components) {
        const RszInstance *in = instance(c);
        if (in && in->def && in->def->name == typeName) return c;
    }
    return -1;
}

QString RszDocument::gameObjectName(int go) const
{
    if (go < 0 || go >= gameObjects.size()) return {};
    return field(gameObjects[go].instance, QStringLiteral("Name")).toString();
}

} // namespace re
