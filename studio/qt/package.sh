#!/usr/bin/env bash
# Build RigReel Studio in Release and install it, with its Qt runtime, into the workspace's app directory.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
APP="$ROOT/app"
QT_PREFIX="${RIGREEL_QT_PREFIX:-C:/Qt/6.10.3/mingw_64}"
MINGW_BIN="${RIGREEL_MINGW_BIN:-/c/Qt/Tools/mingw1310_64/bin}"
QT_BIN="$(cygpath -u "$QT_PREFIX")/bin"
export PATH="$MINGW_BIN:$QT_BIN:$PATH"
taskkill //IM RigReelStudio.exe //F > /dev/null 2>&1 || true
# wait until it is really gone (the exe stays locked until the process has exited)
for i in $(seq 1 20); do tasklist //FI "IMAGENAME eq RigReelStudio.exe" //NH | grep -q RigReelStudio || break; sleep 0.5; done
bash "$HERE/build.sh" Release
mkdir -p "$APP"
rm -f "$APP/DirectorStudio.exe"
cp "$HERE/build/RigReelStudio.exe" "$APP/"
# game plugins (DLL + their data: name lists, type dumps) and the zstd decoder the RE Engine plugin loads
mkdir -p "$APP/plugins/games"
cp "$HERE/build/plugins/games/"*.dll "$APP/plugins/games/"
cp -r "$HERE/build/plugins/games/reengine" "$APP/plugins/games/"
cp "$HERE/third_party/zstd/libzstd.dll" "$HERE/third_party/zstd/LICENSE-zstd.txt" "$APP/"
windeployqt --qmldir "$HERE/qml" --no-translations --no-system-d3d-compiler --no-opengl-sw --compiler-runtime "$APP/RigReelStudio.exe" > "$HERE/deploy.log" 2>&1
cp "$HERE/resources/icons/LICENSE-lucide.txt" "$APP/"
cp "$ROOT/LICENSE" "$ROOT/THIRD_PARTY_NOTICES.md" "$APP/"
echo "installed $(cd "$APP" && pwd)/RigReelStudio.exe"
