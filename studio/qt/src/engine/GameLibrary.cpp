#include "GameLibrary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPluginLoader>
#include <QPointer>
#include <QRegularExpression>
#include <QSettings>
#include <QtConcurrent>

GameLibrary::GameLibrary(QObject *parent) : QObject(parent)
{
    QSettings s;
    m_customFolders = s.value(QStringLiteral("games/folders")).toStringList();
}

GameLibrary::~GameLibrary() = default;

void GameLibrary::loadPlugins(const QString &dir)
{
    const QDir d(dir);
    for (const QString &f : d.entryList({QStringLiteral("*.dll")}, QDir::Files)) {
        QPluginLoader loader(d.filePath(f));
        QObject *inst = loader.instance();
        auto *p = qobject_cast<dir::IGamePlugin *>(inst);
        if (!p) {
            qWarning().noquote() << "game plugin" << f << "not loaded:" << loader.errorString();
            continue;
        }
        m_plugins << p;
        m_pluginFiles << f;
        qInfo().noquote() << "game plugin" << p->name() << p->version() << "from" << f;
    }
}

QStringList GameLibrary::steamLibraries()
{
    QStringList roots;
    QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"), QSettings::NativeFormat);
    const QString steam = QDir::fromNativeSeparators(reg.value(QStringLiteral("SteamPath")).toString());
    for (const QString &r : {steam, QStringLiteral("C:/Program Files (x86)/Steam"), QStringLiteral("C:/Program Files/Steam")})
        if (!r.isEmpty() && QFileInfo::exists(r + QStringLiteral("/steamapps")) && !roots.contains(r, Qt::CaseInsensitive)) roots << r;
    QStringList libs = roots;
    for (const QString &r : roots) {
        QFile f(r + QStringLiteral("/steamapps/libraryfolders.vdf"));
        if (!f.open(QIODevice::ReadOnly)) continue;
        static const QRegularExpression rx(QStringLiteral("\"path\"\\s+\"([^\"]+)\""));
        auto it = rx.globalMatch(QString::fromUtf8(f.readAll()));
        while (it.hasNext()) {
            const QString p = QDir::fromNativeSeparators(it.next().captured(1).replace(QStringLiteral("\\\\"), QStringLiteral("\\")));
            if (!libs.contains(p, Qt::CaseInsensitive)) libs << p;
        }
    }
    return libs;
}

void GameLibrary::refresh()
{
    m_games.clear();
    const QStringList libs = steamLibraries();
    for (dir::IGamePlugin *p : m_plugins) {
        QList<dir::GameInfo> found = p->detect(libs);
        for (const QString &f : m_customFolders) {
            const dir::GameInfo g = p->probe(f);
            if (g.installed && std::none_of(found.cbegin(), found.cend(), [&](const dir::GameInfo &x) { return keyOf(x) == keyOf(g); })) found << g;
        }
        m_games += found;
    }
    emit gamesChanged();
}

bool GameLibrary::addFolder(const QString &folder)
{
    const QString f = QDir(folder).absolutePath();
    for (dir::IGamePlugin *p : m_plugins) {
        if (!p->probe(f).installed) continue;
        if (!m_customFolders.contains(f, Qt::CaseInsensitive)) {
            m_customFolders << f;
            QSettings().setValue(QStringLiteral("games/folders"), m_customFolders);
        }
        refresh();
        return true;
    }
    setStatus(QStringLiteral("No supported game in %1").arg(f));
    return false;
}

void GameLibrary::removeFolder(const QString &folder)
{
    m_customFolders.removeAll(folder);
    QSettings().setValue(QStringLiteral("games/folders"), m_customFolders);
    refresh();
}

dir::IGamePlugin *GameLibrary::pluginFor(const QString &id) const
{
    for (dir::IGamePlugin *p : m_plugins)
        if (p->id() == id) return p;
    return nullptr;
}

void GameLibrary::activate(const QString &key)
{
    auto it = std::find_if(m_games.cbegin(), m_games.cend(), [&](const dir::GameInfo &g) { return keyOf(g) == key; });
    if (it == m_games.cend()) { setStatus(QStringLiteral("That game is not installed")); return; }
    dir::IGamePlugin *plugin = pluginFor(it->pluginId);
    if (!plugin) return;
    const dir::GameInfo game = *it;
    // open already (the film uses it): switch at once
    {
        QMutexLocker lock(&m_poolMutex);
        if (const auto open = m_pool.value(key)) {
            m_source = open;
            m_active = open->info();
            m_activeKey = key;
            lock.unlock();
            QSettings().setValue(QStringLiteral("games/active"), m_activeKey);
            setStatus(QStringLiteral("%1 \u00b7 %2").arg(m_active.title, m_active.note));
            emit activeChanged();
            return;
        }
    }
    close();
    m_opening = true;
    const int serial = ++m_openSerial;
    setStatus(QStringLiteral("Opening %1 ...").arg(game.title));
    auto *watcher = new QFutureWatcher<QPair<dir::IGameSource *, QString>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, game, serial]() {
        const auto r = watcher->result();
        watcher->deleteLater();
        if (serial != m_openSerial) { delete r.first; return; }      // superseded
        m_opening = false;
        if (!r.first) {
            setStatus(QStringLiteral("Could not open %1: %2").arg(game.title, r.second));
            emit activeChanged();
            return;
        }
        m_source.reset(r.first);
        {
            QMutexLocker lock(&m_poolMutex);
            m_pool.insert(keyOf(game), m_source);
        }
        m_active = r.first->info();
        m_activeKey = keyOf(game);
        emit sourceOpened(m_active.gameId);
        QSettings().setValue(QStringLiteral("games/active"), m_activeKey);
        setStatus(QStringLiteral("%1 \u00b7 %2").arg(m_active.title, m_active.note));
        emit activeChanged();
    });
    watcher->setFuture(QtConcurrent::run([plugin, game]() {
        QString err;
        dir::IGameSource *src = plugin->open(game, &err);
        return qMakePair(src, err);
    }));
}

