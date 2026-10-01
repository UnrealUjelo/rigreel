// RE Engine .mesh reader (classic vertex-buffer meshes, RE7 .. Pragmata layouts). Reads LOD 0 into a neutral
// MeshAsset: positions, normals, tangents, UVs, skin weights, submeshes with materials, and the skeleton.
#pragma once

#include <director/Assets.h>

namespace re {

enum class MeshVersion { Unknown, RE7, DMC5, RE8, RE_RT, RE4, SF6, DD2_OLD, KUNITSUGAMI, DD2, ONIMUSHA, MHWILDS, PRAGMATA = 13, RE9 };

MeshVersion meshVersion(quint32 internalVersion, quint32 fileVersion);

// fileVersion = the number after ".mesh." in the path. streaming = the streaming copy's bytes (if the mesh has one).
QSharedPointer<dir::MeshAsset> parseMesh(const QByteArray &data, quint32 fileVersion, const QByteArray &streaming, QString *error);

} // namespace re
