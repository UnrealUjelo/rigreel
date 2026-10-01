// Director game plugin for Capcom's RE Engine games (Resident Evil 2/3/4/7/8/9, DMC5, SF6, DD2, Monster Hunter).
#pragma once

#include <director/GamePlugin.h>
#include <QObject>

class ReEnginePlugin : public QObject, public dir::IGamePlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DirectorGamePlugin_iid FILE "reengine.json")
    Q_INTERFACES(dir::IGamePlugin)
public:
    QString id() const override { return QStringLiteral("reengine"); }
    QString name() const override { return QStringLiteral("RE Engine"); }
    QString version() const override { return QStringLiteral("1.0"); }
    QList<dir::GameInfo> knownGames() const override;
    QList<dir::GameInfo> detect(const QStringList &steamLibraries) override;
    dir::GameInfo probe(const QString &folder) override;
    dir::IGameSource *open(const dir::GameInfo &game, QString *error) override;
};
