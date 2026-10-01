// Resident Evil 2 (RT) style characters. Survivors are part prefabs
// objectroot/prefab/character/survivor/parts/<code>/<code>_<body|face|hair|other>_<default|costume_N>.pfb: a look
// is every part of one costume, the default part where the costume has none. Creatures are whole prefabs
// (objectroot/prefab/character/enemy/<code>.pfb). Zombies get their clothes at run time, so their looks are
// assembled from the mesh folders instead (body + face + shirt + pants).
#pragma once

#include "Re4Cast.h"

namespace re {

class ReSource;

// same shapes as RE4's cast; Re4Look::meshes carries a mesh-assembled look
QVector<Re4Character> partsCharacters(ReSource &src, const QString &namesFile);

} // namespace re
