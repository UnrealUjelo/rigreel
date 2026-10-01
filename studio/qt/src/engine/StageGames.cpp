// Several games in one film (RE2's Leon on an RE4 map playing RE4 clips). Every asset reference carries its game:
// "re2rt::look:pl0000/0", "re4::natives/stm/_chainsaw/animation/...motlist.663". A reference without one belongs
// to the film's own game (films made before). Games open on demand and stay open; the Asset Browser lists one.
#include "GameLibrary.h"
#include "StageRuntime.h"

#include <QFutureWatcher>
#include <QRegularExpression>
#include <QtConcurrent>
#include <utility>

using namespace film;

QString StageRuntime::filmGame() const
{
    QString g = m_doc.gameKey;
    if (g.contains(QLatin1Char(':'))) g = g.section(QLatin1Char(':'), 1, 1);   // an older film kept plugin:game:folder
    if (g.isEmpty() && m_games) g = m_games->gameId();
    return g;
}

QString StageRuntime::browseGame() const
{
    return m_games ? m_games->gameId() : QString();
}

QString StageRuntime::refGame(const QString &ref) const
{
    const int i = ref.indexOf(QLatin1String("::"));
    return i > 0 ? ref.left(i) : filmGame();
}

QString StageRuntime::refLocal(const QString &ref)
{
    const int i = ref.indexOf(QLatin1String("::"));
    return i > 0 ? ref.mid(i + 2) : ref;
}

QString StageRuntime::qualify(const QString &gameId, const QString &local)
{
    if (local.isEmpty() || gameId.isEmpty() || local.contains(QLatin1String("::"))) return local;
    return gameId + QLatin1String("::") + local;
}

dir::IGameSource *StageRuntime::sourceOf(const QString &ref) const
{
    return m_games ? m_games->sourceForId(refGame(ref)) : nullptr;
}

// films saved before games were named in references: everything is the film's own game
void StageRuntime::qualifyDocument(Document &d) const
{
    QString g = d.gameKey;
    if (g.contains(QLatin1Char(':'))) g = g.section(QLatin1Char(':'), 1, 1);
    if (g.isEmpty() && m_games) g = m_games->gameId();
    for (Actor &a : d.actors) {
        a.model = qualify(g, a.model);
        a.idlePath = qualify(g, a.idlePath);
    }
    for (Track &tr : d.seq.tracks)
        for (AnimClip &c : tr.clips) c.path = qualify(g, c.path);
    d.stageId = qualify(g, d.stageId);
}

// the owner of a motion list: animation/ch/<code>/ (RE4) or animation/player|enemy|npc/<code>/ (RE2)
QString StageRuntime::ownerOf(const QString &ref) const
{
    static const QRegularExpression rx(QStringLiteral("/animation/(?:ch|player|enemy|npc)/([a-z0-9]+)/"), QRegularExpression::CaseInsensitiveOption);
    const auto m = rx.match(refLocal(ref));
    return m.hasMatch() ? m.captured(1).toLower() : QString();
}

// a game's characters without listing them in the Asset Browser: its looks (rig skeletons) and general lists
void StageRuntime::ensureGameCast(const QString &gameId)
{
    if (gameId.isEmpty() || m_castByGame.contains(gameId) || m_castLoading.contains(gameId)) return;
    dir::IGameSource *src = m_games ? m_games->sourceForId(gameId) : nullptr;
    if (!src) return;                                              // opening: sourceOpened asks again
    m_castLoading.insert(gameId);
    auto *w = new QFutureWatcher<QList<dir::CatalogEntry>>(this);
    connect(w, &QFutureWatcherBase::finished, this, [this, w, gameId]() {
        const QList<dir::CatalogEntry> chars = w->result();
        w->deleteLater();
        m_castLoading.remove(gameId);
        storeCast(gameId, chars);
        m_bindings.clear();
        if (!m_playing) evaluate(m_t);
        publish();
    });
    w->setFuture(QtConcurrent::run([src]() { return src->catalog(dir::AssetKind::Character); }));
}