// a game the film needs while another one is shown in the Asset Browser
void GameLibrary::openInBackground(const QString &key)
{
    auto it = std::find_if(m_games.cbegin(), m_games.cend(), [&](const dir::GameInfo &g) { return keyOf(g) == key; });
    dir::IGamePlugin *plugin = it != m_games.cend() ? pluginFor(it->pluginId) : nullptr;
    {
        QMutexLocker lock(&m_poolMutex);
        if (!plugin || m_pool.contains(key) || m_poolOpening.contains(key)) return;
        m_poolOpening.insert(key);
    }
    const dir::GameInfo game = *it;
    auto *watcher = new QFutureWatcher<QPair<dir::IGameSource *, QString>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, game, key]() {
        const auto r = watcher->result();
        watcher->deleteLater();
        {
            QMutexLocker lock(&m_poolMutex);
            m_poolOpening.remove(key);
            if (r.first) m_pool.insert(key, std::shared_ptr<dir::IGameSource>(r.first));
        }
        if (r.first) emit sourceOpened(game.gameId);
        else setStatus(QStringLiteral("Could not open %1: %2").arg(game.title, r.second));
    });
    watcher->setFuture(QtConcurrent::run([plugin, game]() {
        QString err;
        dir::IGameSource *src = plugin->open(game, &err);
        return qMakePair(src, err);
    }));
}

dir::IGameSource *GameLibrary::sourceForId(const QString &gameId)
{
    QString key;
    {
        QMutexLocker lock(&m_poolMutex);
        if (m_source && m_active.gameId == gameId) return m_source.get();
        for (auto it = m_pool.cbegin(); it != m_pool.cend(); ++it)
            if (it.value()->info().gameId == gameId) return it.value().get();
        for (const dir::GameInfo &g : m_games) if (g.gameId == gameId) { key = keyOf(g); break; }
        if (key.isEmpty() || m_poolOpening.contains(key)) return nullptr;
    }
    QMetaObject::invokeMethod(this, [this, key]() { openInBackground(key); }, Qt::QueuedConnection);
    return nullptr;
}

QString GameLibrary::titleForId(const QString &gameId) const
{
    for (const dir::GameInfo &g : m_games) if (g.gameId == gameId) return g.title;
    return gameId;
}

void GameLibrary::close()
{
    if (!m_source && m_activeKey.isEmpty()) return;
    m_source.reset();                                   // the pool keeps it open for the film
    m_active = {};
    m_activeKey.clear();
    emit activeChanged();
}

QVariantList GameLibrary::gamesJs() const
{
    QVariantList out;
    for (const dir::GameInfo &g : m_games)
        out << QVariantMap{{QStringLiteral("key"), keyOf(g)}, {QStringLiteral("plugin"), g.pluginId}, {QStringLiteral("id"), g.gameId},
                           {QStringLiteral("title"), g.title}, {QStringLiteral("folder"), g.folder}, {QStringLiteral("support"), g.support},
                           {QStringLiteral("note"), g.note}, {QStringLiteral("steamAppId"), g.steamAppId}};
    return out;
}

QVariantList GameLibrary::pluginsJs() const
{
    QVariantList out;
    for (int i = 0; i < m_plugins.size(); ++i) {
        QVariantList known;
        for (const dir::GameInfo &g : m_plugins[i]->knownGames())
            known << QVariantMap{{QStringLiteral("id"), g.gameId}, {QStringLiteral("title"), g.title}, {QStringLiteral("support"), g.support}};
        out << QVariantMap{{QStringLiteral("id"), m_plugins[i]->id()}, {QStringLiteral("name"), m_plugins[i]->name()},
                           {QStringLiteral("version"), m_plugins[i]->version()}, {QStringLiteral("file"), m_pluginFiles.value(i)},
                           {QStringLiteral("games"), known}};
    }
    return out;
}

void GameLibrary::setStatus(const QString &s)
{
    m_status = s;
    emit statusChanged();
}
