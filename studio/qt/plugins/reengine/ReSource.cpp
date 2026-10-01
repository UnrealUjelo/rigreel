#include "ReSource.h"
#include "Mdf.h"
#include "Mesh.h"
#include "Mot.h"
#include "PartsCast.h"
#include "Re4Cast.h"
#include "Rsz.h"
#include "Hash.h"
#include "Tex.h"
#include "Thumbnail.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <functional>

namespace re {

using namespace dir;

ReSource::ReSource(const GameInfo &info, const GameProfile &profile, const QString &dataDir)
    : m_info(info), m_profile(profile), m_dataDir(dataDir)
{
}

ReSource::~ReSource() = default;

static QString firstExisting(const QStringList &candidates)
{
    for (const QString &c : candidates)
        if (!c.isEmpty() && QFileInfo::exists(c)) return c;
    return {};
}

// Community data (name lists, RSZ type dumps) ships with REasy; the plugin looks in its own data folder first,
// then in REasy's folders the user may have installed.
static QStringList dataRoots(const QString &dataDir)
{
    QStringList roots{dataDir};
    QString env = qEnvironmentVariable("RIGREEL_REASY");
    if (env.isEmpty()) env = qEnvironmentVariable("DIRECTOR_REASY"); // legacy name
    if (!env.isEmpty()) roots << env;
    // a workspace checkout: <workspace>/tools/REasy
    QDir d(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        if (QFileInfo::exists(d.filePath(QStringLiteral("tools/REasy")))) { roots << d.filePath(QStringLiteral("tools/REasy")); break; }
        if (!d.cdUp()) break;
    }
    return roots;
}

bool ReSource::open(QString *error)
{
    if (!m_fs.open(m_info.folder, error)) return false;
    QStringList listCandidates, rszCandidates;
    for (const QString &r : dataRoots(m_dataDir)) {
        listCandidates << r + QStringLiteral("/lists/") + m_profile.listName << r + QStringLiteral("/resources/data/lists/") + m_profile.listName;
        rszCandidates << r + QStringLiteral("/rsz/") + m_profile.rszName << r + QLatin1Char('/') + m_profile.rszName
                      << r + QStringLiteral("/resources/data/dumps/") + m_profile.rszName;
    }
    const QString list = firstExisting(listCandidates);
    if (list.isEmpty()) {
        if (error) *error = QStringLiteral("the file-name list %1 was not found (install REasy's data or put it in %2/lists)").arg(m_profile.listName, m_dataDir);
        return false;
    }
    if (!m_fs.loadNames(list, error)) return false;
    detectVersions();
    m_info.note = QStringLiteral("%1 archives, %2 files").arg(m_fs.archiveCount()).arg(m_fs.names().size());
    m_rszPath = firstExisting(rszCandidates);
    return true;
}

void ReSource::detectVersions()
{
    static const QRegularExpression rx(QStringLiteral("\\.([a-z0-9_]+)\\.(\\d+)(?:\\.x64|\\.stm|\\.en)?$"), QRegularExpression::CaseInsensitiveOption);
    QHash<QString, QHash<int, int>> counts;
    QHash<QString, int> plats;
    const QStringList &names = m_fs.names();
    for (const QString &n : names) {
        if (const auto m = rx.match(n); m.hasMatch()) counts[m.captured(1).toLower()][m.captured(2).toInt()]++;
        const int a = n.indexOf(QLatin1Char('/')), b = a < 0 ? -1 : n.indexOf(QLatin1Char('/'), a + 1);
        if (b > a) plats[n.mid(a + 1, b - a - 1).toLower()]++;
    }
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        int best = 0, bestN = -1;
        for (auto v = it->cbegin(); v != it->cend(); ++v) if (v.value() > bestN) { bestN = v.value(); best = v.key(); }
        m_versions.insert(it.key(), best);
    }
    int bestN = -1;
    for (auto it = plats.cbegin(); it != plats.cend(); ++it) if (it.value() > bestN) { bestN = it.value(); m_platform = it.key(); }
}

QString ReSource::resolve(const QString &resourcePath, const QString &extIn) const
{
    QString p = normalizePath(resourcePath);
    while (p.startsWith(QLatin1Char('@'))) p.remove(0, 1);          // "@path": the engine's resource-path prefix
    if (p.isEmpty()) return {};
    const QString ext = extIn.isEmpty() ? p.section(QLatin1Char('.'), -1).toLower() : extIn.toLower();
    if (!p.startsWith(QLatin1String("natives/"), Qt::CaseInsensitive)) p = QStringLiteral("natives/%1/%2").arg(m_platform, p);
    static const QRegularExpression hasVersion(QStringLiteral("\\.\\d+$"));
    if (!hasVersion.match(p).hasMatch()) {
        if (!p.endsWith(QLatin1Char('.') + ext, Qt::CaseInsensitive)) p += QLatin1Char('.') + ext;
        const int v = m_versions.value(ext);
        if (v) p += QLatin1Char('.') + QString::number(v);
    }
    // prefer the path spelling the name list uses (display only; the hash is case-insensitive)
    const QString known = m_fs.nameOf(pathHash(p));
    return known.isEmpty() ? p : known;
}

QString ReSource::streamingPath(const QString &path) const
{
    const int a = path.indexOf(QLatin1Char('/')), b = a < 0 ? -1 : path.indexOf(QLatin1Char('/'), a + 1);
    return b < 0 ? QString() : path.left(b) + QStringLiteral("/streaming") + path.mid(b);
}

const RszTypes *ReSource::rszTypes()
{
    QMutexLocker lock(&m_rszMutex);
    if (!m_rszTried) {
        m_rszTried = true;
        if (m_rszPath.isEmpty()) { m_rszError = QStringLiteral("the type list %1 was not found").arg(m_profile.rszName); return nullptr; }
        auto t = std::make_unique<RszTypes>();
        const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/rsz/%1.bin").arg(m_profile.id);
        if (t->load(m_rszPath, cache, &m_rszError)) m_rsz = std::move(t);
    }
    return m_rsz.get();
}

