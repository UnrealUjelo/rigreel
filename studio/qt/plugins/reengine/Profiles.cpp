#include "Profiles.h"

#include <QDir>
#include <QFileInfo>

namespace re {

const QVector<GameProfile> &profiles()
{
    static const QVector<GameProfile> list = {
        {QStringLiteral("re4"), QStringLiteral("Resident Evil 4"), 2050650, QStringLiteral("re4.exe"), QStringLiteral("RE4_STM.list"), QStringLiteral("rszre4.json"), QStringLiteral("full")},
        {QStringLiteral("re2rt"), QStringLiteral("Resident Evil 2"), 883710, QStringLiteral("re2.exe"), QStringLiteral("RE2_RT_STM.list"), QStringLiteral("rszre2rt.json"), QStringLiteral("untested")},
        {QStringLiteral("re3rt"), QStringLiteral("Resident Evil 3"), 952060, QStringLiteral("re3.exe"), QStringLiteral("RE3_RT_STM.list"), QStringLiteral("rszre3rt.json"), QStringLiteral("untested")},
        {QStringLiteral("re7rt"), QStringLiteral("Resident Evil 7 Biohazard"), 418370, QStringLiteral("re7.exe"), QStringLiteral("RE7_RT_STM.list"), QStringLiteral("rszre7rt.json"), QStringLiteral("untested")},
        {QStringLiteral("re8"), QStringLiteral("Resident Evil Village"), 1196590, QStringLiteral("re8.exe"), QStringLiteral("RE8_STM.list"), QStringLiteral("rszre8.json"), QStringLiteral("untested")},
        {QStringLiteral("re9"), QStringLiteral("Resident Evil Requiem"), 3764200, QStringLiteral("re9.exe"), QStringLiteral("RE9_STM.list"), QStringLiteral("rszre9.json"), QStringLiteral("untested")},
        {QStringLiteral("dmc5"), QStringLiteral("Devil May Cry 5"), 601150, QStringLiteral("DevilMayCry5.exe"), QStringLiteral("DMC5_STM.list"), QStringLiteral("rszdmc5.json"), QStringLiteral("untested")},
        {QStringLiteral("sf6"), QStringLiteral("Street Fighter 6"), 1364780, QStringLiteral("StreetFighter6.exe"), QStringLiteral("SF6_STM.list"), QStringLiteral("rszsf6.json"), QStringLiteral("untested")},
        {QStringLiteral("dd2"), QStringLiteral("Dragon's Dogma 2"), 2054970, QStringLiteral("DD2.exe"), QStringLiteral("DD2_STM.list"), QStringLiteral("rszdd2.json"), QStringLiteral("untested")},
        {QStringLiteral("mhrise"), QStringLiteral("Monster Hunter Rise"), 1446780, QStringLiteral("MonsterHunterRise.exe"), QStringLiteral("MHR_STM.list"), QStringLiteral("rszmhrise.json"), QStringLiteral("untested")},
        {QStringLiteral("mhwilds"), QStringLiteral("Monster Hunter Wilds"), 2246340, QStringLiteral("MonsterHunterWilds.exe"), QStringLiteral("MHWS_STM.list"), QStringLiteral("rszmhwilds.json"), QStringLiteral("untested")},
        {QStringLiteral("oniwots"), QStringLiteral("Onimusha: Way of the Sword"), 2596400, QStringLiteral("Onimusha.exe"), QStringLiteral("ONIWOTS_STM.list"), QStringLiteral("rszoniwots.json"), QStringLiteral("untested")},
        {QStringLiteral("kunitsugami"), QStringLiteral("Kunitsu-Gami: Path of the Goddess"), 2510710, QStringLiteral("KunitsuGami.exe"), QStringLiteral("KUNITSUGAMI_STM.list"), QStringLiteral("rszkunitsugami.json"), QStringLiteral("untested")},
    };
    return list;
}

const GameProfile *profileById(const QString &id)
{
    for (const GameProfile &p : profiles())
        if (p.id == id) return &p;
    return nullptr;
}

const GameProfile *profileByExe(const QString &folder)
{
    const QDir d(folder);
    for (const GameProfile &p : profiles())
        if (QFileInfo::exists(d.filePath(p.exe))) return &p;
    return nullptr;
}

} // namespace re
