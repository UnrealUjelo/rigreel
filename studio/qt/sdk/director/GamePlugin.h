// The contract between RigReel Studio and a game plugin.
//
// A plugin knows one engine family (e.g. RE Engine). It finds installed games, and for a game the user owns it
// opens a GameSource: a read-only view of that game's files that hands out neutral assets (Assets.h).
// Plugins live in <app>/plugins/games/*.dll and are loaded with QPluginLoader.
//
// Threading: IGamePlugin calls happen on the GUI thread. IGameSource must be safe to call from several worker
// threads at once (the Studio loads assets in the background).
#pragma once

#include "Assets.h"

#include <QImage>
#include <QList>
#include <QObject>
#include <QString>
#include <QtPlugin>

namespace dir {

struct GameInfo {
    QString pluginId;          // "reengine"
    QString gameId;            // "re4"
    QString title;             // "Resident Evil 4"
    QString folder;            // install folder
    QString executable;        // main exe inside folder (for the optional live link)
    int steamAppId = 0;
    bool installed = false;
    QString support;           // "full" | "partial" | "untested" | "unsupported"
    QString note;              // why, for the Games page
};

class IGameSource {
public:
    virtual ~IGameSource() = default;

    virtual GameInfo info() const = 0;

    // What the Asset Browser shows. May take a few seconds the first time (callers use a worker thread).
    virtual QList<CatalogEntry> catalog(AssetKind kind) = 0;

    // Raw file access by the game's own path.
    virtual bool exists(const QString &path) = 0;
    virtual QByteArray readFile(const QString &path) = 0;
    virtual QStringList listFiles(const QString &prefix, const QString &suffix = {}, int limit = -1) = 0;

    // Assets. On failure: null + *error.
    virtual ModelPtr loadModel(const QString &id, QString *error) = 0;            // character look or prop
    virtual AnimationSetPtr loadAnimations(const QString &id, QString *error) = 0;
    virtual TexturePtr loadTexture(const QString &path, QString *error) = 0;
    virtual StagePtr loadStage(const QString &id, QString *error) = 0;

    // Animations that fit a model (same skeleton family), best first.
    virtual QList<CatalogEntry> animationsFor(const QString &modelId) = 0;

    // Free-form details about any file (Element Viewer).
    virtual QVariantMap describe(const QString &path) = 0;
    // A small picture of a model (browser thumbnails): transparent background, about size x size pixels.
    // Optional: an empty image means "no thumbnail".
    virtual QImage thumbnail(const QString &modelId, int size, QString *error) { Q_UNUSED(modelId) Q_UNUSED(size) Q_UNUSED(error) return {}; }
};

class IGamePlugin {
public:
    virtual ~IGamePlugin() = default;
    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual QString version() const = 0;

    // Every game this plugin knows, installed or not (the Games page lists them).
    virtual QList<GameInfo> knownGames() const = 0;
    // Installed games found in these Steam library folders.
    virtual QList<GameInfo> detect(const QStringList &steamLibraries) = 0;
    // A folder the user picked by hand. info.installed = false when it is not a game this plugin reads.
    virtual GameInfo probe(const QString &folder) = 0;
    virtual IGameSource *open(const GameInfo &game, QString *error) = 0;     // caller owns the result
};

} // namespace dir

#define DirectorGamePlugin_iid "studio.director.GamePlugin/1"
Q_DECLARE_INTERFACE(dir::IGamePlugin, DirectorGamePlugin_iid)