bool ReSource::exists(const QString &path) { return m_fs.exists(resolve(path, {})); }
QByteArray ReSource::readFile(const QString &path) { return m_fs.read(resolve(path, {})); }
QStringList ReSource::listFiles(const QString &prefix, const QString &suffix, int limit) { return m_fs.list(prefix, suffix, limit); }

// ------------------------------------------------------------------ textures, meshes, materials
TexturePtr ReSource::loadTexture(const QString &path, QString *error)
{
    const QString full = resolve(path, QStringLiteral("tex"));
    QSharedPointer<TextureAsset> resident, streaming;
    QString err;
    QByteArray residentData, streamingData;
    if (m_fs.exists(full)) resident = parseTex(residentData = m_fs.read(full), &err);
    const QString sp = streamingPath(full);
    if (!sp.isEmpty() && m_fs.exists(sp)) streaming = parseTex(streamingData = m_fs.read(sp), &err);
    // texture arrays (terrain layers): every layer merged on its own, then laid out as one grid atlas no wider
    // than twice the size cap
    const auto first = resident ? resident : streaming;
    if (first && first->arraySize > 1 && !first->cube) {
        const int n = first->arraySize, cols = int(std::ceil(std::sqrt(double(n))));
        QVector<QSharedPointer<TextureAsset>> layers;
        for (int i = 0; i < n; ++i) {
            auto r = residentData.isEmpty() ? QSharedPointer<TextureAsset>() : parseTex(residentData, &err, i);
            auto s = streamingData.isEmpty() ? QSharedPointer<TextureAsset>() : parseTex(streamingData, &err, i);
            layers << mergeTex(r, s, std::max(16, 2 * m_maxTexture / cols));
        }
        auto atlas = atlasTex(layers, &err);
        if (!atlas) { if (error) *error = err; return {}; }
        atlas->path = full;
        return atlas;
    }
    auto t = mergeTex(resident, streaming, m_maxTexture);
    if (!t) { if (error) *error = err.isEmpty() ? QStringLiteral("texture not found: %1").arg(path) : err; return {}; }
    t->path = full;
    return t;
}

QSharedPointer<MeshAsset> ReSource::loadMesh(const QString &path, QString *error)
{
    const QString full = resolve(path, QStringLiteral("mesh"));
    {
        QMutexLocker lock(&m_cacheMutex);
        if (auto it = m_meshCache.constFind(full.toLower()); it != m_meshCache.cend()) return (*it).constCast<MeshAsset>();
    }
    const quint32 fv = full.section(QLatin1Char('.'), -1).toUInt();
    QString err;
    const QByteArray data = m_fs.read(full, &err);
    if (data.isEmpty()) { if (error) *error = err; return {}; }
    auto m = parseMesh(data, fv, {}, &err);
    if (!m) { if (error) *error = QStringLiteral("%1: %2").arg(path.section(QLatin1Char('/'), -1), err); return {}; }
    m->path = full;
    QMutexLocker lock(&m_cacheMutex);
    // a map loads hundreds of meshes once: keep the cache small (models keep the meshes they use)
    if (m_meshCache.size() > 200) m_meshCache.clear();
    m_meshCache.insert(full.toLower(), m);
    return m;
}

QVector<MaterialAsset> ReSource::loadMaterials(const QString &path, QString *error)
{
    const QString full = resolve(path, QStringLiteral("mdf2"));
    const QByteArray data = m_fs.read(full, error);
    if (data.isEmpty()) return {};
    QVector<MaterialAsset> mats = parseMdf(data, full.section(QLatin1Char('.'), -1).toInt(), error);
    return mats;
}

static QVector<MaterialAsset> orderMaterials(const MeshAsset &mesh, const QVector<MaterialAsset> &mdf)
{
    QVector<MaterialAsset> out;
    for (const QString &n : mesh.materialNames) {
        auto it = std::find_if(mdf.cbegin(), mdf.cend(), [&](const MaterialAsset &m) { return m.name.compare(n, Qt::CaseInsensitive) == 0; });
        if (it != mdf.cend()) out << *it;
        else { MaterialAsset m; m.name = n; m.hidden = n.contains(QLatin1String("shadow"), Qt::CaseInsensitive); out << m; }
    }
    return out;
}

// Merge skeletons by bone name: body, head and clothes each carry the joints they are bound to.
static void mergeSkeleton(Skeleton &into, const Skeleton &from)
{
    for (const Bone &b : from.bones) {
        if (into.findHash(b.hash) >= 0) continue;
        Bone nb = b;
        nb.parent = b.parent >= 0 ? into.findHash(from.bones[b.parent].hash) : -1;
        nb.symmetry = -1;
        into.bones << nb;
    }
    for (int i = 0; i < from.bones.size(); ++i) {           // symmetry needs every bone present
        const int me = into.findHash(from.bones[i].hash);
        const int sym = from.bones[i].symmetry;
        if (me >= 0 && sym >= 0 && sym < from.bones.size() && into.bones[me].symmetry < 0) into.bones[me].symmetry = into.findHash(from.bones[sym].hash);
    }
}

ModelPtr ReSource::modelFromMesh(const QString &meshPath, const QString &mdfPath, QString *error)
{
    auto mesh = loadMesh(meshPath, error);
    if (!mesh) return {};
    auto model = QSharedPointer<ModelAsset>::create();
    model->id = QStringLiteral("mesh:") + meshPath;
    model->name = meshPath.section(QLatin1Char('/'), -1).section(QLatin1Char('.'), 0, 0);
    ModelPiece piece;
    piece.mesh = mesh;
    piece.name = model->name;
    QString mdf = mdfPath;
    if (mdf.isEmpty()) {
        // the material file sits next to the mesh: <name>.mdf2 (characters, weapons) or <name>_Mat.mdf2 (world)
        QString base = resolve(meshPath, QStringLiteral("mesh"));
        base = base.left(base.indexOf(QStringLiteral(".mesh"), 0, Qt::CaseInsensitive));
        if (base.startsWith(QLatin1String("natives/"), Qt::CaseInsensitive))
            base = base.section(QLatin1Char('/'), 2);                     // drop natives/<platform>/
        mdf = base + QStringLiteral(".mdf2");
        for (const QString &c : {base + QStringLiteral(".mdf2"), base + QStringLiteral("_Mat.mdf2"), base + QStringLiteral("_mat.mdf2")})
            if (m_fs.exists(resolve(c, QStringLiteral("mdf2")))) { mdf = c; break; }
    }
    QString merr;
    piece.materials = orderMaterials(*mesh, loadMaterials(mdf, &merr));
    model->pieces << piece;
    mergeSkeleton(model->skeleton, mesh->skeleton);
    model->boundsMin = mesh->boundsMin;
    model->boundsMax = mesh->boundsMax;
    return model;
}

