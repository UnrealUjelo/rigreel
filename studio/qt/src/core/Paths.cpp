#include "Paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace Paths {

QString gameDir()
{
    QString env = qEnvironmentVariable("RIGREEL_GAME_DIR");
    if (env.isEmpty())
        env = qEnvironmentVariable("DIRECTOR_GAME_DIR"); // legacy name
    if (!env.isEmpty())
        return QDir::cleanPath(env);
    return QStringLiteral("C:/Program Files (x86)/Steam/steamapps/common/RESIDENT EVIL 4  BIOHAZARD RE4");
}

QString directorDir() { return gameDir() + QStringLiteral("/reframework/data/director"); }
QString bridgeDir() { return directorDir() + QStringLiteral("/bridge"); }

QString workspace()
{
    static QString cached;
    if (!cached.isEmpty())
        return cached;
    QString env = qEnvironmentVariable("RIGREEL_HOME");
    if (env.isEmpty())
        env = qEnvironmentVariable("DIRECTOR_HOME"); // legacy name
    if (!env.isEmpty() && QFileInfo::exists(env + QStringLiteral("/mod/Director")))
        return cached = QDir::cleanPath(env);
    // A source build or portable checkout sits below the workspace root. Walk up until the live runtime appears.
    QDir d(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        if (d.exists(QStringLiteral("mod/Director")))
            return cached = d.absolutePath();
        if (!d.cdUp())
            break;
    }
    // Standalone packaged builds may not include the live runtime or development tools.
    return cached = QCoreApplication::applicationDirPath();
}

QString python()
{
    const QString configured = qEnvironmentVariable("RIGREEL_PYTHON");
    if (!configured.isEmpty())
        return QDir::cleanPath(configured);
    const QString bundled = workspace() + QStringLiteral("/tools/venv/Scripts/python.exe");
    return QFileInfo::exists(bundled) ? bundled : QStandardPaths::findExecutable(QStringLiteral("python"));
}

QString downloads()
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    return d.isEmpty() ? QDir::homePath() + QStringLiteral("/Downloads") : d;
}

QString findBlender()
{
    const QString configured = qEnvironmentVariable("RIGREEL_BLENDER");
    if (!configured.isEmpty() && QFileInfo::exists(configured))
        return QDir::cleanPath(configured);
    QStringList hits;
    QDir pf(QStringLiteral("C:/Program Files/Blender Foundation"));
    for (const QString &sub : pf.entryList(QStringList() << QStringLiteral("Blender*"), QDir::Dirs, QDir::Name | QDir::Reversed)) {
        const QString exe = pf.absoluteFilePath(sub + QStringLiteral("/blender.exe"));
        if (QFileInfo::exists(exe))
            hits << exe;
    }
    if (!hits.isEmpty())
        return hits.first();
    const QString steam = QStringLiteral("C:/Program Files (x86)/Steam/steamapps/common/Blender/blender.exe");
    if (QFileInfo::exists(steam))
        return steam;
    return QStandardPaths::findExecutable(QStringLiteral("blender"));
}

QString ffmpeg()
{
    const QString configured = qEnvironmentVariable("RIGREEL_FFMPEG");
    return configured.isEmpty() ? QStandardPaths::findExecutable(QStringLiteral("ffmpeg")) : QDir::cleanPath(configured);
}

QString settingsFile()
{
    // Keep existing source-checkout settings compatible with Director Studio builds.
    const QString checkout = workspace() + QStringLiteral("/studio/qt/DirectorStudio.ini");
    if (QFileInfo::exists(checkout) || QFileInfo::exists(workspace() + QStringLiteral("/studio/qt")))
        return checkout;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/RigReelStudio.ini");
}

} // namespace Paths
