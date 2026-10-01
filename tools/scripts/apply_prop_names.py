"""Write the human names from tools/prop_names.json into the runtime's mesh catalogue.

The catalogue entries gain a `name` field; the Prop browser shows it instead of the raw asset id
(sm01_536_00 -> "Wooden crate"). Run it again after naming another batch of sheets.

    python apply_prop_names.py            # updates the catalogue in the game folder
    python apply_prop_names.py --report   # just says how many are named, per category
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent          # tools/
NAMES = ROOT / "prop_names.json"
GAME = Path(os.environ.get("RIGREEL_GAME_DIR", r"C:\Program Files (x86)\Steam\steamapps\common\RESIDENT EVIL 4  BIOHAZARD RE4"))
CATALOG = Path(os.environ.get("RIGREEL_MESH_CATALOG", GAME / "reframework" / "data" / "director" / "catalog" / "meshes.json"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--report", action="store_true")
    args = ap.parse_args()

    names = json.loads(NAMES.read_text(encoding="utf-8"))
    flat = {}
    for cat, entries in names.items():
        if cat.startswith("_") or not isinstance(entries, dict):
            continue
        flat.update(entries)

    data = json.loads(CATALOG.read_text(encoding="utf-8"))
    meshes = data.get("meshes") or []
    named = Counter()
    total = Counter()
    for m in meshes:
        total[m.get("c")] += 1
        nm = flat.get(m.get("n"))
        if nm:
            m["name"] = nm
            named[m.get("c")] += 1
        elif "name" in m and m["n"] not in flat:
            m.pop("name", None)

    for cat in sorted(total):
        print(f"{cat:12} {named[cat]:5} / {total[cat]}")
    if args.report:
        return 0

    CATALOG.write_text(json.dumps(data, separators=(",", ":")), encoding="utf-8")
    print(f"catalogue updated: {sum(named.values())} named of {len(meshes)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