// several meshes on one skeleton (a zombie: body, face, shirt, pants)
ModelPtr ReSource::modelFromMeshes(const QString &id, const QString &name, const QStringList &meshes, QString *error)
{
    auto model = QSharedPointer<ModelAsset>::create();
    model->id = id;
    model->name = name;
    QVector3D lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f);
    for (const QString &m : meshes) {
        QString err;
        const ModelPtr one = modelFromMesh(m, {}, &err);
        if (!one) { if (error) *error = err; continue; }
        for (ModelPiece p : one->pieces) {
            // damage states (a broken head, the inside of the body, stumps) are for when it has been shot: the game
            // shows them only then
            static const QRegularExpression damageRx(QStringLiteral("inside|meat|broken|deficit|damage|internal|_joint"), QRegularExpression::CaseInsensitiveOption);
            for (MaterialAsset &mat : p.materials)
                if (damageRx.match(mat.name).hasMatch()) mat.hidden = true;
            model->pieces << p;
        }
        mergeSkeleton(model->skeleton, one->skeleton);
        lo = QVector3D(std::min(lo.x(), one->boundsMin.x()), std::min(lo.y(), one->boundsMin.y()), std::min(lo.z(), one->boundsMin.z()));
        hi = QVector3D(std::max(hi.x(), one->boundsMax.x()), std::max(hi.y(), one->boundsMax.y()), std::max(hi.z(), one->boundsMax.z()));
    }
    if (model->pieces.isEmpty()) return {};
    model->boundsMin = lo;
    model->boundsMax = hi;
    return model;
}

ModelPtr ReSource::modelFromPrefabs(const QString &id, const QString &name, const QStringList &prefabs,
                                    const QHash<QString, QVector<int>> &hiddenParts, QString *error)
{
    const RszTypes *types = rszTypes();
    if (!types) { if (error) *error = m_rszError; return {}; }
    auto model = QSharedPointer<ModelAsset>::create();
    model->id = id;
    model->name = name;
    QVector3D lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f);
    QStringList problems;
    for (const QString &pfb : prefabs) {
        const QString full = resolve(pfb, QStringLiteral("pfb"));
        RszDocument doc;
        QString err;
        if (!doc.parse(m_fs.read(full), *types, &err)) { problems << QStringLiteral("%1: %2").arg(pfb.section(QLatin1Char('/'), -1), err); continue; }
        const QVector<int> off = hiddenParts.value(pfb.toLower());
        for (int g = 0; g < doc.gameObjects.size(); ++g) {
            const int meshComp = doc.component(g, QStringLiteral("via.render.Mesh"));
            if (meshComp < 0) continue;
            // some type dumps leave the flag unnamed (RE2): absent counts as on
            if (const QVariant on = doc.field(meshComp, QStringLiteral("Enabled")); on.isValid() && !on.toBool()) continue;
            const QString meshPath = doc.field(meshComp, QStringLiteral("Mesh")).toString();
            if (meshPath.isEmpty()) continue;
            auto mesh = loadMesh(meshPath, &err);
            if (!mesh) { problems << err; continue; }
            ModelPiece piece;
            piece.mesh = mesh;
            piece.name = doc.gameObjectName(g);
            piece.materials = orderMaterials(*mesh, loadMaterials(doc.field(meshComp, QStringLiteral("Material")).toString(), &err));
            // parts: PartsEnable[i] == false hides group i; the look can switch more off
            const QVariantList enable = doc.field(meshComp, QStringLiteral("PartsEnable")).toList();
            QSet<int> groups;
            for (const MeshPart &p : mesh->parts) groups.insert(p.group);
            for (int grp : groups) if (grp < enable.size() && !enable[grp].toBool()) piece.hiddenGroups << grp;
            for (int o : off) if (!piece.hiddenGroups.contains(o)) piece.hiddenGroups << o;
            const int xf = doc.component(g, QStringLiteral("via.Transform"));
            if (xf >= 0) {
                piece.parentBone = doc.field(xf, QStringLiteral("ParentJoint")).toString();
                QMatrix4x4 m;
                m.translate(doc.field(xf, QStringLiteral("Position")).value<QVector3D>());
                m.rotate(doc.field(xf, QStringLiteral("Rotation")).value<QQuaternion>());
                m.scale(doc.field(xf, QStringLiteral("Scale")).value<QVector3D>());
                piece.local = m;
            }
            // a rigid child piece with no joint is an accessory the game hangs on a bone at run time (RE2's sheathed
            // knife, a flashlight): the prefab does not say where, and left alone it lies at the feet
            if (piece.parentBone.isEmpty() && doc.gameObjects[g].parent >= 0 && prefabs.size() > 1) {
                bool shares = false;
                for (const Bone &b : mesh->skeleton.bones) if (b.parent >= 0 && model->skeleton.findHash(b.hash) >= 0) { shares = true; break; }
                if (!shares) continue;
            }
            // skinned pieces share the character skeleton; rigid ones (no weights) keep their placement
            if (mesh->influences > 0) mergeSkeleton(model->skeleton, mesh->skeleton);
            lo = QVector3D(std::min(lo.x(), mesh->boundsMin.x()), std::min(lo.y(), mesh->boundsMin.y()), std::min(lo.z(), mesh->boundsMin.z()));
            hi = QVector3D(std::max(hi.x(), mesh->boundsMax.x()), std::max(hi.y(), mesh->boundsMax.y()), std::max(hi.z(), mesh->boundsMax.z()));
            model->pieces << piece;
        }
    }
    if (model->pieces.isEmpty()) {
        if (error) *error = problems.isEmpty() ? QStringLiteral("no meshes in %1").arg(prefabs.join(QStringLiteral(", "))) : problems.join(QStringLiteral("; "));
        return {};
    }
    model->boundsMin = lo;
    model->boundsMax = hi;
    model->info.insert(QStringLiteral("prefabs"), prefabs);
    if (!problems.isEmpty()) model->info.insert(QStringLiteral("problems"), problems);
    return model;
}

