#include "Re4Cast.h"
#include "ReSource.h"
#include "Rsz.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace re {

namespace {
constexpr quint32 kDefaultUid = 160198385;   // the preset uid every character uses for its default look

struct Preset {
    quint32 uid = 0;
    QStringList parts;
    QHash<QString, QVector<int>> off;
    bool dlc = false, variant = false, synth = false;
    QString set = QStringLiteral("00");
    QString name;
};

QVector<Preset> parsePresets(ReSource &src, const QString &path)
{
    QVector<Preset> out;
    const RszTypes *types = src.rszTypes();
    if (!types) return out;
    RszDocument doc;
    if (!doc.parse(src.files().read(path), *types, nullptr)) return out;
    auto str = [&](int inst, const char *f) { return doc.field(inst, QLatin1String(f)); };
    auto ref = [](const QVariant &v) { return v.metaType() == QMetaType::fromType<RszRef>() ? v.value<RszRef>().index : -1; };
    for (int i = 1; i < doc.instances.size(); ++i) {
        if (doc.instances[i].typeName() != QLatin1String("chainsaw.CostumePresetUserData.Data")) continue;
        Preset p;
        p.uid = str(i, "_ID").toUInt();
        for (const QVariant &pdv : str(i, "_PrefabTable").toList()) {
            const int pd = ref(pdv);
            if (pd < 0) continue;
            const int pf = ref(doc.field(pd, QStringLiteral("_Prefab")));
            QString path = doc.field(pf, QStringLiteral("Path")).toString();
            path.replace(QLatin1Char('\\'), QLatin1Char('/'));
            if (path.isEmpty()) continue;
            p.parts << path;
            QVector<int> off;
            for (const QVariant &o : doc.field(pd, QStringLiteral("_InitialPartsOffList")).toList()) off << o.toInt();
            p.off.insert(path.toLower(), off);
        }
        if (!p.parts.isEmpty()) out << p;
    }
    return out;
}
} // namespace

