#!/usr/bin/env bash
# Stop a running Studio, rebuild, start it again (dev loop).
HERE="$(cd "$(dirname "$0")" && pwd)"
taskkill //IM RigReelStudio.exe //F > /dev/null 2>&1
sleep 0.7
bash "$HERE/build.sh" "${1:-Release}" || exit 1
cd "$HERE/build" && (./RigReelStudio.exe > /dev/null 2>&1 &)
sleep 4
echo "--- log"; head -40 "$HERE/studio.log"