// ------------------------------------------------------------------ catalog
static QString stem(const QString &p) { return p.section(QLatin1Char('/'), -1).section(QLatin1Char('.'), 0, 0); }

QList<CatalogEntry> ReSource::catalog(AssetKind kind)
{
    const QString key = kindName(kind);
    {
        QMutexLocker lock(&m_cacheMutex);
        if (m_catalogCache.contains(key)) return m_catalogCache.value(key);
    }
    QList<CatalogEntry> out;
    const QStringList &names = m_fs.names();
    if (kind == AssetKind::Character) {
        {
            // RE4: costume presets; RE2-style games: survivor part prefabs, creature prefabs, zombies from meshes
            const QString namesFile = firstExisting({m_dataDir + QLatin1Char('/') + m_profile.id + QStringLiteral("/cast_names.json")});
            const QVector<Re4Character> cast = m_profile.id == QLatin1String("re4") ? re4Characters(*this, namesFile) : partsCharacters(*this, namesFile);
            for (const Re4Character &c : cast) {
                for (int i = 0; i < c.looks.size(); ++i) {
                    const Re4Look &l = c.looks[i];
                    CatalogEntry e;
                    e.id = QStringLiteral("look:%1/%2").arg(c.id).arg(i);
                    e.kind = kind;
                    e.name = l.name;
                    e.group = c.name;
                    e.path = l.prefabs.value(0);
                    if (l.variant) e.tags << QStringLiteral("damaged");
                    if (l.dlc) e.tags << QStringLiteral("dlc");
                    e.extra = {{QStringLiteral("character"), c.id}, {QStringLiteral("code"), c.code}, {QStringLiteral("tree"), c.tree},
                               {QStringLiteral("characterName"), c.name}, {QStringLiteral("characterGroup"), c.group},
                               {QStringLiteral("general"), c.general}, {QStringLiteral("prefabs"), l.prefabs}, {QStringLiteral("look"), i}};
                    if (!l.meshes.isEmpty()) { e.extra.insert(QStringLiteral("meshes"), l.meshes); e.path = l.meshes.value(0); }
                    QVariantMap off;
                    for (auto it = l.hiddenParts.cbegin(); it != l.hiddenParts.cend(); ++it) {
                        QVariantList v;
                        for (int x : *it) v << x;
                        off.insert(it.key(), v);
                    }
                    e.extra.insert(QStringLiteral("off"), off);
                    out << e;
                }
            }
        }
        if (out.isEmpty()) {                     // any other game: every character mesh
            const QString ext = QStringLiteral(".mesh.%1").arg(formatVersion(QStringLiteral("mesh")));
            for (const QString &n : names) {
                if (!n.endsWith(ext, Qt::CaseInsensitive) || !n.contains(QLatin1String("/character/"), Qt::CaseInsensitive)) continue;
                CatalogEntry e;
                e.id = QStringLiteral("mesh:") + n;
                e.kind = kind;
                e.name = stem(n);
                e.group = n.section(QLatin1Char('/'), -3, -3);
                e.path = n;
                out << e;
            }
        }
    } else if (kind == AssetKind::Prop) {
        // groups the Asset Browser shows: prop (loose objects), weapon, environment (world pieces), character
        // (accessories), other; engine test and UI meshes are left out. Human names come from the plugin data.
        const QString ext = QStringLiteral(".mesh.%1").arg(formatVersion(QStringLiteral("mesh")));
        QHash<QString, QString> titles;                       // mesh stem (lower case) -> name
        {
            QFile nf(firstExisting({m_dataDir + QLatin1Char('/') + m_profile.id + QStringLiteral("/prop_names.json")}));
            if (nf.open(QIODevice::ReadOnly)) {
                const QJsonObject all = QJsonDocument::fromJson(nf.readAll()).object();
                for (auto g = all.begin(); g != all.end(); ++g) {
                    const QJsonObject group = g->toObject();
                    for (auto it = group.begin(); it != group.end(); ++it) titles.insert(it.key().toLower(), it->toString());
                }
            }
        }
        for (const QString &n : names) {
            if (!n.endsWith(ext, Qt::CaseInsensitive)) continue;
            const QString l = n.toLower();
            if (l.contains(QLatin1String("/streaming/")) || l.contains(QLatin1String("/character/ch/")) || l.contains(QLatin1String("/vfx"))
                || l.contains(QLatin1String("/ui/")) || l.contains(QLatin1String("/systems/")) || l.contains(QLatin1String("/test/"))
                || l.contains(QLatin1String("/postprocess/")) || l.contains(QLatin1String("re_engine_library")))
                continue;
            CatalogEntry e;
            e.id = QStringLiteral("mesh:") + n;
            e.kind = kind;
            e.name = stem(n);
            // RE4: character/wp, environment/sm; RE2: character/weapon, setmodel/sm1x-sm7x (sm2x are room pieces)
            e.group = l.contains(QLatin1String("/character/wp/")) || l.contains(QLatin1String("/character/weapon/")) ? QStringLiteral("weapon")
                    : l.contains(QLatin1String("/environment/sm/")) || (l.contains(QLatin1String("/setmodel/sm")) && !l.contains(QLatin1String("/setmodel/sm2x"))) ? QStringLiteral("prop")
                    : l.contains(QLatin1String("/environment/")) || l.contains(QLatin1String("/setmodel/sm2x")) ? QStringLiteral("environment")
                    : l.contains(QLatin1String("/character/")) ? QStringLiteral("character") : QStringLiteral("other");
            e.path = n;
            if (m_profile.id == QLatin1String("re4") && !l.contains(QLatin1String("/_chainsaw/"))) e.tags << QStringLiteral("dlc");
            if (const QString title = titles.value(e.name.toLower()); !title.isEmpty()) e.extra.insert(QStringLiteral("title"), title);
            out << e;
        }
    } else if (kind == AssetKind::Animation) {
        const QString ext = QStringLiteral(".motlist.%1").arg(formatVersion(QStringLiteral("motlist")));
        for (const QString &n : names) {
            if (!n.endsWith(ext, Qt::CaseInsensitive)) continue;
            CatalogEntry e;
            e.id = n;
            e.kind = kind;
            e.name = stem(n);
            const int a = n.indexOf(QLatin1String("/animation/"), 0, Qt::CaseInsensitive);
            e.group = a >= 0 ? n.mid(a + 11).section(QLatin1Char('/'), 0, 1) : n.section(QLatin1Char('/'), -2, -2);
            e.path = n;
            if (n.contains(QLatin1String("/facial/"), Qt::CaseInsensitive) || n.contains(QLatin1String("_facial"), Qt::CaseInsensitive)) e.tags << QStringLiteral("facial");
            out << e;
        }
    } else if (kind == AssetKind::Stage) {
        const QString ext = QStringLiteral(".scn.%1").arg(formatVersion(QStringLiteral("scn")));
        // RE4 keeps one "optimized" root scene per area chunk linking everything the area shows:
        // appsystem/scene/optimized/stage/st40/st40_201.scn. Named standing points come from the plugin data.
        static const QRegularExpression chunkRx(QStringLiteral("/_chainsaw/appsystem/scene/optimized/stage/st\\d+/st(\\d+)_(\\d+)\\.scn\\.\\d+$"),
                                                QRegularExpression::CaseInsensitiveOption);
        QMultiHash<int, QVariantMap> points;          // chunk number (40201) -> named points {name, map, pos}
        QFile f(firstExisting({m_dataDir + QLatin1Char('/') + m_profile.id + QStringLiteral("/stage_names.json")}));
        if (f.open(QIODevice::ReadOnly)) {
            for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("areas")).toArray())
                points.insert(v.toObject().value(QStringLiteral("stage")).toInt(), v.toObject().toVariantMap());
        }
        for (const QString &n : names) {
            const auto m = chunkRx.match(n);
            if (!m.hasMatch()) continue;
            const int number = m.captured(1).toInt() * 1000 + m.captured(2).toInt();
            CatalogEntry e;
            e.id = QStringLiteral("stage:") + n;
            e.kind = kind;
            e.path = n;
            e.name = QStringLiteral("st%1_%2").arg(m.captured(1), m.captured(2));
            e.group = number < 50000 ? QStringLiteral("Village") : number < 59000 ? QStringLiteral("Castle") : QStringLiteral("Island");
            QVariantList pts;
            for (const QVariantMap &p : points.values(number)) pts << p;
            std::sort(pts.begin(), pts.end(), [](const QVariant &a, const QVariant &b) {
                return a.toMap().value(QStringLiteral("name")).toString() < b.toMap().value(QStringLiteral("name")).toString(); });
            if (!pts.isEmpty()) {
                e.group = pts.first().toMap().value(QStringLiteral("map"), e.group).toString();
                e.tags << QStringLiteral("named");
            }
            e.extra = {{QStringLiteral("stage"), number}, {QStringLiteral("code"), e.name}, {QStringLiteral("points"), pts}};
            out << e;
        }
        // RE2-style games: objectroot/scene/location/<place>/environments.scn links every room of a place (police
        // station, sewers ...); each room is environments/<st>.scn and is offered on its own (a whole place loaded at
        // once takes ~14 GB).
        if (out.isEmpty()) {
            static const QRegularExpression locRx(QStringLiteral("/objectroot/scene/location/([^/]+)/environments(?:/([^/]+))?\\.scn\\.\\d+$"),
                                                  QRegularExpression::CaseInsensitiveOption);
            QJsonObject places;
            QFile nf(firstExisting({m_dataDir + QLatin1Char('/') + m_profile.id + QStringLiteral("/stage_names.json")}));
            if (nf.open(QIODevice::ReadOnly)) places = QJsonDocument::fromJson(nf.readAll()).object().value(QStringLiteral("locations")).toObject();
            QList<CatalogEntry> rooms;
            int number = 0;
            for (const QString &n : names) {
                const auto m = locRx.match(n);
                if (!m.hasMatch()) continue;
                const QString place = m.captured(1).toLower();
                const QJsonValue label = places.value(place);
                if (label.isNull() || label.toString().isEmpty()) continue;        // title screens, test and system scenes
                CatalogEntry e;
                e.id = QStringLiteral("stage:") + n;
                e.kind = kind;
                e.path = n;
                e.group = label.toString();
                const bool whole = m.captured(2).isEmpty();
                if (whole) continue;                   // a whole place needs ~14 GB (the police station): rooms only for now
                e.name = whole ? QStringLiteral("The whole %1").arg(label.toString()) : m.captured(2);
                e.tags << QStringLiteral("named");
                // one named point without a position: the Work Camera goes to the middle of what loads
                e.extra = {{QStringLiteral("stage"), ++number}, {QStringLiteral("code"), whole ? place : m.captured(2)},
                           {QStringLiteral("points"), QVariantList{QVariantMap{{QStringLiteral("name"), e.name}, {QStringLiteral("map"), e.group}}}},
                           {QStringLiteral("whole"), whole}};
                (whole ? out : rooms) << e;
            }
            std::sort(rooms.begin(), rooms.end(), [](const CatalogEntry &a, const CatalogEntry &b) { return a.name < b.name; });
            out << rooms;
        }
        // other games: every environment scene is a stage of its own
        if (out.isEmpty()) {
            for (const QString &n : names) {
                if (!n.endsWith(ext, Qt::CaseInsensitive) || !n.contains(QLatin1String("/environment/"), Qt::CaseInsensitive)) continue;
                CatalogEntry e;
                e.id = QStringLiteral("stage:") + n;
                e.kind = kind;
                e.name = stem(n);
                e.group = n.section(QLatin1Char('/'), -3, -3);
                e.path = n;
                out << e;
            }
        }
    }
    QMutexLocker lock(&m_cacheMutex);
    m_catalogCache.insert(key, out);
    return out;
}