QVector<Re4Character> re4Characters(ReSource &src, const QString &namesFile)
{
    static const QRegularExpression presetRx(QStringLiteral("^natives/stm/(_chainsaw|_anotherorder|_mercenaries)/appsystem/character/(ch[0-9a-z]+)/costume/([^/]*costumepresetuserdata[^/]*)\\.user\\.2$"),
                                             QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression partRx(QStringLiteral("/costume/(ch[a-z][0-9a-z])([0-9a-z]{2})_(\\d\\d)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression variantRx(QStringLiteral("_\\d\\d([a-z])(?:_|\\.pfb$)"), QRegularExpression::CaseInsensitiveOption);
    static const QHash<QString, QString> codeNames = {{QStringLiteral("cha0"), QStringLiteral("Leon")}, {QStringLiteral("cha1"), QStringLiteral("Ashley")},
                                                      {QStringLiteral("cha2"), QStringLiteral("Ada")}, {QStringLiteral("cha3"), QStringLiteral("Luis")},
                                                      {QStringLiteral("chb0"), QStringLiteral("Merchant")}};
    static const QHash<QString, QString> idNames = {
        {QStringLiteral("ch1c0z0"), QStringLiteral("Villager A")}, {QStringLiteral("ch1c0z1"), QStringLiteral("Villager B")},
        {QStringLiteral("ch1c0z2"), QStringLiteral("Villager C")}, {QStringLiteral("ch1c8z0"), QStringLiteral("Villager D")},
        {QStringLiteral("ch1d1z1"), QStringLiteral("Villager E")}, {QStringLiteral("ch0a1z0"), QStringLiteral("Ashley (playable)")},
        {QStringLiteral("ch2a1z0"), QStringLiteral("Ashley")}, {QStringLiteral("ch3a8z0"), QStringLiteral("Ada (Separate Ways)")},
        {QStringLiteral("ch0a0z0"), QStringLiteral("Leon")}, {QStringLiteral("ch6i0z0"), QStringLiteral("Leon (Mercenaries)")}};

    QJsonObject overrides;
    {
        QFile f(namesFile);
        if (f.open(QIODevice::ReadOnly)) overrides = QJsonDocument::fromJson(f.readAll()).object();
    }
    const QStringList &names = src.files().names();
    const int pfbVer = src.formatVersion(QStringLiteral("pfb"));
    const int mlVer = src.formatVersion(QStringLiteral("motlist"));
    QSet<QString> lower;
    lower.reserve(names.size());
    for (const QString &n : names) lower.insert(n.toLower());
    auto pfbExists = [&](const QString &p) { return lower.contains(QStringLiteral("natives/stm/%1.%2").arg(p.toLower()).arg(pfbVer)); };

    // motion lists named *_general*, for each character's everyday animations
    QStringList generals;
    const QRegularExpression genRx(QStringLiteral("/animation/ch/[^/]+/motlist/(?:[^/]+/)*[^/]+_general[^/]*\\.motlist\\.%1$").arg(mlVer), QRegularExpression::CaseInsensitiveOption);
    QMap<QString, QVector<QPair<QString, QString>>> files;     // cid -> (tree, path)
    for (const QString &n : names) {
        if (const auto m = presetRx.match(n); m.hasMatch()) files[m.captured(2).toLower()] << qMakePair(m.captured(1).toLower(), n);
        else if (genRx.match(n).hasMatch() && !n.contains(QLatin1String("/facial/"), Qt::CaseInsensitive)) generals << n;
    }
    auto findGeneral = [&](const QString &tree, const QString &code) -> QString {
        for (const QString &t : {tree, QStringLiteral("_chainsaw")}) {
            QStringList c;
            const QString needle = QStringLiteral("/%1/animation/ch/%2/motlist/").arg(t, code);
            for (const QString &g : generals) if (g.contains(needle, Qt::CaseInsensitive)) c << g;
            if (c.isEmpty()) continue;
            auto rank = [&](const QString &p) {
                const QString b = p.section(QLatin1Char('/'), -1).toLower();
                const int r = b == QStringLiteral("%1_general.motlist.%2").arg(code).arg(mlVer) ? 0 : b.startsWith(code + QStringLiteral("_general_0th_com")) ? 1 : b.contains(QLatin1String("_0th_")) ? 2 : 3;
                return r * 1000 + int(b.size());
            };
            std::sort(c.begin(), c.end(), [&](const QString &a, const QString &b) { return rank(a) < rank(b); });
            return c.first();
        }
        return {};
    };

    QVector<Re4Character> out;
    for (auto it = files.begin(); it != files.end(); ++it) {
        const QString cid = it.key();
        auto entries = it.value();
        std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
            const auto key = [](const QPair<QString, QString> &e) {
                return std::make_tuple(e.first != QLatin1String("_chainsaw"), e.second.contains(QLatin1String("_add"), Qt::CaseInsensitive), e.second.size());
            };
            return key(a) < key(b);
        });
        QString tree = entries.first().first;
        for (const auto &e : entries) if (e.first == QLatin1String("_chainsaw")) { tree = e.first; break; }

        QVector<Preset> presets;
        QSet<QString> seen;
        for (const auto &e : entries) {
            for (Preset p : parsePresets(src, e.second)) {
                QStringList key = p.parts;
                for (QString &k : key) k = k.toLower();
                key.sort();
                const QString k = key.join(QLatin1Char('|'));
                if (seen.contains(k)) continue;
                seen.insert(k);
                if (!std::all_of(p.parts.cbegin(), p.parts.cend(), pfbExists)) continue;
                p.dlc = e.first != QLatin1String("_chainsaw");
                p.variant = std::any_of(p.parts.cbegin(), p.parts.cend(), [&](const QString &x) { return variantRx.match(x).hasMatch(); });
                presets << p;
            }
        }
        if (presets.isEmpty()) continue;
        auto bodyOf = [&](const Preset &p) -> QString {
            for (const QString &x : p.parts) if (const auto m = partRx.match(x); m.hasMatch() && m.captured(3) == QLatin1String("00")) return x;
            return {};
        };
        std::stable_sort(presets.begin(), presets.end(), [](const Preset &a, const Preset &b) { return std::make_pair(a.variant, a.dlc) < std::make_pair(b.variant, b.dlc); });
        const QString body = bodyOf(presets.first()).isEmpty() ? presets.first().parts.first() : bodyOf(presets.first());
        const auto pm = partRx.match(body);
        const QString code = pm.hasMatch() ? pm.captured(1).toLower() : QStringLiteral("ch") + cid.mid(3, 2);
        QString baseName = idNames.value(cid, codeNames.value(code, code.toUpper()));
        for (Preset &p : presets) {
            const QString b = bodyOf(p);
            const auto sm = partRx.match(b);
            p.set = sm.hasMatch() ? sm.captured(2) : QStringLiteral("00");
        }
        // sets that exist as files but are in no table: synthesise a look so they stay one click away
        QMap<QString, QMap<QString, QString>> setParts;
        const QRegularExpression setRx(QStringLiteral("^natives/stm/%1/appsystem/character/%2/costume/(%3(\\d\\d)_(\\d\\d))\\.pfb\\.\\d+$").arg(tree, cid, code),
                                       QRegularExpression::CaseInsensitiveOption);
        for (const QString &n : src.files().list(QStringLiteral("natives/stm/%1/appsystem/character/%2/costume/").arg(tree, cid))) {
            if (const auto m = setRx.match(n); m.hasMatch()) setParts[m.captured(2)][m.captured(3)] = n.mid(12, n.lastIndexOf(QLatin1Char('.')) - 12);
        }
        QSet<QString> haveSets;
        for (const Preset &p : presets) if (!p.variant) haveSets.insert(p.set);
        for (auto sp = setParts.cbegin(); sp != setParts.cend(); ++sp) {
            if (haveSets.contains(sp.key()) || !sp->contains(QStringLiteral("00")) || !sp->contains(QStringLiteral("10"))) continue;
            Preset p;
            for (const char *k : {"00", "01", "10", "20"}) if (sp->contains(QLatin1String(k))) p.parts << sp->value(QLatin1String(k));
            p.dlc = tree != QLatin1String("_chainsaw");
            p.synth = true;
            p.set = sp.key();
            presets << p;
        }
        std::stable_sort(presets.begin(), presets.end(), [](const Preset &a, const Preset &b) {
            return std::make_tuple(a.variant, a.synth, a.dlc, a.uid != kDefaultUid) < std::make_tuple(b.variant, b.synth, b.dlc, b.uid != kDefaultUid);
        });
        // names
        const bool generic = baseName == code.toUpper() || baseName.startsWith(QLatin1String("Villager"));
        const Preset *firstClean = nullptr;
        for (const Preset &p : presets) if (!p.variant && !p.synth) { firstClean = &p; break; }
        const bool anyDefault = std::any_of(presets.cbegin(), presets.cend(), [](const Preset &p) { return p.uid == kDefaultUid; });
        int counter = 0;
        for (Preset &p : presets) {
            if (generic) p.name = QStringLiteral("%1 %2").arg(baseName).arg(++counter);
            else if (p.uid == kDefaultUid || (&p == firstClean && !anyDefault)) p.name = QStringLiteral("Default");
            else p.name = QStringLiteral("Costume %1").arg(p.set);
            if (p.variant) {
                QStringList letters;
                for (const QString &x : p.parts) if (const auto m = variantRx.match(x); m.hasMatch() && !letters.contains(m.captured(1).toLower())) letters << m.captured(1).toLower();
                letters.sort();
                p.name += QStringLiteral(" · damaged ") + letters.join(QLatin1Char('/'));
            }
            if (p.synth) p.name += QStringLiteral(" (files)");
        }
        QString group;
        const QJsonObject ov = overrides.value(cid).toObject();
        if (!ov.isEmpty()) {
            baseName = ov.value(QLatin1String("name")).toString(baseName);
            group = ov.value(QLatin1String("group")).toString();
            const QJsonObject looks = ov.value(QLatin1String("looks")).toObject();
            if (!looks.isEmpty()) {
                int n = 0;
                for (Preset &p : presets) {
                    if (p.variant) continue;
                    ++n;
                    p.name = looks.value(QString::number(n)).toString(QStringLiteral("%1 %2").arg(baseName).arg(n));
                }
                for (Preset &p : presets) {
                    if (!p.variant) continue;
                    const Preset *twin = nullptr;
                    for (const Preset &q : presets) if (!q.variant && q.set == p.set) { twin = &q; break; }
                    p.name = (twin ? twin->name : baseName) + QStringLiteral(" · damaged");
                }
            }
        }
        QHash<QString, int> dup, idx;
        for (const Preset &p : presets) dup[p.name]++;
        for (Preset &p : presets) if (dup[p.name] > 1) p.name = QStringLiteral("%1 (%2)").arg(p.name).arg(++idx[p.name]);

        Re4Character c;
        c.id = cid;
        c.code = code;
        c.tree = tree;
        c.name = baseName;
        c.group = group;
        c.general = findGeneral(tree, code);
        if (c.general.isEmpty() && code == QLatin1String("cha2")) {
            c.general = findGeneral(QStringLiteral("_mercenaries"), QStringLiteral("cha8"));
            if (c.general.isEmpty()) c.general = findGeneral(QStringLiteral("_anotherorder"), QStringLiteral("cha8"));
        }
        if (c.general.isEmpty() && (code.startsWith(QLatin1String("cha")) || code.startsWith(QLatin1String("chb")))) c.general = findGeneral(QStringLiteral("_chainsaw"), QStringLiteral("cha0"));
        for (const Preset &p : presets) {
            Re4Look l;
            l.name = p.name;
            l.prefabs = p.parts;
            l.hiddenParts = p.off;
            l.variant = p.variant;
            l.dlc = p.dlc;
            c.looks << l;
        }
        out << c;
    }
    return out;
}

} // namespace re
