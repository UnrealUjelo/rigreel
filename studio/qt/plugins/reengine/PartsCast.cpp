#include "PartsCast.h"
#include "ReSource.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <algorithm>

namespace re {

namespace {
QString stemOf(const QString &p) { return p.section(QLatin1Char('/'), -1).section(QLatin1Char('.'), 0, 0); }

// "costume_3" -> "Costume 3", "default" -> "Default"
QString costumeLabel(const QString &suffix)
{
    if (suffix == QLatin1String("default")) return QStringLiteral("Default");
    QString s = suffix;
    s.replace(QLatin1Char('_'), QLatin1Char(' '));
    s[0] = s[0].toUpper();
    return s;
}
} // namespace

QVector<Re4Character> partsCharacters(ReSource &src, const QString &namesFile)
{
    QJsonObject names;
    if (QFile f(namesFile); f.open(QIODevice::ReadOnly)) names = QJsonDocument::fromJson(f.readAll()).object();
    auto nameOf = [&](const QString &id, const QString &fallback, const QString &group) {
        const QJsonObject n = names.value(id).toObject();
        return qMakePair(n.value(QStringLiteral("name")).toString(fallback), n.value(QStringLiteral("group")).toString(group));
    };
    const QString natives = src.resolve(QStringLiteral("x"), QStringLiteral("pfb")).section(QLatin1Char('/'), 0, 1) + QLatin1Char('/');   // natives/stm/
    const QString pfb = QStringLiteral(".pfb.%1").arg(src.formatVersion(QStringLiteral("pfb")));
    const QString mesh = QStringLiteral(".mesh.%1").arg(src.formatVersion(QStringLiteral("mesh")));
    const QString motlist = QStringLiteral(".motlist.%1").arg(src.formatVersion(QStringLiteral("motlist")));
    QVector<Re4Character> out;

    // ---- survivors: part prefabs per costume
    static const QRegularExpression partRx(QStringLiteral("/parts/(pl\\d+)/\\1_(body|face|hair|other)_(default|costume_\\d+)\\.pfb"), QRegularExpression::CaseInsensitiveOption);
    QMap<QString, QMap<QString, QMap<QString, QString>>> parts;     // code -> costume -> part -> prefab path
    for (const QString &p : src.listFiles(natives + QStringLiteral("objectroot/prefab/character/survivor/parts/"), pfb, 0)) {
        const auto m = partRx.match(p);
        if (m.hasMatch()) parts[m.captured(1).toLower()][m.captured(3).toLower()][m.captured(2).toLower()] = p;
    }
    for (auto c = parts.cbegin(); c != parts.cend(); ++c) {
        Re4Character ch;
        ch.id = c.key();
        ch.code = c.key().left(4);                            // pl0000 -> pl00: its animation folder
        const auto [name, group] = nameOf(ch.id, QStringLiteral("Survivor %1").arg(ch.id), QStringLiteral("People"));
        ch.name = name;
        ch.group = group;
        // everyday movement (idle / walk / jog) lives in the character's base move list
        const QStringList moves = src.listFiles(natives + QStringLiteral("sectionroot/animation/player/%1/list/cmn/base_cmn_move").arg(ch.code), motlist, 1);
        ch.general = moves.value(0);
        const QMap<QString, QString> fallback = c->value(QStringLiteral("default"));
        QStringList costumes = c->keys();
        std::sort(costumes.begin(), costumes.end(), [](const QString &a, const QString &b) {
            if (a == QLatin1String("default")) return b != QLatin1String("default");
            if (b == QLatin1String("default")) return false;
            return a.section(QLatin1Char('_'), 1).toInt() < b.section(QLatin1Char('_'), 1).toInt();
        });
        for (const QString &cos : costumes) {
            Re4Look look;
            look.name = nameOf(ch.id + QLatin1Char('/') + cos, costumeLabel(cos), {}).first;
            for (const QString &part : {QStringLiteral("body"), QStringLiteral("face"), QStringLiteral("hair"), QStringLiteral("other")}) {
                const QString p = c->value(cos).value(part, fallback.value(part));
                if (!p.isEmpty()) look.prefabs << p;
            }
            if (!look.prefabs.isEmpty()) ch.looks << look;
        }
        if (!ch.looks.isEmpty()) out << ch;
    }

    // ---- zombies: body + face + shirt + pants from the mesh folders, paired up into a handful of looks
    static const QRegularExpression zRx(QStringLiteral("/character/enemy/(em0[0-2]00)/(body|face|shirt|pants|hat)/[a-z]+\\d+/[^/]+\\.mesh"), QRegularExpression::CaseInsensitiveOption);
    QMap<QString, QMap<QString, QStringList>> zparts;               // code -> part -> meshes
    for (const QString &p : src.listFiles(natives + QStringLiteral("sectionroot/character/enemy/"), mesh, 0)) {
        const auto m = zRx.match(p);
        if (!m.hasMatch() || p.contains(QLatin1String("internal"), Qt::CaseInsensitive)) continue;
        zparts[m.captured(1).toLower()][m.captured(2).toLower()] << p;
    }
    for (auto z = zparts.cbegin(); z != zparts.cend(); ++z) {
        const QStringList body = z->value(QStringLiteral("body")), faces = z->value(QStringLiteral("face")), shirts = z->value(QStringLiteral("shirt")),
                          pants = z->value(QStringLiteral("pants"));
        if (body.isEmpty()) continue;
        Re4Character ch;
        ch.id = z.key();
        ch.code = z.key();
        const auto [name, group] = nameOf(ch.id, QStringLiteral("Zombie %1").arg(ch.id), QStringLiteral("Zombies"));
        ch.name = name;
        ch.group = group;
        const int n = std::min<int>(12, std::max({int(faces.size()), int(shirts.size()), int(pants.size()), 1}));
        for (int i = 0; i < n; ++i) {
            Re4Look look;
            look.name = QStringLiteral("Look %1").arg(i + 1);
            look.meshes << body.first();
            if (!faces.isEmpty()) look.meshes << faces[i % faces.size()];
            if (!shirts.isEmpty()) look.meshes << shirts[i % shirts.size()];
            if (!pants.isEmpty()) look.meshes << pants[i % pants.size()];
            ch.looks << look;
        }
        out << ch;
    }

    // ---- creatures: whole prefabs
    static const QRegularExpression eRx(QStringLiteral("/prefab/character/enemy/(em\\d+)\\.pfb"), QRegularExpression::CaseInsensitiveOption);
    for (const QString &p : src.listFiles(natives + QStringLiteral("objectroot/prefab/character/enemy/"), pfb, 0)) {
        const auto m = eRx.match(p);
        if (!m.hasMatch() || zparts.contains(m.captured(1).toLower())) continue;
        Re4Character ch;
        ch.id = m.captured(1).toLower();
        ch.code = ch.id;
        const auto [name, group] = nameOf(ch.id, QStringLiteral("Creature %1").arg(ch.id), QStringLiteral("Creatures & bosses"));
        ch.name = name;
        ch.group = group;
        Re4Look look;
        look.name = QStringLiteral("Default");
        look.prefabs << p;
        ch.looks << look;
        out << ch;
    }
    return out;
}

} // namespace re
