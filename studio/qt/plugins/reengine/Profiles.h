// The RE Engine games this plugin knows. Format versions are not listed here: every game carries them in its
// own file names, so ReSource reads them from the name list (most common ".mesh.<n>", ".tex.<n>" ...).
#pragma once

#include <QString>
#include <QVector>

namespace re {

struct GameProfile {
    QString id;            // "re4"
    QString title;
    int steamAppId = 0;
    QString exe;           // main executable
    QString listName;      // name list file (REasy's naming: RE4_STM.list)
    QString rszName;       // RSZ type dump (REasy's naming: rszre4.json)
    QString support;       // "full" | "partial" | "untested"
};

const QVector<GameProfile> &profiles();
const GameProfile *profileById(const QString &id);
const GameProfile *profileByExe(const QString &folder);   // by the exe found in a folder

} // namespace re
