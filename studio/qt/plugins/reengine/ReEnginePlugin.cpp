#include "ReEnginePlugin.h"
#include "Profiles.h"
#include "ReSource.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

static dir::GameInfo infoFor(const re::GameProfile &p)
{
    dir::GameInfo g;
    g.pluginId = QStringLiteral("reengine");
    g.gameId = p.id;
    g.title = p.title;
    g.executable = p.exe;
    g.steamAppId = p.steamAppId;
    g.support = p.support;
    return g;
}

QList<dir::GameInfo> ReEnginePlugin::knownGames() const
{
    QList<dir::GameInfo> out;
    for (const re::GameProfile &p : re::profiles()) out << infoFor(p);
    return out;
}

dir::GameInfo ReEnginePlugin::probe(const QString &folder)
{
    const re::GameProfile *p = re::profileByExe(folder);
    if (!p) {
        dir::GameInfo g;
        g.pluginId = id();
        g.folder = folder;
        g.note = QStringLiteral("no RE Engine game found in this folder");
        return g;
    }
    dir::GameInfo g = infoFor(*p);
    g.folder = QDir(folder).absolutePath();
    g.installed = true;
    return g;
}

QList<dir::GameInfo> ReEnginePlugin::detect(const QStringList &steamLibraries)
{
    QList<dir::GameInfo> out;
    for (const QString &lib : steamLibraries) {
        const QDir common(lib + QStringLiteral("/steamapps/common"));
        for (const QString &sub : common.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const dir::GameInfo g = probe(common.filePath(sub));
            if (g.installed) out << g;
        }
    }
    return out;
}

dir::IGameSource *ReEnginePlugin::open(const dir::GameInfo &game, QString *error)
{
    const re::GameProfile *p = re::profileById(game.gameId);
    if (!p) p = re::profileByExe(game.folder);
    if (!p) { if (error) *error = QStringLiteral("%1 is not an RE Engine game this plugin knows").arg(game.title); return nullptr; }
    const QString dataDir = QCoreApplication::applicationDirPath() + QStringLiteral("/plugins/games/reengine");
    auto *src = new re::ReSource(game, *p, dataDir);
    if (!src->open(error)) { delete src; return nullptr; }
    return src;
}
