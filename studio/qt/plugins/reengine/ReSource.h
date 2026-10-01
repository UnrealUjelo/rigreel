// One opened RE Engine game: archives + name list + format versions, handing out neutral assets.
#pragma once

#include "Pak.h"
#include "Profiles.h"

#include <director/GamePlugin.h>
#include <QHash>
#include <QMutex>
#include <memory>

namespace re {

class RszTypes;

class ReSource : public dir::IGameSource {
public:
    ReSource(const dir::GameInfo &info, const GameProfile &profile, const QString &dataDir);
    ~ReSource() override;
    bool open(QString *error);

    dir::GameInfo info() const override { return m_info; }
    QList<dir::CatalogEntry> catalog(dir::AssetKind kind) override;
    bool exists(const QString &path) override;
    QByteArray readFile(const QString &path) override;
    QStringList listFiles(const QString &prefix, const QString &suffix, int limit) override;
    dir::ModelPtr loadModel(const QString &id, QString *error) override;
    dir::AnimationSetPtr loadAnimations(const QString &id, QString *error) override;
    dir::TexturePtr loadTexture(const QString &path, QString *error) override;
    dir::StagePtr loadStage(const QString &id, QString *error) override;
    QList<dir::CatalogEntry> animationsFor(const QString &modelId) override;
    QVariantMap describe(const QString &path) override;
    QImage thumbnail(const QString &modelId, int size, QString *error) override;

    // "_Chainsaw/Character/x.tex" or "natives/stm/..." -> full archive path with the game's version suffix
    QString resolve(const QString &resourcePath, const QString &ext) const;
    int formatVersion(const QString &ext) const { return m_versions.value(ext.toLower()); }
    PakFileSystem &files() { return m_fs; }
    const RszTypes *rszTypes();          // loaded on first use (null when the type list is missing)
    QString rszError() const { return m_rszError; }
    void setMaxTextureSize(int px) { m_maxTexture = px; }

private:
    QSharedPointer<dir::MeshAsset> loadMesh(const QString &path, QString *error);
    QVector<dir::MaterialAsset> loadMaterials(const QString &path, QString *error);
    dir::ModelPtr modelFromMesh(const QString &meshPath, const QString &mdfPath, QString *error);
    dir::ModelPtr modelFromMeshes(const QString &id, const QString &name, const QStringList &meshes, QString *error);
    dir::ModelPtr modelFromPrefabs(const QString &id, const QString &name, const QStringList &prefabs,
                                   const QHash<QString, QVector<int>> &hiddenParts, QString *error);
    void detectVersions();
    QString streamingPath(const QString &path) const;

    dir::GameInfo m_info;
    GameProfile m_profile;
    QString m_dataDir;
    PakFileSystem m_fs;
    QString m_platform = QStringLiteral("stm");
    QHash<QString, int> m_versions;             // extension -> version number
    int m_maxTexture = 2048;
    QMutex m_cacheMutex;
    QHash<QString, QSharedPointer<const dir::MeshAsset>> m_meshCache;
    QHash<QString, QList<dir::CatalogEntry>> m_catalogCache;
    QString m_rszPath;
    std::unique_ptr<RszTypes> m_rsz;
    bool m_rszTried = false;
    QString m_rszError;
    QMutex m_rszMutex;
};

} // namespace re
