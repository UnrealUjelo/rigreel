"""Build the Director motlist catalog (JSON) from the RE4 pak file list.

Output: <game>/reframework/data/director/catalog/motlists.json
Each entry: {"i": index, "p": engine path (no natives/stm, no version), "n": short name,
             "c": character/owner code, "g": group}
Bank IDs used in-game are 60000 + index, so keep this list append-only once projects exist.
"""
import json
import os
import re
from pathlib import Path

WS = Path(os.environ.get("RIGREEL_HOME", Path(__file__).resolve().parents[2]))
GAME = Path(os.environ.get("RIGREEL_GAME_DIR", r"C:\Program Files (x86)\Steam\steamapps\common\RESIDENT EVIL 4  BIOHAZARD RE4"))
LIST = Path(os.environ.get("RIGREEL_RE4_LIST", WS / "extract" / "RE4_STM.list"))
DIRECTOR_DIR = Path(os.environ.get("RIGREEL_DIRECTOR_DIR", GAME / "reframework" / "data" / "director"))
OUT = DIRECTOR_DIR / "catalog" / "motlists.json"

CHAR_NAMES = {
    "cha0": "Leon", "cha1": "Ashley", "cha2": "Luis", "cha3": "Ada", "cha6": "Krauser?", "cha7": "Hunnigan?",
    "chb0": "Merchant", "chc0": "Ganado (village)", "chd0": "Zealot?", "chd4": "Zealot armored?", "chd6": "Soldier?",
}


def classify(path: str):
    p = path.lower()
    group = "other"
    owner = ""
    m = re.search(r"/animation/ch/(ch[0-9a-z]{2})/", p)
    if m:
        owner = m.group(1)
        group = "facial" if "/facial/" in p else "gameplay"
    elif "/event/cs/" in p:
        group = "cutscene"
        m2 = re.search(r"/event/cs/(cs[0-9a-z]+)/", p)
        owner = m2.group(1) if m2 else ""
    elif "/animation/wp/" in p:
        group = "weapon"
        m3 = re.search(r"/animation/wp/(wp[0-9a-z]+)/", p)
        owner = m3.group(1) if m3 else ""
    elif "/gimmick" in p:
        group = "gimmick"
    elif "/animation/sm/" in p or "/environment/" in p:
        group = "prop"
    if p.startswith("natives/stm/_mercenaries/"):
        group += "(merc)"
    elif p.startswith("natives/stm/_anotherorder/"):
        group += "(sw)"
    return owner, group


def main():
    entries = []
    seen = set()
    for raw in LIST.read_text(encoding="utf-8", errors="ignore").splitlines():
        line = raw.strip()
        if not line.lower().endswith(".motlist.663"):
            continue
        engine_path = re.sub(r"^natives/stm/", "", line, flags=re.I)
        engine_path = re.sub(r"\.663$", "", engine_path)
        key = engine_path.lower()
        if key in seen:
            continue
        seen.add(key)
        owner, group = classify(line)
        name = engine_path.rsplit("/", 1)[-1].replace(".motlist", "")
        entries.append({"p": engine_path, "n": name, "c": owner, "g": group})
    entries.sort(key=lambda e: (e["g"], e["c"], e["n"]))
    for i, e in enumerate(entries):
        e["i"] = i
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps({"version": 1, "chars": CHAR_NAMES, "motlists": entries}, separators=(",", ":")), encoding="utf-8")
    groups = {}
    for e in entries:
        groups[e["g"]] = groups.get(e["g"], 0) + 1
    print(f"wrote {len(entries)} motlists -> {OUT}")
    print("groups:", groups)


if __name__ == "__main__":
    main()


# ---------------------------------------------------------------------------- meshes (for the spawner)
MESH_OUT = DIRECTOR_DIR / "catalog" / "meshes.json"


def mesh_category(p: str) -> str:
    q = p.lower()
    if "/character/ch/" in q or "/appsystem/character/" in q: return "character"
    if "/animation/wp/" in q or "/weapon/" in q or "/wp/" in q: return "weapon"
    if "/item/" in q or "/it_" in q: return "item"
    if "/gimmick/" in q or "/gm" in q: return "gimmick"
    if "/environment/sm/" in q or "/environment/props" in q or "/sm/" in q: return "prop"
    if "/environment/" in q: return "environment"
    if "/vfx/" in q or "/effect" in q: return "vfx"
    return "other"


def build_meshes():
    lines = LIST.read_text(encoding="utf-8", errors="ignore").splitlines()
    mdfs = {}
    meshes = []
    for raw in lines:
        line = raw.strip()
        low = line.lower()
        if low.endswith(".mdf2.32"):
            ep = re.sub(r"^natives/stm/", "", line, flags=re.I)
            ep = re.sub(r"\.32$", "", ep)
            folder = ep.rsplit("/", 1)[0].lower()
            mdfs.setdefault(folder, []).append(ep)
        elif low.endswith(".mesh.221108797"):
            ep = re.sub(r"^natives/stm/", "", line, flags=re.I)
            ep = re.sub(r"\.221108797$", "", ep)
            meshes.append(ep)
    out = []
    for ep in meshes:
        folder, fname = ep.rsplit("/", 1)
        stem = fname[:-5]  # drop .mesh
        cands = mdfs.get(folder.lower(), [])
        mdf = None
        for cnd in cands:
            cs = cnd.rsplit("/", 1)[1][:-5]
            if cs.lower() in (stem.lower(), stem.lower() + "_mat", stem.lower() + "_v00"):
                mdf = cnd; break
        if mdf is None and cands:
            mdf = cands[0]
        out.append({"p": ep, "n": stem, "c": mesh_category(ep), "mdf": mdf})
    out.sort(key=lambda e: (e["c"], e["n"]))
    MESH_OUT.write_text(json.dumps({"version": 1, "meshes": out}, separators=(",", ":")), encoding="utf-8")
    cats = {}
    for e in out: cats[e["c"]] = cats.get(e["c"], 0) + 1
    print(f"wrote {len(out)} meshes -> {MESH_OUT}", cats)


if __name__ == "__main__":
    build_meshes()