ModelPtr ReSource::loadModel(const QString &id, QString *error)
{
    if (id.startsWith(QLatin1String("look:"))) {
        for (const CatalogEntry &e : catalog(AssetKind::Character)) {
            if (e.id != id) continue;
            QHash<QString, QVector<int>> off;
            const QVariantMap om = e.extra.value(QStringLiteral("off")).toMap();
            for (auto it = om.cbegin(); it != om.cend(); ++it) {
                QVector<int> v;
                for (const QVariant &x : it->toList()) v << x.toInt();
                off.insert(it.key(), v);
            }
            const QString title = e.extra.value(QStringLiteral("characterName")).toString() + QStringLiteral(" – ") + e.name;
            const QStringList meshes = e.extra.value(QStringLiteral("meshes")).toStringList();
            auto m = meshes.isEmpty() ? modelFromPrefabs(id, title, e.extra.value(QStringLiteral("prefabs")).toStringList(), off, error)
                                      : modelFromMeshes(id, title, meshes, error);
            if (m) {
                auto mm = m.constCast<ModelAsset>();
                mm->info.insert(QStringLiteral("character"), e.extra.value(QStringLiteral("character")));
                mm->info.insert(QStringLiteral("code"), e.extra.value(QStringLiteral("code")));
                mm->info.insert(QStringLiteral("general"), e.extra.value(QStringLiteral("general")));
            }
            return m;
        }
        if (error) *error = QStringLiteral("unknown look %1").arg(id);
        return {};
    }
    if (id.startsWith(QLatin1String("pfb:"))) return modelFromPrefabs(id, stem(id.mid(4)), {id.mid(4)}, {}, error);
    // mesh:<mesh>[|<mdf2>] - a world object placed by a scene carries its own material file
    if (id.startsWith(QLatin1String("mesh:"))) return modelFromMesh(id.mid(5).section(QLatin1Char('|'), 0, 0), id.section(QLatin1Char('|'), 1, 1), error);
    if (id.contains(QLatin1String(".pfb"), Qt::CaseInsensitive)) return modelFromPrefabs(id, stem(id), {id}, {}, error);
    return modelFromMesh(id, {}, error);
}

