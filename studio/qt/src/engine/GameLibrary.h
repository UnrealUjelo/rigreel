// The games the user can animate with: game plugins (plugins/games/*.dll), the installs they find in the Steam
// libraries or in folders the user adds, and the games that are open. A film can mix games (RE2's Leon on an RE4
// map playing RE4 clips), so games stay open once opened; the active one is the one the Asset Browser shows.
// Opening indexes the game's archives on a worker thread.
#pragma once

#include <director/GamePlugin.h>
#include <QFutureWatcher>
#include <QObject>
#include <QVariantList>
#include <QHash>
#include <QMutex>
#include <QSet>
#include <memory>

class GameLibrary : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList games READ gamesJs NOTIFY gamesChanged)
    Q_PROPERTY(QVariantList plugins READ pluginsJs NOTIFY gamesChanged)
    Q_PROPERTY(QString activeKey READ activeKey NOTIFY activeChanged)
    Q_PROPERTY(QString activeTitle READ activeTitle NOTIFY activeChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY activeChanged)
    Q_PROPERTY(bool opening READ opening NOTIFY statusChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
public:
    explicit GameLibrary(QObject *parent = nullptr);
    ~GameLibrary() override;

    void loadPlugins(const QString &dir);
    static QStringList steamLibraries();

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool addFolder(const QString &folder);
    Q_INVOKABLE void removeFolder(const QString &folder);
    Q_INVOKABLE void activate(const QString &key);
    Q_INVOKABLE void close();

    QVariantList gamesJs() const;
    QVariantList pluginsJs() const;
    QString activeKey() const { return m_activeKey; }
    QString activeTitle() const { return m_active.title; }
    dir::GameInfo activeInfo() const { return m_active; }
    bool ready() const { return m_source != nullptr; }
    bool opening() const { return m_opening; }
    QString status() const { return m_status; }
    dir::IGameSource *source() const { return m_source.get(); }
    QString gameId() const { return m_active.gameId; }
    // any installed game by its id ("re4", "re2rt"): open already, else nullptr while it opens in the background
    // (sourceOpened follows). Safe from worker threads.
    dir::IGameSource *sourceForId(const QString &gameId);
    QString titleForId(const QString &gameId) const;
    static QString keyOf(const dir::GameInfo &g) { return g.pluginId + QLatin1Char(':') + g.gameId + QLatin1Char(':') + g.folder.toLower(); }

signals:
    void gamesChanged();
    void activeChanged();
    void statusChanged();
    void sourceOpened(const QString &gameId);

private:
    void setStatus(const QString &s);
    dir::IGamePlugin *pluginFor(const QString &id) const;
    void openInBackground(const QString &key);

    QVector<dir::IGamePlugin *> m_plugins;
    QStringList m_pluginFiles;
    QList<dir::GameInfo> m_games;
    QStringList m_customFolders;
    std::shared_ptr<dir::IGameSource> m_source;                  // the active game
    QHash<QString, std::shared_ptr<dir::IGameSource>> m_pool;    // every open game, by key (never closed: the film may use it)
    QSet<QString> m_poolOpening;
    mutable QMutex m_poolMutex;
    dir::GameInfo m_active;
    QString m_activeKey;
    QString m_status;
    bool m_opening = false;
    int m_openSerial = 0;
};