// characters in the Lua catalog's shape (cast.json): one entry per character, its looks as presets, every
// reference qualified with the game; the first look of each owner code is that code's rig skeleton
QJsonArray StageRuntime::storeCast(const QString &gameId, const QList<dir::CatalogEntry> &chars)
{
    QMap<QString, QJsonObject> byChar;
    QStringList order;
    for (const dir::CatalogEntry &e : chars) {
        const QString cid = e.extra.value(QStringLiteral("character"), e.id).toString();
        if (!byChar.contains(cid)) {
            order << cid;
            byChar.insert(cid, QJsonObject{{QStringLiteral("id"), cid}, {QStringLiteral("code"), e.extra.value(QStringLiteral("code")).toString()},
                                           {QStringLiteral("tree"), e.extra.value(QStringLiteral("tree")).toString()},
                                           {QStringLiteral("name"), e.extra.value(QStringLiteral("characterName"), e.group.isEmpty() ? e.name : e.group).toString()},
                                           {QStringLiteral("group"), e.extra.value(QStringLiteral("characterGroup")).toString()},
                                           {QStringLiteral("general"), qualify(gameId, e.extra.value(QStringLiteral("general")).toString())},
                                           {QStringLiteral("game"), gameId},
                                           {QStringLiteral("dlc"), e.tags.contains(QStringLiteral("dlc"))}, {QStringLiteral("skel"), true},
                                           {QStringLiteral("presets"), QJsonArray()}});
        }
        QJsonObject ch = byChar.value(cid);
        QJsonArray presets = ch.value(QStringLiteral("presets")).toArray();
        presets.append(QJsonObject{{QStringLiteral("name"), e.name}, {QStringLiteral("model"), qualify(gameId, e.id)},
                                   {QStringLiteral("variant"), e.tags.contains(QStringLiteral("damaged"))}, {QStringLiteral("dlc"), e.tags.contains(QStringLiteral("dlc"))}});
        ch.insert(QStringLiteral("presets"), presets);
        if (!e.tags.contains(QStringLiteral("dlc"))) ch.insert(QStringLiteral("dlc"), false);
        byChar.insert(cid, ch);
    }
    QJsonArray cast;
    for (const QString &cid : order) {
        const QJsonObject ch = byChar.value(cid);
        cast.append(ch);
        const QString code = ch.value(QStringLiteral("code")).toString();
        const QJsonArray looks = ch.value(QStringLiteral("presets")).toArray();
        const QString rig = qualify(gameId, code);
        if (!code.isEmpty() && !looks.isEmpty() && !m_rigModel.contains(rig)) m_rigModel.insert(rig, looks.first().toObject().value(QStringLiteral("model")).toString());
    }
    m_castByGame.insert(gameId, cast);
    return cast;
}

QJsonArray StageRuntime::castOf(const Actor &a) const
{
    return m_castByGame.value(refGame(a.model));
}

// the game's cast entry of an actor (its looks, its general list)
QJsonObject StageRuntime::castEntry(const Actor &a) const
{
    for (const QJsonValue &v : castOf(a)) {
        const QJsonObject ch = v.toObject();
        if (ch.value(QStringLiteral("id")).toString() == a.castId) return ch;
    }
    return {};
}

// a game the film uses finished opening: what waited for it loads now
void StageRuntime::sourceOpened(const QString &gameId)
{
    Q_UNUSED(gameId);
    for (const Actor &a : std::as_const(m_doc.actors)) ensureModel(a.id);
    const QSet<QString> waiting = std::exchange(m_animsWaiting, {});
    for (const QString &p : waiting) ensureAnimations(p);
    ensureStage();
    m_bindings.clear();
    if (!m_playing) evaluate(m_t);
    publish();
}
