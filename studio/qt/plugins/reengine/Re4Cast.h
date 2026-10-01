// Resident Evil 4's characters: every folder appsystem/character/<id>/costume has costume preset tables
// (chainsaw.CostumePresetUserData). Each preset is a complete, game-authored look: part prefabs (body, head,
// hair, clothes) plus mesh parts switched off. This turns them into catalog entries with friendly names.
#pragma once

#include <director/Assets.h>

namespace re {

class ReSource;

struct Re4Look {
    QString name;
    QStringList prefabs;
    QStringList meshes;                          // or assembled from meshes (their materials sit beside them)
    QHash<QString, QVector<int>> hiddenParts;    // prefab path (lower) -> parts off
    bool variant = false, dlc = false;
};

struct Re4Character {
    QString id, code, tree, name, group, general;
    QVector<Re4Look> looks;
};

QVector<Re4Character> re4Characters(ReSource &src, const QString &namesFile);

} // namespace re