AnimationSetPtr ReSource::loadAnimations(const QString &id, QString *error)
{
    const QString full = id.contains(QLatin1String(".mot"), Qt::CaseInsensitive) && !id.contains(QLatin1String(".motlist"), Qt::CaseInsensitive)
                             ? resolve(id, QStringLiteral("mot")) : resolve(id, QStringLiteral("motlist"));
    QString err;
    const QByteArray data = m_fs.read(full, &err);
    if (data.isEmpty()) { if (error) *error = err; return {}; }
    auto set = full.contains(QLatin1String(".motlist."), Qt::CaseInsensitive) ? parseMotlist(data, &err) : parseMot(data, &err);
    if (!set) { if (error) *error = err; return {}; }
    set->path = full;
    return set;
}

QList<CatalogEntry> ReSource::animationsFor(const QString &modelId)
{
    QString code;
    QString general;
    if (modelId.startsWith(QLatin1String("look:")))
        for (const CatalogEntry &e : catalog(AssetKind::Character))
            if (e.id == modelId) { code = e.extra.value(QStringLiteral("code")).toString(); general = e.extra.value(QStringLiteral("general")).toString(); break; }
    QList<CatalogEntry> all = catalog(AssetKind::Animation), out;
    if (code.isEmpty()) return all;
    // RE4 keeps a character's lists in animation/ch/<code>/, RE2 in animation/player/<code>/ or animation/enemy/<code>/
    for (const CatalogEntry &e : all)
        for (const char *dir : {"ch", "player", "enemy", "npc"})
            if (e.path.contains(QStringLiteral("/animation/%1/%2/").arg(QLatin1String(dir), code), Qt::CaseInsensitive)) { out << e; break; }
    std::stable_sort(out.begin(), out.end(), [&](const CatalogEntry &a, const CatalogEntry &b) { return (a.path == general) > (b.path == general); });
    return out;
}

// ------------------------------------------------------------------ thumbnails
QImage ReSource::thumbnail(const QString &modelId, int size, QString *error)
{
    const ModelPtr m = loadModel(modelId, error);
    if (!m) return {};
    return renderThumbnail(*m, [this](const QString &path, int *w, int *h) -> QByteArray {
        QString e;
        const TexturePtr t = loadTexture(path, &e);
        return t ? textureRgba(*t, 128, w, h) : QByteArray();
    }, size);
}

// ------------------------------------------------------------------ stages
namespace {
QMatrix4x4 transformOf(const RszDocument &doc, int xf)
{
    QMatrix4x4 m;
    if (xf < 0) return m;
    m.translate(doc.field(xf, QStringLiteral("Position")).value<QVector3D>());
    m.rotate(doc.field(xf, QStringLiteral("Rotation")).value<QQuaternion>());
    const QVariant s = doc.field(xf, QStringLiteral("Scale"));
    m.scale(s.isValid() ? s.value<QVector3D>() : QVector3D(1, 1, 1));
    return m;
}

// linked scenes that make the picture (areas also link navigation, sound, event, effect and occluder scenes)
bool pictureScene(const QString &path)
{
    const QString l = path.toLower();
    for (const char *skip : {"/occ/", "/vfx/", "/sound/", "/event/", "/navigation/", "/leveldesign/", "/develop/", "/weather/", "postprocess"})
        if (l.contains(QLatin1String(skip))) return false;
    return true;
}
} // namespace

