// Where things live: the game, the runtime's bridge folder, the RigReel workspace and its tools.
#pragma once
#include <QString>

namespace Paths {
QString gameDir();        // ...\RESIDENT EVIL 4  BIOHAZARD RE4
QString directorDir();    // <game>\reframework\data\director
QString bridgeDir();      // <director>\bridge
QString workspace();      // repository/portable root (mod/, studio/, tools/)
QString python();         // tools\venv\Scripts\python.exe
QString downloads();      // renders land here
QString findBlender();    // newest Blender install, or empty
QString ffmpeg();         // ffmpeg on PATH, or empty
QString settingsFile();   // legacy checkout INI or the user's application-config directory
}
