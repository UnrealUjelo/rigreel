#!/usr/bin/env bash
# Build RigReel Studio (Qt 6 / MinGW). Usage: bash build.sh [Release|Debug]
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
CFG="${1:-Release}"
QT_PREFIX="${RIGREEL_QT_PREFIX:-C:/Qt/6.10.3/mingw_64}"
MINGW_BIN="${RIGREEL_MINGW_BIN:-/c/Qt/Tools/mingw1310_64/bin}"
TOOLS_BIN="${RIGREEL_TOOLS_BIN:-$HERE/../../tools/venv/Scripts}"
QT_BIN="$(cygpath -u "$QT_PREFIX")/bin"
export PATH="$MINGW_BIN:$QT_BIN:$TOOLS_BIN:$PATH"
cmake -S "$HERE" -B "$HERE/build" -G Ninja -DCMAKE_BUILD_TYPE="$CFG" -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
      -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc > "$HERE/build.log" 2>&1 || { tail -40 "$HERE/build.log"; exit 1; }
cmake --build "$HERE/build" -j 8 -- -k 0 >> "$HERE/build.log" 2>&1 || { grep -E "error|Error" "$HERE/build.log" | head -60; exit 1; }
echo "built $HERE/build/RigReelStudio.exe"
