// RE Engine motion lists (.motlist) and motions (.mot): bone tracks with the engine's key compressions.
// Decoding follows the Noesis RE Engine plugin (alphaZomega) for mot versions 65 .. 613+.
#pragma once

#include <director/Assets.h>

namespace re {

QSharedPointer<dir::AnimationSet> parseMotlist(const QByteArray &data, QString *error);
QSharedPointer<dir::AnimationSet> parseMot(const QByteArray &data, QString *error);   // a lone .mot

} // namespace re
