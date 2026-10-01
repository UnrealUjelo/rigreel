#!/usr/bin/env bash
# Sync the Director live runtime from the RigReel workspace into RE4's reframework/autorun folder.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
WS="${RIGREEL_HOME:-$(cd "$HERE/../.." && pwd)}"
GAME="${RIGREEL_GAME_DIR:-/c/Program Files (x86)/Steam/steamapps/common/RESIDENT EVIL 4  BIOHAZARD RE4}"
PYTHON="${RIGREEL_PYTHON:-$WS/tools/venv/Scripts/python.exe}"

if [ ! -d "$WS/mod/Director" ]; then echo "RigReel workspace not found: $WS" >&2; exit 1; fi
if [ ! -d "$GAME/reframework/autorun" ]; then echo "REFramework autorun folder not found: $GAME" >&2; exit 1; fi

"$PYTHON" - "$WS/mod" <<'PY'
from luaparser import ast
from pathlib import Path
import sys
bad = 0
for f in sorted(Path(sys.argv[1]).rglob("*.lua")):
    try: ast.parse(f.read_text(encoding="utf-8"))
    except Exception as e: bad += 1; print("SYNTAX", f, e)
raise SystemExit(bad)
PY
rm -rf "$GAME/reframework/autorun/Director"
cp "$WS/mod/Director.lua" "$GAME/reframework/autorun/Director.lua"
cp -r "$WS/mod/Director" "$GAME/reframework/autorun/Director"
echo "deployed $(date +%H:%M:%S)"