// A stage is a root scene and the scenes it links (via.Folder ScenePath + UniversalOffset), flattened into mesh
// placements (mesh + material file + world matrix) and lights. Objects the game only shows while streaming, and
// meshes switched off in the scene, are left out.
StagePtr ReSource::loadStage(const QString &id, QString *error)
{
    const RszTypes *types = rszTypes();
    if (!types) { if (error) *error = m_rszError; return {}; }
    const QString root = id.startsWith(QLatin1String("stage:")) ? id.mid(6) : id;
    auto stage = QSharedPointer<StageAsset>::create();
    stage->id = id;
    stage->name = stem(root);
    QSet<QString> seen;
    QStringList problems;
    QVector3D lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f);

    // a lighting variant: RE4 keeps one light scene per chapter / time of day in folders the game switches on
    // (light_st40_200_cp10_chp1_3); their lights are tagged so a film uses one set, not all at once
    static const QRegularExpression variantRx(QStringLiteral("_cp\\d+_(chp\\d+_\\d+)$"), QRegularExpression::CaseInsensitiveOption);
    std::function<void(const QString &, const QVector3D &, int, const QString &)> walk = [&](const QString &path, const QVector3D &offset, int depth,
                                                                                            const QString &variant) {
        const QString full = resolve(path, QStringLiteral("scn"));
        if (depth > 6 || seen.contains(full.toLower())) return;
        seen.insert(full.toLower());
        const QByteArray data = m_fs.read(full);
        if (data.isEmpty()) { problems << QStringLiteral("%1 is missing").arg(stem(path)); return; }
        RszDocument doc;
        QString err;
        if (!doc.parse(data, *types, &err)) { problems << QStringLiteral("%1: %2").arg(stem(path), err); return; }

        // linked scenes
        bool linked = false;
        for (const RszDocument::Folder &fo : doc.folders) {
            const QString sp = doc.field(fo.instance, QStringLiteral("ScenePath")).toString();
            if (sp.isEmpty()) continue;
            linked = true;
            QString v = variant;
            if (const auto vm = variantRx.match(stem(sp)); vm.hasMatch()) v = vm.captured(1).toLower();
            if (pictureScene(sp)) walk(sp, offset + doc.field(fo.instance, QStringLiteral("UniversalOffset")).value<QVector3D>(), depth + 1, v);
        }
        if (!linked)
            for (const QString &r : doc.resources)
                if (r.endsWith(QLatin1String(".scn"), Qt::CaseInsensitive) && pictureScene(r)) walk(r, offset, depth + 1, variant);
        // an area's ground comes in two material qualities next to its static.scn: take the enhanced one
        QString rel = full.section(QLatin1Char('/'), 2);                        // without natives/<platform>/
        rel = rel.left(rel.lastIndexOf(QLatin1String(".scn"), -1, Qt::CaseInsensitive) + 4);
        if (stem(rel).compare(QLatin1String("static"), Qt::CaseInsensitive) == 0) {
            const QString dir = rel.section(QLatin1Char('/'), 0, -2), code = dir.section(QLatin1Char('/'), -1);
            for (const QString &v : {dir + QStringLiteral("/enhanced_") + code + QStringLiteral(".scn"), dir + QStringLiteral("/legacy_") + code + QStringLiteral(".scn")})
                if (m_fs.exists(resolve(v, QStringLiteral("scn")))) { walk(v, offset, depth + 1, variant); break; }
        }

        // objects: world matrices down the parent chain
        const int n = int(doc.gameObjects.size());
        QVector<QMatrix4x4> world(n);
        QVector<quint8> state(n, 0);
        QMatrix4x4 base;
        base.translate(offset);
        std::function<QMatrix4x4(int)> worldOf = [&](int g) -> QMatrix4x4 {
            if (state[g] == 2) return world[g];
            state[g] = 1;
            const int p = doc.gameObjects[g].parent;
            const QMatrix4x4 parent = p >= 0 && p < n && state[p] != 1 ? worldOf(p) : base;
            world[g] = parent * transformOf(doc, doc.component(g, QStringLiteral("via.Transform")));
            state[g] = 2;
            return world[g];
        };
        // gimmicks the game puts in place at run time sit at the origin in the file (a cart, buckets, a pool of
        // breakables): their root is a "Gimmick" at exactly (0, 0, 0) unturned. Breakables keep their broken state
        // ("After") beside the whole one ("Before"): only the whole one is drawn.
        auto rootOf = [&](int g) { int r = g; for (int k = 0; k < 64 && doc.gameObjects[r].parent >= 0 && doc.gameObjects[r].parent < n; ++k) r = doc.gameObjects[r].parent; return r; };
        auto broken = [&](int g) {
            for (int x = g, k = 0; x >= 0 && x < n && k < 64; x = doc.gameObjects[x].parent, ++k)
                if (doc.gameObjectName(x) == QLatin1String("After")) return true;
            return false;
        };
        auto runtimePlaced = [&](int g) {
            const int r = rootOf(g);
            if (doc.field(doc.gameObjects[r].instance, QStringLiteral("Tag")).toString() != QLatin1String("Gimmick")) return false;
            return transformOf(doc, doc.component(r, QStringLiteral("via.Transform"))).isIdentity();
        };
        for (int g = 0; g < n; ++g) {
            if (doc.component(g, QStringLiteral("chainsaw.StreamingDummyController")) >= 0) continue;
            if (doc.component(g, QStringLiteral("via.render.Mesh")) >= 0 && (broken(g) || runtimePlaced(g))) continue;
            const QVariant drawSelf = doc.field(doc.gameObjects[g].instance, QStringLiteral("DrawSelf"));
            if (drawSelf.isValid() && !drawSelf.toBool()) continue;
            const int mc = doc.component(g, QStringLiteral("via.render.Mesh"));
            const QVariant meshOn = mc >= 0 ? doc.field(mc, QStringLiteral("Enabled")) : QVariant();
            if (mc >= 0 && (!meshOn.isValid() || meshOn.toBool())) {
                const QVariant drawDefault = doc.field(mc, QStringLiteral("DrawDefault"));
                const QString mesh = doc.field(mc, QStringLiteral("Mesh")).toString();
                // occluder shells (RE2's *_OCC / *_VOCC) carry no geometry to draw
                const bool occluder = mesh.endsWith(QLatin1String("_OCC.mesh"), Qt::CaseInsensitive) || mesh.endsWith(QLatin1String("_VOCC.mesh"), Qt::CaseInsensitive);
                if ((!drawDefault.isValid() || drawDefault.toBool()) && !mesh.isEmpty() && !occluder && !mesh.contains(QLatin1String("RE_ENGINE_LIBRARY"), Qt::CaseInsensitive)) {
                    StageInstance si;
                    si.name = doc.gameObjectName(g);
                    si.model = QStringLiteral("mesh:") + mesh + QLatin1Char('|') + doc.field(mc, QStringLiteral("Material")).toString();
                    si.world = worldOf(g);
                    const QVector3D p = si.world.column(3).toVector3D();
                    lo = QVector3D(std::min(lo.x(), p.x()), std::min(lo.y(), p.y()), std::min(lo.z(), p.z()));
                    hi = QVector3D(std::max(hi.x(), p.x()), std::max(hi.y(), p.y()), std::max(hi.z(), p.z()));
                    stage->instances << si;
                }
            }
            for (const auto &[type, kind] : {std::pair{"via.render.PointLight", StageLight::Point}, std::pair{"via.render.SpotLight", StageLight::Spot},
                                             std::pair{"via.render.DirectionalLight", StageLight::Directional}}) {
                const int lc = doc.component(g, QLatin1String(type));
                if (lc < 0 || !doc.field(lc, QStringLiteral("Enabled")).toBool()) continue;
                StageLight l;
                l.kind = kind;
                l.name = doc.gameObjectName(g);
                l.world = worldOf(g);
                l.color = doc.field(lc, QStringLiteral("Color")).value<QVector3D>();
                // Unit 1 = candela (RE4's chapter lights): lumens of an even emitter are 4 pi times that
                const float raw = doc.field(lc, QStringLiteral("Intensity")).toFloat();
                l.intensity = doc.field(lc, QStringLiteral("Unit")).toInt() == 1 ? raw * 12.566f : raw;
                // how far it reaches: ReferenceEffectiveRange (RE4); older games give the reach as Radius, RE4's
                // Radius is the bulb (3 cm)
                const float reach = doc.field(lc, QStringLiteral("ReferenceEffectiveRange")).toFloat(), radius = doc.field(lc, QStringLiteral("Radius")).toFloat();
                l.range = reach > 0.1f ? reach : radius > 0.5f ? radius : 8.f;
                l.temperature = doc.field(lc, QStringLiteral("Temperature")).toFloat();
                if (l.temperature < 500) l.temperature = 6500;
                l.blackbody = doc.field(lc, QStringLiteral("BlackBodyRadiation")).toBool();
                if (doc.field(lc, QStringLiteral("UseCustomAttenuation")).toBool())
                    l.fadeStart = std::max(0.1f, doc.field(lc, QStringLiteral("AttenuationStartDistance")).toFloat());
                l.shadows = doc.field(lc, QStringLiteral("ShadowEnable")).toBool();
                if (kind == StageLight::Spot) {
                    l.cone = doc.field(lc, QStringLiteral("Cone")).toFloat();
                    l.spread = doc.field(lc, QStringLiteral("Spread")).toFloat();
                }
                l.variant = variant;
                stage->lights << l;
            }
        }
    };
    walk(root, {}, 0, {});
    // RE4 streams an area as a block of chunks (st40_2xx: the square, its houses and yards): load the whole block
    static const QRegularExpression chunkRx(QStringLiteral("/appsystem/scene/optimized/stage/st(\\d+)/st(\\d+)_(\\d)(\\d\\d)\\.scn\\.\\d+$"),
                                            QRegularExpression::CaseInsensitiveOption);
    if (const auto m = chunkRx.match(root); m.hasMatch()) {
        const QString prefix = root.left(m.capturedStart(0)) + QStringLiteral("/appsystem/scene/optimized/stage/st") + m.captured(1)
                               + QStringLiteral("/st") + m.captured(2) + QLatin1Char('_') + m.captured(3);
        // ... plus the region's landscape (st40_000: the far hills around every block)
        const QString landscape = root.left(m.capturedStart(0)) + QStringLiteral("/appsystem/scene/optimized/stage/st") + m.captured(1)
                                  + QStringLiteral("/st") + m.captured(2) + QStringLiteral("_000.scn");
        for (const QString &n : m_fs.names()) {
            if (n.compare(root, Qt::CaseInsensitive) == 0 || !chunkRx.match(n).hasMatch()) continue;
            if (n.startsWith(prefix, Qt::CaseInsensitive) || n.startsWith(landscape, Qt::CaseInsensitive)) walk(n, {}, 0, {});
        }
    }
    if (stage->instances.isEmpty()) {
        if (error) *error = problems.isEmpty() ? QStringLiteral("nothing to show in %1").arg(stem(root)) : problems.join(QStringLiteral("; "));
        return {};
    }
    // the same mesh placed twice at the same spot (an area's ground from two scenes, in two material qualities;
    // swaying plants listed twice): drawn once, the enhanced material kept. Two copies z-fight into grain.
    {
        QHash<QString, int> at;
        QVector<StageInstance> kept;
        kept.reserve(stage->instances.size());
        for (const StageInstance &si : std::as_const(stage->instances)) {
            QString key = si.model.section(QLatin1Char('|'), 0, 0).toLower();
            const float *m = si.world.constData();
            for (int i = 0; i < 16; ++i) key += QLatin1Char(',') + QString::number(std::round(m[i] * 100) / 100, 'f', 2);
            const auto it = at.constFind(key);
            if (it == at.cend()) { at.insert(key, int(kept.size())); kept << si; continue; }
            if (si.model.contains(QLatin1String("_Enhanced_"), Qt::CaseInsensitive)) kept[*it] = si;
        }
        stage->instances = kept;
    }
    stage->boundsMin = lo;
    stage->boundsMax = hi;
    QSet<QString> variants;
    for (const StageLight &l : std::as_const(stage->lights)) if (!l.variant.isEmpty()) variants.insert(l.variant);
    stage->lightVariants = QStringList(variants.cbegin(), variants.cend());
    stage->lightVariants.sort();
    return stage;
}

QVariantMap ReSource::describe(const QString &path)
{
    const QString full = resolve(path, {});
    QVariantMap m{{QStringLiteral("path"), full}, {QStringLiteral("exists"), m_fs.exists(full)}};
    return m;
}

} // namespace re
