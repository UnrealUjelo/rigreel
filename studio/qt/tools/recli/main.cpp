// recli: command-line checks for the RE Engine readers against a real install.
//   recli info    <game> <list>
//   recli extract <game> <list> <path> <outfile>
//   recli ls      <game> <list> <prefix> [suffix]
//   recli tex     <game> <list> <path> <out.png> [maxSize]
//   recli bcntest
#include "Bcn.h"
#include "Mdf.h"
#include "Mesh.h"
#include "Mot.h"
#include <QMap>
#include "Pak.h"
#include "Rsz.h"
#include <QDir>
#include <QVector3D>
#include <QQuaternion>
#include "Tex.h"
#include "ReSource.h"
#include "Profiles.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QTextStream>

static QTextStream out(stdout);

static QString streamingPath(const QString &p)
{
    // natives/stm/xxx -> natives/stm/streaming/xxx
    const int i = p.indexOf(QLatin1Char('/'), p.indexOf(QLatin1Char('/')) + 1);
    return i < 0 ? QString() : p.left(i) + QStringLiteral("/streaming") + p.mid(i);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList a = app.arguments();
    if (a.value(1) == QLatin1String("bcntest")) {
        QString report;
        const bool ok = re::bcn::selfTest(&report);
        out << report << (ok ? "OK\n" : "FAILED\n");
        return ok ? 0 : 1;
    }
    if (a.size() < 3) {
        out << "usage: recli info|extract|ls|tex <game> <list> ...\n";
        return 2;
    }
    const QString cmd = a[1];
    QElapsedTimer t;
    t.start();
    re::PakFileSystem fs;
    QString err;
    const bool rawFs = cmd != QLatin1String("cast") && cmd != QLatin1String("model") && cmd != QLatin1String("catalog") && cmd != QLatin1String("stage") && cmd != QLatin1String("rest") && cmd != QLatin1String("rigdiff");
    if (rawFs && !fs.open(a[2], &err)) { out << "open failed: " << err << "\n"; return 1; }
    const qint64 tOpen = t.restart();
    if (rawFs && !fs.loadNames(a[3], &err)) out << "names: " << err << "\n";
    const qint64 tNames = t.restart();

    if (cmd == QLatin1String("info")) {
        out << "archives " << fs.archiveCount() << " entries " << fs.entryCount() << " named " << fs.names().size()
            << "  (open " << tOpen << " ms, names " << tNames << " ms)\n";
        if (!err.isEmpty()) out << "warning: " << err << "\n";
        return 0;
    }
    if (cmd == QLatin1String("extract") && a.size() >= 6) {
        const QByteArray data = fs.read(a[4], &err);
        if (data.isEmpty()) { out << "read failed: " << err << "\n"; return 1; }
        QFile f(a[5]);
        if (!f.open(QIODevice::WriteOnly)) return 1;
        f.write(data);
        out << data.size() << " bytes in " << t.elapsed() << " ms\n";
        return 0;
    }
    if (cmd == QLatin1String("ls") && a.size() >= 5) {
        for (const QString &p : fs.list(a[4], a.size() > 5 ? a[5] : QString(), 200)) out << p << "\n";
        return 0;
    }
    if (cmd == QLatin1String("tex") && a.size() >= 6) {
        const QString path = a[4];
        auto resident = re::parseTex(fs.read(path), &err);
        const QString sp = streamingPath(path);
        QSharedPointer<dir::TextureAsset> streaming;
        if (fs.exists(sp)) streaming = re::parseTex(fs.read(sp), &err);
        auto merged = re::mergeTex(resident, streaming, a.size() > 6 ? a[6].toInt() : 2048);
        if (!merged) { out << "failed: " << err << "\n"; return 1; }
        out << "format " << merged->format << " srgb " << merged->srgb << " " << merged->width << "x" << merged->height << " layers " << merged->arraySize
            << " mips " << merged->mips.size() << " (resident " << (resident ? resident->mips.size() : 0)
            << ", streaming " << (streaming ? streaming->mips.size() : 0) << ") in " << t.elapsed() << " ms\n";
        int w, h;
        const QByteArray rgba = re::textureRgba(*merged, 1024, &w, &h);
        QImage img(reinterpret_cast<const uchar *>(rgba.constData()), w, h, w * 4, QImage::Format_RGBA8888);
        img.copy().save(a[5]);
        // also the alpha channel as grey
        QImage alpha(w, h, QImage::Format_Grayscale8);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) alpha.scanLine(y)[x] = uchar(rgba[(y * w + x) * 4 + 3]);
        alpha.save(QString(a[5]).replace(QStringLiteral(".png"), QStringLiteral("_a.png")));
        const QByteArray small = re::textureRgba(*merged, 4, &w, &h);
        out << "smallest used level " << w << "x" << h << "\n";
        return 0;
    }
    if (cmd == QLatin1String("mesh") && a.size() >= 5) {
        const QString path = a[4];
        const quint32 fv = path.section(QLatin1Char('.'), -1).toUInt();
        auto m = re::parseMesh(fs.read(path), fv, {}, &err);
        if (!m) { out << "mesh failed: " << err << "\n"; return 1; }
        out << "vertices " << m->vertexCount() << " indices " << m->indices.size() << " parts " << m->parts.size()
            << " influences " << m->influences << " uv0 " << m->uv0.size() << " normals " << m->normals.size() << " in " << t.elapsed() << " ms\n";
        out << "bounds " << m->boundsMin.x() << "," << m->boundsMin.y() << "," << m->boundsMin.z() << " .. "
            << m->boundsMax.x() << "," << m->boundsMax.y() << "," << m->boundsMax.z() << "\n";
        out << "materials: " << m->materialNames.join(QStringLiteral(", ")) << "\n";
        if (qEnvironmentVariableIsSet("RECLI_UV")) {
            // how the UV sets relate to world position (terrain splat addressing)
            for (const auto *uvs : {&m->uv0, &m->uv1}) {
                if (uvs->isEmpty()) continue;
                QVector2D lo(1e9f, 1e9f), hi(-1e9f, -1e9f);
                for (const QVector2D &u : *uvs) { lo = QVector2D(std::min(lo.x(), u.x()), std::min(lo.y(), u.y())); hi = QVector2D(std::max(hi.x(), u.x()), std::max(hi.y(), u.y())); }
                out << (uvs == &m->uv0 ? "uv0" : "uv1") << " range " << lo.x() << "," << lo.y() << " .. " << hi.x() << "," << hi.y() << "\n";
            }
            for (int v = 0; v < m->vertexCount(); v += std::max(1, m->vertexCount() / 6)) {
                const QVector3D p = m->positions[v];
                out << "  v" << v << " pos " << p.x() << "," << p.y() << "," << p.z() << " uv0 " << m->uv0.value(v).x() << "," << m->uv0.value(v).y();
                if (!m->uv1.isEmpty()) out << " uv1 " << m->uv1.value(v).x() << "," << m->uv1.value(v).y();
                if (!m->colors.isEmpty()) out << " col " << Qt::hex << m->colors.value(v) << Qt::dec;
                out << "\n";
            }
        }
        QMap<int, int> perGroup;
        for (const auto &p : m->parts) perGroup[p.group] += p.indexCount / 3;
        out << "triangles per part group:";
        for (auto it = perGroup.cbegin(); it != perGroup.cend(); ++it) out << " " << it.key() << ":" << it.value();
        out << "\nbones " << m->skeleton.bones.size() << " skin bones " << m->skinBones.size() << "\n";
        for (int i = 0; i < std::min<int>(12, m->skeleton.bones.size()); ++i) {
            const auto &b = m->skeleton.bones[i];
            out << "  " << i << " " << b.name << " parent " << b.parent << " t " << b.translation.x() << "," << b.translation.y() << "," << b.translation.z()
                << " r " << b.rotation.scalar() << "," << b.rotation.x() << "," << b.rotation.y() << "," << b.rotation.z() << "\n";
        }
        // weights sanity
        double sum = 0;
        int bad = 0;
        for (int v = 0; v < m->vertexCount() && m->influences; ++v) {
            float s = 0;
            for (int k = 0; k < m->influences; ++k) s += m->weights[v * m->influences + k];
            sum += s;
            if (std::abs(s - 1) > 0.05f) ++bad;
        }
        if (m->influences) out << "mean weight sum " << sum / m->vertexCount() << " off by >5%: " << bad << "\n";
        if (a.size() >= 6) {
            const QString mp = a[5];
            auto mats = re::parseMdf(fs.read(mp), mp.section(QLatin1Char('.'), -1).toInt(), &err);
            out << "mdf materials " << mats.size() << (err.isEmpty() ? QString() : QStringLiteral(" (") + err + QStringLiteral(")")) << "\n";
            for (const auto &mt : mats) {
                out << "  " << mt.name << " role " << mt.role << " shader " << mt.shader.section(QLatin1Char('/'), -1)
                    << " twoSided " << mt.twoSided << " alphaTest " << mt.alphaTest << " base " << mt.baseAlpha << " normal " << mt.normalAlpha
                    << " packed " << mt.packedLayout << "\n";
                for (auto it = mt.textures.cbegin(); it != mt.textures.cend(); ++it) out << "      " << it.key() << " = " << it.value().section(QLatin1Char('/'), -1) << "\n";
                const bool all = qEnvironmentVariableIsSet("RECLI_RAW");
                if (mt.textures.isEmpty() || all)
                    for (auto it = mt.rawTextures.cbegin(); it != mt.rawTextures.cend(); ++it) out << "      raw " << it.key() << " = " << it.value() << "\n";
                if (all)
                    for (auto it = mt.params.cbegin(); it != mt.params.cend(); ++it)
                        out << "      param " << it.key() << " = " << it->x() << ", " << it->y() << ", " << it->z() << ", " << it->w() << "\n";
            }
        }
        return 0;
    }
    if (cmd == QLatin1String("rsz") && a.size() >= 6) {
        // recli rsz <game> <list> <types.json> <path> [maxInstances]
        re::RszTypes types;
        const QString cache = QDir::tempPath() + QStringLiteral("/recli_rsz.bin");
        if (!types.load(a[4], cache, &err)) { out << "types: " << err << "\n"; return 1; }
        out << "types " << types.count() << " loaded in " << t.restart() << " ms\n";
        re::RszDocument doc;
        if (!doc.parse(fs.read(a[5]), types, &err)) { out << "parse failed: " << err << "\n"; return 1; }
        out << "kind " << doc.kind << " instances " << doc.instances.size() << " gameobjects " << doc.gameObjects.size()
            << " resources " << doc.resources.size() << " userfiles " << doc.userFiles.size() << " prefabs " << doc.prefabs.size()
            << " folders " << doc.folders.size() << " in " << t.elapsed() << " ms\n";
        {
            int withPrefab = 0, withPrefabNoMesh = 0, withMesh = 0;
            for (int g = 0; g < doc.gameObjects.size(); ++g) {
                const bool mesh = doc.component(g, QStringLiteral("via.render.Mesh")) >= 0;
                if (mesh) ++withMesh;
                if (doc.gameObjects[g].prefab >= 0) { ++withPrefab; if (!mesh) ++withPrefabNoMesh; }
            }
            out << "gameobjects with a mesh " << withMesh << ", from a prefab " << withPrefab << " (" << withPrefabNoMesh << " without a mesh of their own)\n";
            for (const QString &p : doc.prefabs.mid(0, 8)) out << "  prefab " << p << "\n";
        }
        for (const QString &r : doc.resources.mid(0, 10)) out << "  res " << r << "\n";
        for (int g = 0; g < std::min<int>(10, doc.gameObjects.size()); ++g) {
            out << "  GO " << g << " '" << doc.gameObjectName(g) << "' parent " << doc.gameObjects[g].parent << " comps:";
            for (int c : doc.gameObjects[g].components) out << " " << (doc.instance(c) ? doc.instance(c)->typeName() : QStringLiteral("?"));
            out << "\n";
        }
        // RECLI_GO=<part of a name>: those game objects, their parent chain and every component's fields
        if (qEnvironmentVariableIsSet("RECLI_GO")) {
            const QString want = qEnvironmentVariable("RECLI_GO");
            auto dumpInstance = [&](int idx, const QString &indent) {
                const auto *in = doc.instance(idx);
                if (!in) return;
                out << indent << "[" << idx << "] " << in->typeName() << "\n";
                for (auto it = in->fields.cbegin(); it != in->fields.cend(); ++it) {
                    const QVariant &val = it.value();
                    QString v;
                    if (val.metaType().id() == QMetaType::QVector3D) { const auto q = val.value<QVector3D>(); v = QStringLiteral("(%1, %2, %3)").arg(q.x()).arg(q.y()).arg(q.z()); }
                    else if (val.metaType().id() == QMetaType::QQuaternion) { const auto q = val.value<QQuaternion>(); v = QStringLiteral("q(%1; %2, %3, %4)").arg(q.scalar()).arg(q.x()).arg(q.y()).arg(q.z()); }
                    else if (val.metaType().id() == QMetaType::QVariantList) v = QStringLiteral("[%1 items]").arg(val.toList().size());
                    else v = val.toString().left(100);
                    out << indent << "    " << it.key() << " = " << v << "\n";
                }
            };
            for (int g = 0; g < doc.gameObjects.size(); ++g) {
                if (!doc.gameObjectName(g).contains(want)) continue;
                out << "GO " << g << " '" << doc.gameObjectName(g) << "' parent " << doc.gameObjects[g].parent << " prefab " << doc.gameObjects[g].prefab << "\n";
                dumpInstance(doc.gameObjects[g].instance, QStringLiteral("  "));
                for (int c : doc.gameObjects[g].components) dumpInstance(c, QStringLiteral("  "));
                for (int p = doc.gameObjects[g].parent, n = 0; p >= 0 && n < 8; p = doc.gameObjects[p].parent, ++n)
                    out << "  parent GO " << p << " '" << doc.gameObjectName(p) << "'\n";
            }
            return 0;
        }
        const int maxI = a.size() > 6 ? a[6].toInt() : 30;
        // RECLI_TYPE=<part of a type name>: only those instances (all of them, up to maxInstances)
        const QString typeFilter = qEnvironmentVariable("RECLI_TYPE");
        int shownI = 0;
        for (int i = 1; i < doc.instances.size() && shownI < maxI; ++i) {
            const auto &in = doc.instances[i];
            if (!typeFilter.isEmpty() && !in.typeName().contains(typeFilter, Qt::CaseInsensitive)) continue;
            ++shownI;
            out << "  [" << i << "] " << in.typeName() << (in.userFile.isEmpty() ? QString() : QStringLiteral(" user=") + in.userFile) << "\n";
            for (auto it = in.fields.cbegin(); it != in.fields.cend(); ++it) {
                QString v;
                const QVariant &val = it.value();
                if (val.canConvert<re::RszRef>() && val.metaType() == QMetaType::fromType<re::RszRef>()) v = QStringLiteral("-> %1").arg(val.value<re::RszRef>().index);
                else if (val.metaType().id() == QMetaType::QVariantList) v = QStringLiteral("[%1 items]").arg(val.toList().size());
                else if (val.metaType().id() == QMetaType::QVector3D) { const auto q = val.value<QVector3D>(); v = QStringLiteral("(%1, %2, %3)").arg(q.x()).arg(q.y()).arg(q.z()); }
                else if (val.metaType().id() == QMetaType::QQuaternion) { const auto q = val.value<QQuaternion>(); v = QStringLiteral("q(%1; %2, %3, %4)").arg(q.scalar()).arg(q.x()).arg(q.y()).arg(q.z()); }
                else v = val.toString().left(120);
                out << "      " << it.key() << " = " << v << "\n";
            }
        }
        return 0;
    }
    if (cmd == QLatin1String("mot") && a.size() >= 5) {
        // recli mot <game> <list> <motlist> [mesh]
        auto set = re::parseMotlist(fs.read(a[4]), &err);
        if (!set) { out << "motlist failed: " << err << "\n"; return 1; }
        out << set->name << ": " << set->clips.size() << " clips in " << t.elapsed() << " ms\n";
        int badQ = 0, keys = 0;
        for (const auto &c : set->clips)
            for (const auto &tr : c.tracks) {
                keys += tr.rotation.values.size() + tr.translation.values.size();
                for (const auto &q : tr.rotation.values) if (std::abs(q.length() - 1) > 0.01f || std::isnan(q.scalar())) ++badQ;
            }
        out << "keys " << keys << " bad quaternions " << badQ << "\n";
        for (int i = 0; i < std::min<int>(4, set->clips.size()); ++i) {
            const auto &c = set->clips[i];
            out << "  [" << c.id << "] " << c.name << " frames " << c.frames << " fps " << c.fps << " tracks " << c.tracks.size() << " bones " << c.boneNames.size() << "\n";
        }
        if (a.size() >= 7 && a[5] == QLatin1String("--clip")) {
            // recli mot <game> <list> <motlist> --clip <name part>: per-track ranges of one clip
            for (const auto &c : set->clips) {
                if (!c.name.contains(a[6], Qt::CaseInsensitive)) continue;
                out << "[" << c.id << "] " << c.name << " frames " << c.frames << " fps " << c.fps << " tracks " << c.tracks.size() << "\n";
                for (const auto &tr : c.tracks) {
                    QVector3D lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f);
                    int nan = 0;
                    for (const QVector3D &v : tr.translation.values) {
                        if (std::isnan(v.x()) || std::isnan(v.y()) || std::isnan(v.z())) { ++nan; continue; }
                        lo = QVector3D(std::min(lo.x(), v.x()), std::min(lo.y(), v.y()), std::min(lo.z(), v.z()));
                        hi = QVector3D(std::max(hi.x(), v.x()), std::max(hi.y(), v.y()), std::max(hi.z(), v.z()));
                    }
                    int badR = 0;
                    for (const QQuaternion &q : tr.rotation.values) if (std::abs(q.length() - 1) > 0.02f || std::isnan(q.scalar())) ++badR;
                    float tmax = 0;
                    for (float x : tr.rotation.times) tmax = std::max(tmax, x);
                    for (float x : tr.translation.times) tmax = std::max(tmax, x);
                    out << "  " << Qt::hex << tr.boneHash << Qt::dec << " " << c.boneNames.value(tr.boneHash) << " r" << tr.rotation.values.size() << (badR ? QStringLiteral(" BAD%1").arg(badR) : QString())
                        << " t" << tr.translation.values.size() << (nan ? QStringLiteral(" NAN%1").arg(nan) : QString());
                    if (!tr.translation.isEmpty()) out << " [" << lo.x() << "," << lo.y() << "," << lo.z() << " .. " << hi.x() << "," << hi.y() << "," << hi.z() << "]";
                    out << " s" << tr.scale.values.size();
                    if (!tr.scale.isEmpty()) out << " s0(" << tr.scale.values.first().x() << "," << tr.scale.values.first().y() << "," << tr.scale.values.first().z() << ")";
                    out << " tmax " << tmax << "\n";
                }
            }
            return 0;
        }
        if (a.size() >= 6) {
            const QString mp = a[5];
            auto m = re::parseMesh(fs.read(mp), mp.section(QLatin1Char('.'), -1).toUInt(), {}, &err);
            if (!m) { out << "mesh: " << err << "\n"; return 1; }
            const auto &c = set->clips[0];
            int matched = 0;
            for (const auto &tr : c.tracks) if (m->skeleton.findHash(tr.boneHash) >= 0) ++matched;
            out << "tracks matching mesh bones: " << matched << "/" << c.tracks.size() << "\n";
            int shown = 0;
            for (auto it = c.restPose.cbegin(); it != c.restPose.cend() && shown < 6; ++it) {
                const int b = m->skeleton.findHash(it.key());
                if (b < 0) continue;
                const auto &bone = m->skeleton.bones[b];
                const QQuaternion q = it.value().second;
                out << "  " << bone.name << " mesh r(" << bone.rotation.scalar() << "," << bone.rotation.x() << "," << bone.rotation.y() << "," << bone.rotation.z()
                    << ") mot r(" << q.scalar() << "," << q.x() << "," << q.y() << "," << q.z() << ") mesh t(" << bone.translation.x() << "," << bone.translation.y() << "," << bone.translation.z()
                    << ") mot t(" << it.value().first.x() << "," << it.value().first.y() << "," << it.value().first.z() << ")\n";
                ++shown;
            }
            // first key of a few tracks vs mesh rest
            shown = 0;
            for (const auto &tr : c.tracks) {
                const int b = m->skeleton.findHash(tr.boneHash);
                if (b < 0 || tr.rotation.isEmpty() || shown >= 4) continue;
                const QQuaternion q = tr.rotation.values.first();
                out << "  key0 " << m->skeleton.bones[b].name << " r(" << q.scalar() << "," << q.x() << "," << q.y() << "," << q.z() << ") keys " << tr.rotation.values.size()
                    << " t-keys " << tr.translation.values.size() << "\n";
                ++shown;
            }
        }
        return 0;
    }
    if (cmd == QLatin1String("cast") || cmd == QLatin1String("model") || cmd == QLatin1String("catalog") || cmd == QLatin1String("stage") || cmd == QLatin1String("rest") || cmd == QLatin1String("rigdiff")) {
        // recli cast <game>            characters and looks from the plugin
        // recli model <game> <id>      assemble one model
        // recli catalog <game>         counts per asset kind
        dir::GameInfo gi;
        // the game by its exe in the folder (re4.exe, re2.exe ...)
        const re::GameProfile *prof = re::profileByExe(a[2]);
        if (!prof) prof = re::profileById(QStringLiteral("re4"));
        gi.gameId = prof->id;
        gi.folder = a[2];
        re::ReSource src(gi, *prof, QStringLiteral(RE_PLUGIN_DATA));
        out << "game " << prof->id << "\n";
        if (!src.open(&err)) { out << "open: " << err << "\n"; return 1; }
        out << "opened in " << t.restart() << " ms; mesh v" << src.formatVersion(QStringLiteral("mesh")) << " tex v" << src.formatVersion(QStringLiteral("tex"))
            << " motlist v" << src.formatVersion(QStringLiteral("motlist")) << " pfb v" << src.formatVersion(QStringLiteral("pfb")) << "\n";
        if (cmd == QLatin1String("catalog")) {
            for (auto k : {dir::AssetKind::Character, dir::AssetKind::Prop, dir::AssetKind::Animation, dir::AssetKind::Stage}) {
                const auto list = src.catalog(k);
                out << dir::kindName(k) << ": " << list.size() << " (" << t.restart() << " ms)";
                if (!list.isEmpty()) out << "  e.g. " << list.first().name << " [" << list.first().group << "]";
                out << "\n";
            }
            return 0;
        }
        if (cmd == QLatin1String("rigdiff") && a.size() >= 5) {
            // recli rigdiff <game> <modelA> <modelB>: bind pose differences of the bones both skeletons have
            auto ma = src.loadModel(a[3], &err), mb = src.loadModel(a[4], &err);
            if (!ma || !mb) { out << "model: " << err << "\n"; return 1; }
            for (const auto &bone : ma->skeleton.bones) {
                const int j = mb->skeleton.findHash(bone.hash);
                if (j < 0) continue;
                const auto &o = mb->skeleton.bones[j];
                const float ang = 2 * std::acos(std::min(1.f, std::abs(QQuaternion::dotProduct(bone.rotation.normalized(), o.rotation.normalized())))) * 57.2958f;
                const QString pa = bone.parent >= 0 ? ma->skeleton.bones[bone.parent].name : QStringLiteral("-");
                const QString pb = o.parent >= 0 ? mb->skeleton.bones[o.parent].name : QStringLiteral("-");
                if (qEnvironmentVariableIsSet("RECLI_PARENTS")) { out << "  " << bone.name << " < " << pa << "\n"; continue; }
                out << "  " << bone.name << " dR " << QString::number(ang, 'f', 1) << " dT " << QString::number((bone.translation - o.translation).length() * 1000, 'f', 1) << " mm"
                    << " |T| " << QString::number(bone.translation.length() * 1000, 'f', 0) << (pa != pb ? QStringLiteral(" parent %1 vs %2").arg(pa, pb) : QString()) << "\n";
            }
            return 0;
        }
        if (cmd == QLatin1String("rest") && a.size() >= 5) {
            // recli rest <game> <modelId> <motlist> [clip]: a clip's own rest pose against a model's bind pose
            auto model = src.loadModel(a[3], &err);
            if (!model) { out << "model: " << err << "\n"; return 1; }
            auto set = src.loadAnimations(a[4], &err);
            if (!set || set->clips.isEmpty()) { out << "motlist: " << err << "\n"; return 1; }
            const dir::AnimationClip *clip = &set->clips.first();
            for (const auto &c : set->clips) if (a.size() >= 6 && c.name.contains(a[5], Qt::CaseInsensitive)) { clip = &c; break; }
            const dir::Skeleton &sk = model->skeleton;
            out << clip->name << ": " << clip->tracks.size() << " tracks, rest " << clip->restPose.size() << ", model bones " << sk.bones.size() << "\n";
            // the file's top bones (parent 0), and what the binding makes of the clip on this model
            {
                QVector<QVector3D> at(sk.bones.size());
                QVector<QQuaternion> turn(sk.bones.size());
                for (int i = 0; i < sk.bones.size(); ++i) {
                    const int p = sk.bones[i].parent;
                    at[i] = p >= 0 && p < i ? at[p] + turn[p].rotatedVector(sk.bones[i].translation) : sk.bones[i].translation;
                    turn[i] = p >= 0 && p < i ? turn[p] * sk.bones[i].rotation : sk.bones[i].rotation;
                }
                out << "tops:";
                for (auto it = clip->restParent.cbegin(); it != clip->restParent.cend(); ++it) {
                    if (it.value()) continue;
                    const int b = sk.findHash(it.key());
                    const QVector3D r = clip->restPose.value(it.key()).first;
                    const bool skipped = b >= 0 && sk.bones[b].parent >= 0 && (r - at[b]).length() + 0.05f < (r - sk.bones[b].translation).length();
                    out << " " << clip->boneNames.value(it.key()) << (b < 0 ? "(not in model)" : sk.bones[b].parent < 0 ? "(root)" : skipped ? "(model space: skipped)" : "(kept)");
                }
                out << "\n";
            }
            if (qEnvironmentVariableIsSet("RECLI_TOPS")) return 0;
            auto deg = [](const QQuaternion &x, const QQuaternion &y) {
                return QString::number(2 * std::acos(std::min(1.f, std::abs(QQuaternion::dotProduct(x.normalized(), y.normalized())))) * 57.2958f, 'f', 1);
            };
            // model-space rotations of the bind pose
            QVector<QQuaternion> global(sk.bones.size());
            for (int i = 0; i < sk.bones.size(); ++i) {
                const int p = sk.bones[i].parent;
                global[i] = p >= 0 && p < i ? global[p] * sk.bones[i].rotation : sk.bones[i].rotation;
            }
            for (const auto &tr : clip->tracks) {
                const int b = sk.findHash(tr.boneHash);
                const auto rest = clip->restPose.value(tr.boneHash);
                const QVector3D key = tr.translation.isEmpty() ? QVector3D() : tr.translation.values.first();
                out << "  " << clip->boneNames.value(tr.boneHash) << (b < 0 ? " (not in model)" : "");
                if (b >= 0) {
                    const auto &bone = sk.bones[b];
                    out << " bind t(" << bone.translation.x() << "," << bone.translation.y() << "," << bone.translation.z() << ")";
                    out << " rest t(" << rest.first.x() << "," << rest.first.y() << "," << rest.first.z() << ")";
                    if (!tr.translation.isEmpty()) out << " key t(" << key.x() << "," << key.y() << "," << key.z() << ")";
                    out << " rest~bind " << deg(bone.rotation, rest.second) << " rest~bindG " << deg(global[b], rest.second);
                    if (!tr.rotation.isEmpty()) out << " key~bind " << deg(bone.rotation, tr.rotation.values.first()) << " key~rest " << deg(rest.second, tr.rotation.values.first());
                    out << " |rest| " << rest.second.length();
                }
                out << "\n";
            }
            return 0;
        }
        if (cmd == QLatin1String("cast")) {
            const auto list = src.catalog(dir::AssetKind::Character);
            out << list.size() << " looks in " << t.elapsed() << " ms " << src.rszError() << "\n";
            QString last;
            for (const auto &e : list) {
                if (e.group != last) {
                    out << "\n" << e.group << " (" << e.extra.value(QStringLiteral("character")).toString() << ", general "
                        << e.extra.value(QStringLiteral("general")).toString().section(QLatin1Char('/'), -1) << "):";
                    last = e.group;
                }
                out << " | " << e.name;
            }
            out << "\n";
            return 0;
        }
        if (cmd == QLatin1String("stage")) {
            // recli stage <game> <st40_201 | stage:path> [--load]: what an area places, optionally loading every mesh
            QString sid = a.value(3);
            const auto stages = src.catalog(dir::AssetKind::Stage);
            out << stages.size() << " stages listed\n";
            if (!sid.startsWith(QLatin1String("stage:")))
                for (const auto &e : stages) if (e.name == sid) sid = e.id;
            auto st = src.loadStage(sid, &err);
            if (!st) { out << "stage failed: " << err << "\n"; return 1; }
            QMap<QString, int> models;
            for (const auto &i : st->instances) models[i.model]++;
            out << st->name << ": " << st->instances.size() << " placements of " << models.size() << " meshes, " << st->lights.size() << " lights in "
                << t.restart() << " ms; positions " << st->boundsMin.x() << "," << st->boundsMin.y() << "," << st->boundsMin.z() << " .. "
                << st->boundsMax.x() << "," << st->boundsMax.y() << "," << st->boundsMax.z() << "\n";
            // RECLI_NEAR="x,z,r": the placements within r metres of (x, z)
            if (qEnvironmentVariableIsSet("RECLI_NEAR")) {
                const QStringList n = qEnvironmentVariable("RECLI_NEAR").split(QLatin1Char(','));
                const float nx = n.value(0).toFloat(), nz = n.value(1).toFloat(), nr = n.value(2, QStringLiteral("5")).toFloat();
                for (const auto &i : st->instances) {
                    const QVector3D p = i.world.column(3).toVector3D();
                    if (std::hypot(p.x() - nx, p.z() - nz) > nr) continue;
                    out << "  near " << i.name << " at " << p.x() << "," << p.y() << "," << p.z() << "  " << i.model << "\n";
                }
            }
            if (qEnvironmentVariableIsSet("RECLI_LIGHTS")) out << "light variants: " << st->lightVariants.join(QStringLiteral(", ")) << "\n";
            if (qEnvironmentVariableIsSet("RECLI_LIGHTS"))
                for (const auto &l : st->lights) {
                    const QVector3D p = l.world.column(3).toVector3D();
                    out << "  light " << (l.kind == dir::StageLight::Point ? "point" : l.kind == dir::StageLight::Spot ? "spot" : "sun") << " '" << l.name << "' at "
                        << p.x() << "," << p.y() << "," << p.z() << " colour " << l.color.x() << "," << l.color.y() << "," << l.color.z()
                        << " lumens " << l.intensity << " range " << l.range << " cone " << l.cone << "/" << l.spread << " variant '" << l.variant << "'"
                        << " z-axis " << l.world.column(2).x() << "," << l.world.column(2).y() << "," << l.world.column(2).z()
                        << " y-axis " << l.world.column(1).x() << "," << l.world.column(1).y() << "," << l.world.column(1).z() << "\n";
                }
            QList<QPair<int, QString>> top;
            for (auto it = models.cbegin(); it != models.cend(); ++it) top << qMakePair(it.value(), it.key());
            std::sort(top.begin(), top.end(), [](const auto &x, const auto &y) { return x.first > y.first; });
            for (const auto &p : top.mid(0, 8)) out << "  " << p.first << " x " << p.second.section(QLatin1Char('|'), 0, 0).section(QLatin1Char('/'), -1) << "\n";
            if (a.value(4) == QLatin1String("--load")) {
                int ok = 0, verts = 0;
                QSet<QString> texs;
                QStringList failed;
                for (auto it = models.cbegin(); it != models.cend(); ++it) {
                    auto m = src.loadModel(it.key(), &err);
                    if (!m) { failed << err; continue; }
                    ++ok;
                    for (const auto &p : m->pieces) { verts += p.mesh->vertexCount() * it.value(); for (const auto &mt : p.materials) for (const auto &tp : mt.textures) texs.insert(tp); }
                }
                out << "models " << ok << "/" << models.size() << " (" << verts << " vertices placed, " << texs.size() << " textures) in " << t.restart() << " ms\n";
                int texOk = 0;
                QStringList texFailed;
                for (const QString &tp : std::as_const(texs)) { if (src.loadTexture(tp, &err)) ++texOk; else texFailed << tp.section(QLatin1Char('/'), -1) + QStringLiteral(" (") + err + QLatin1Char(')'); }
                out << "textures loaded " << texOk << "/" << texs.size() << " in " << t.restart() << " ms\n";
                for (const QString &f : texFailed.mid(0, 12)) out << "  texture failed " << f << "\n";
                // which shaders the area uses, and which of them came out without a colour texture
                QMap<QString, QPair<int, int>> shaders;         // shader -> (materials, without base colour)
                QMap<QString, QString> example;
                for (auto it = models.cbegin(); it != models.cend(); ++it) {
                    auto m = src.loadModel(it.key(), &err);
                    if (!m) continue;
                    for (const auto &p : m->pieces)
                        for (const auto &mt : p.materials) {
                            const QString sh = mt.shader.section(QLatin1Char('/'), -1);
                            auto &c = shaders[sh];
                            ++c.first;
                            if (!mt.textures.contains(QStringLiteral("baseColor"))) ++c.second;
                            if (!example.contains(sh)) example[sh] = mt.name + QStringLiteral(" in ") + it.key().section(QLatin1Char('|'), 0, 0);
                        }
                }
                // bright colour textures (average of a small mip): candidates for surfaces that render white
                if (qEnvironmentVariableIsSet("RECLI_BRIGHT")) {
                    QSet<QString> shown;
                    for (auto it = models.cbegin(); it != models.cend(); ++it) {
                        auto m = src.loadModel(it.key(), &err);
                        if (!m) continue;
                        for (const auto &p : m->pieces)
                            for (const auto &mt : p.materials) {
                                const QString tp = mt.textures.value(QStringLiteral("baseColor"));
                                if (tp.isEmpty() || shown.contains(mt.name)) continue;
                                shown.insert(mt.name);
                                auto tex = src.loadTexture(tp, &err);
                                if (!tex) continue;
                                int w = 0, h = 0;
                                const QByteArray px = re::textureRgba(*tex, 8, &w, &h);
                                double sum = 0;
                                for (int i = 0; i < w * h; ++i) sum += (uchar(px[i * 4]) + uchar(px[i * 4 + 1]) + uchar(px[i * 4 + 2])) / 3.0;
                                const double mean = w * h ? sum / (w * h) : 0;
                                const QVector4D tint = mt.baseColor;
                                if (mean * std::max({tint.x(), tint.y(), tint.z()}) > 150)
                                    out << "  bright " << int(mean) << " tint " << tint.x() << "," << tint.y() << "," << tint.z() << " " << mt.name << " ["
                                        << mt.shader.section(QLatin1Char('/'), -1) << "] " << tp.section(QLatin1Char('/'), -1) << " in "
                                        << it.key().section(QLatin1Char('|'), 0, 0).section(QLatin1Char('/'), -1) << "\n";
                            }
                    }
                }
                for (auto it = shaders.cbegin(); it != shaders.cend(); ++it)
                    out << "  shader " << it.key() << ": " << it->first << " materials, " << it->second << " without base colour" << (example.contains(it.key()) ? QStringLiteral(" (e.g. ") + example[it.key()] + QLatin1Char(')') : QString()) << "\n";
                for (const QString &f : failed.mid(0, 6)) out << "  failed " << f << "\n";
            }
            return 0;
        }
        if (a.value(4) == QLatin1String("--thumb")) {
            // recli model <game> <id> --thumb <out.png>
            const QImage img = src.thumbnail(a.value(3), 256, &err);
            if (img.isNull()) { out << "thumbnail failed: " << err << "\n"; return 1; }
            img.save(a.value(5));
            out << "thumbnail " << img.width() << "x" << img.height() << " in " << t.elapsed() << " ms\n";
            return 0;
        }
        const QString id = a.value(3);
        auto m = src.loadModel(id, &err);
        if (!m) { out << "model failed: " << err << "\n"; return 1; }
        out << m->name << ": " << m->pieces.size() << " pieces, " << m->skeleton.bones.size() << " bones in " << t.restart() << " ms\n";
        for (const auto &p : m->pieces)
            out << "  " << p.name << " " << p.mesh->path.section(QLatin1Char('/'), -1) << " verts " << p.mesh->vertexCount() << " mats " << p.materials.size()
                << " hidden groups " << p.hiddenGroups.size() << " parent '" << p.parentBone << "'\n";
        if (m->info.contains(QStringLiteral("problems"))) out << "problems: " << m->info.value(QStringLiteral("problems")).toStringList().join(QStringLiteral("; ")) << "\n";
        QSet<QString> texs;
        for (const auto &p : m->pieces) for (const auto &mt : p.materials) for (const auto &tp : mt.textures) texs.insert(tp);
        int ok = 0;
        QStringList failed;
        for (const QString &tp : texs) { if (src.loadTexture(tp, &err)) ++ok; else failed << tp + QStringLiteral(" (") + err + QStringLiteral(")"); }
        out << "textures " << ok << "/" << texs.size() << " in " << t.elapsed() << " ms\n";
        for (const QString &f : failed.mid(0, 5)) out << "  missing " << f << "\n";
        return 0;
    }
    out << "bad command\n";
    return 2;
}
