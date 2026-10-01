"""Build director/catalog/cast.json (v2) from the game's own costume preset tables.

Every character folder appsystem/character/<id>/costume/ has one or more
<id>costumepresetuserdata*.user.2 (chainsaw.CostumePresetUserData). Each preset (Data) is a complete, game-authored
look: a list of part prefabs (body, head, hair, clothes, accessories) plus, per prefab, the mesh sub-parts to switch off
(_InitialPartsOffList). Spawning exactly these lists is what makes a spawned character look like the real one.

A spawnable cast member = bare skeleton root (<code>.fbxskel / .skeleton + via.motion.Motion) + these prefabs parented under it.

Output: [{ id, code, tree, dlc, name, skel, general, general_shared,
           presets: [{ name, uid, parts: [{ path, off: [int] }] }] }]
"""
import json
import os
import re
import sys
from pathlib import Path

WS = Path(os.environ.get("RIGREEL_HOME", Path(__file__).resolve().parents[2]))
GAME = Path(os.environ.get("RIGREEL_GAME_DIR", r"C:\Program Files (x86)\Steam\steamapps\common\RESIDENT EVIL 4  BIOHAZARD RE4"))
LIST = Path(os.environ.get("RIGREEL_RE4_LIST", WS / "extract" / "RE4_STM.list"))
EXTRACT = Path(os.environ.get("RIGREEL_EXTRACT_DIR", WS / "extract" / "natives_out" / "natives" / "stm"))
DIRECTOR_DIR = Path(os.environ.get("RIGREEL_DIRECTOR_DIR", GAME / "reframework" / "data" / "director"))
OUT = DIRECTOR_DIR / "catalog" / "cast.json"
REASY = Path(os.environ.get("RIGREEL_REASY", WS / "tools" / "REasy-src"))

sys.path.insert(0, str(REASY))
from file_handlers.rsz.rsz_file import RszFile  # noqa: E402
from utils.type_registry import TypeRegistry  # noqa: E402

# conservative friendly names; everything else shows its code until the user renames it
NAMES = {"cha0": "Leon", "cha1": "Ashley", "cha2": "Ada", "cha3": "Luis", "chb0": "Merchant"}  # cha2/cha3 verified by spawning
# generic families get a letter per folder (the game has several villager builds)
ID_NAMES = {"ch1c0z0": "Villager A", "ch1c0z1": "Villager B", "ch1c0z2": "Villager C", "ch1c8z0": "Villager D", "ch1d1z1": "Villager E",
            "ch0a1z0": "Ashley (playable)", "ch2a1z0": "Ashley", "ch3a8z0": "Ada (Separate Ways)", "ch0a0z0": "Leon", "ch6i0z0": "Leon (Mercenaries)"}
# preset uid shared by every character for its default look (Leon = set 02 with jacket, Ashley = set 00, Ada = set 00)
DEFAULT_UID = 160198385
_names_file = Path(__file__).resolve().parents[1] / "cast_names.json"
OVERRIDES = {k: v for k, v in json.loads(_names_file.read_text(encoding="utf-8")).items() if not k.startswith("_")} if _names_file.is_file() else {}

PRESET_RE = re.compile(r"^natives/stm/(_chainsaw|_anotherorder|_mercenaries)/appsystem/character/(ch[0-9a-z]+)/costume/([^/]*costumepresetuserdata[^/]*)\.user\.2$", re.I)
PART_RE = re.compile(r"/costume/(ch[a-z][0-9a-z])([0-9a-z]{2})_(\d\d)", re.I)
VARIANT_RE = re.compile(r"_\d\d[a-z](?:_|\.pfb$)", re.I)  # cha000_00b.pfb = damaged / wet variant of a part


def val(v):
    for attr in ("value", "string", "index"):
        if hasattr(v, attr):
            return getattr(v, attr)
    if hasattr(v, "values"):
        return [val(x) for x in v.values]
    return None


def parse_presets(reg, path: Path):
    f = RszFile()
    f.type_registry = reg
    f.filepath = "x.user.2"
    f.read(path.read_bytes())

    def tname(i):
        return (reg.registry.get(f"{f.instance_infos[i].type_id:08x}") or {}).get("name", "?")

    def fields(i):
        return {k: val(v) for k, v in f.parsed_elements.get(i, {}).items()}

    presets = []
    for i in range(len(f.instance_infos)):
        if tname(i) != "chainsaw.CostumePresetUserData.Data":
            continue
        d = fields(i)
        parts = []
        for pd_idx in (d.get("_PrefabTable") or []):
            pd = fields(pd_idx)
            pf = pd.get("_Prefab")
            if not isinstance(pf, int):
                continue
            p = (fields(pf).get("Path") or "").rstrip("\x00")
            if not p:
                continue
            parts.append({"path": p.replace("\\", "/"), "off": [int(x) for x in (pd.get("_InitialPartsOffList") or [])]})
        if parts:
            presets.append({"uid": int(d.get("_ID") or 0), "parts": parts})
    return presets


def main():
    reg = TypeRegistry(str(REASY / "resources" / "data" / "dumps" / "rszre4.json"))
    lines = [l.strip() for l in LIST.read_text(encoding="utf-8", errors="ignore").splitlines() if l.strip()]
    lower_set = {l.lower() for l in lines}

    # skeleton per mesh code
    skel_files = [l for l in lines if re.search(r"/character/ch/[^/]+/.*\.(fbxskel|skeleton)\.\d+$", l, re.I)]

    def find_skel(tree, code):
        cands = [l for l in skel_files if f"/{tree}/character/ch/{code}/".lower() in l.lower()]
        if not cands and tree != "_chainsaw":
            cands = [l for l in skel_files if f"/_chainsaw/character/ch/{code}/".lower() in l.lower()]
        if not cands:  # any tree (Separate Ways Ada uses the Mercenaries cha8 rig)
            cands = [l for l in skel_files if f"/character/ch/{code}/".lower() in l.lower()]
        if not cands:
            return None

        def rank(p):
            base = p.lower().rsplit("/", 1)[1]
            if base.startswith(f"{code}.fbxskel"):
                return 0
            if base.startswith(f"{code}00."):
                return 1
            if base.startswith(f"{code}0"):
                return 2
            return 3 + p.count("/")
        best = sorted(cands, key=rank)[0]
        return re.sub(r"\.(fbxskel|skeleton)\.\d+$", lambda m: "." + m.group(1), best[len("natives/stm/"):])

    motlists = [l for l in lines if re.search(r"/animation/ch/[^/]+/motlist/(?:[^/]+/)*[^/]+_general[^/]*\.motlist\.663$", l, re.I) and "/facial/" not in l.lower()]

    def find_general(tree, code):
        for t in (tree, "_chainsaw"):
            cands = [l for l in motlists if f"/{t}/animation/ch/{code}/motlist/".lower() in l.lower()]
            if cands:
                def rank(p):
                    b = p.lower().rsplit("/", 1)[1]
                    return (0 if b == f"{code}_general.motlist.663" else 1 if b.startswith(f"{code}_general_0th_com") else 2 if "_0th_" in b else 3, len(b))
                return sorted(cands, key=rank)[0][len("natives/stm/"):-4]
        return None

    # collect preset files per (tree, id)
    files = {}
    for l in lines:
        m = PRESET_RE.match(l)
        if m:
            files.setdefault(m.group(2).lower(), []).append((m.group(1).lower(), l))

    out = []
    for cid, entries in sorted(files.items()):
        presets, seen = [], set()
        trees = sorted({t for t, _ in entries}, key=lambda t: t != "_chainsaw")
        tree = trees[0]
        # base game tables first, then DLC; base tables before _add variants
        for t, l in sorted(entries, key=lambda e: (e[0] != "_chainsaw", "_add" in e[1].lower(), len(e[1]))):
            fp = EXTRACT / l[len("natives/stm/"):]
            if not fp.exists():
                print(f"  (missing extract) {l}")
                continue
            for pr in parse_presets(reg, fp):
                key = tuple(sorted(p["path"].lower() for p in pr["parts"]))
                if key in seen:
                    continue
                seen.add(key)
                # only keep presets whose prefabs exist in this game's file list (DLC tables may point at absent content)
                if all(("natives/stm/" + p["path"] + ".17").lower() in lower_set for p in pr["parts"]):
                    pr["dlc"] = t != "_chainsaw"
                    pr["variant"] = any(VARIANT_RE.search(p["path"]) for p in pr["parts"])
                    presets.append(pr)
        if not presets:
            continue
        presets.sort(key=lambda pr: (pr["variant"], pr["dlc"]))  # clean, base-game looks first
        # mesh code + set from the body prefab of the first preset
        body = next((p["path"] for p in presets[0]["parts"] if PART_RE.search(p["path"]) and PART_RE.search(p["path"]).group(3) == "00"), presets[0]["parts"][0]["path"])
        pm = PART_RE.search(body)
        code = pm.group(1).lower() if pm else "ch" + cid[3:5]
        base_name = ID_NAMES.get(cid) or NAMES.get(code, code.upper())
        # sets that exist as files but are in no table (e.g. Ashley's cha102): synthesise a same-set look so it is still one click
        covered = {pr["set"] if "set" in pr else None for pr in presets}
        set_parts = {}
        for l in lines:
            m = re.search(rf"^natives/stm/{tree}/appsystem/character/{cid}/costume/({code}(\d\d)_(\d\d))\.pfb\.\d+$", l, re.I)
            if m:
                set_parts.setdefault(m.group(2), {})[m.group(3)] = l[len("natives/stm/"):-3]
        for pr in presets:
            b_ = next((p["path"] for p in pr["parts"] if PART_RE.search(p["path"]) and PART_RE.search(p["path"]).group(3) == "00"), None)
            sm = PART_RE.search(b_) if b_ else None
            pr["set"] = sm.group(2) if sm else "00"
        have_sets = {pr["set"] for pr in presets if not pr["variant"]}
        for sset, parts in sorted(set_parts.items()):
            if sset in have_sets or "00" not in parts or "10" not in parts:
                continue
            plist = [{"path": parts[k], "off": []} for k in ("00", "01", "10", "20") if k in parts]
            presets.append({"uid": 0, "parts": plist, "dlc": tree != "_chainsaw", "variant": False, "set": sset, "synth": True})
        presets.sort(key=lambda pr: (pr["variant"], pr.get("synth", False), pr["dlc"], pr["uid"] != DEFAULT_UID))
        # names
        generic = base_name == code.upper() or base_name.startswith("Villager")
        first_clean = next((pr for pr in presets if not pr["variant"] and not pr.get("synth")), None)
        counter = 0
        for pr in presets:
            if generic:
                counter += 1
                pr["name"] = f"{base_name} {counter}"
            elif pr["uid"] == DEFAULT_UID or (pr is first_clean and not any(q["uid"] == DEFAULT_UID for q in presets)):
                pr["name"] = "Default"
            else:
                pr["name"] = f"Costume {pr['set']}"
            if pr["variant"]:
                letters = sorted({m.group(0)[3] for m in (VARIANT_RE.search(p["path"]) for p in pr["parts"]) if m})
                pr["name"] += " · damaged " + "/".join(letters)
            if pr.get("synth"):
                pr["name"] += " (files)"
        from collections import Counter
        dup = Counter(pr["name"] for pr in presets)
        idx = Counter()
        for pr in presets:
            if dup[pr["name"]] > 1:
                idx[pr["name"]] += 1
                pr["name"] = f"{pr['name']} ({idx[pr['name']]})"
        # friendly names from tools/cast_names.json (the user's identifications); look numbers follow the order shown in Studio
        ov = OVERRIDES.get(cid)
        group = None
        if ov:
            base_name = ov.get("name", base_name)
            looks = ov.get("looks") or {}
            group = ov.get("group")
            n = 0
            for pr in presets:
                if pr["variant"] or not looks:
                    continue
                n += 1
                pr["name"] = looks.get(str(n)) or f"{base_name} {n}"
            # damaged variants: name after the clean look they belong to (same set) + marker
            for pr in presets:
                if pr["variant"] and looks:
                    twin = next((q for q in presets if not q["variant"] and q["set"] == pr["set"]), None)
                    pr["name"] = ((twin["name"] if twin else base_name) + " · damaged")
            from collections import Counter as _C
            dup2, idx2 = _C(pr["name"] for pr in presets), _C()
            for pr in presets:
                if dup2[pr["name"]] > 1:
                    idx2[pr["name"]] += 1
                    pr["name"] = f"{pr['name']} ({idx2[pr['name']]})"
        general = find_general(tree, code)
        shared = False
        if not general and code == "cha2":  # Ada's own locomotion lives with the Separate Ways / Mercenaries build
            general = find_general("_mercenaries", "cha8") or find_general("_anotherorder", "cha8")
        if not general and code[:3] in ("cha", "chb"):
            general, shared = find_general("_chainsaw", "cha0"), True
        out.append({
            "id": cid, "code": code, "tree": tree, "dlc": all(pr["dlc"] for pr in presets), "name": base_name, "group": group,
            "skel": find_skel(tree, code), "general": general, "general_shared": shared, "presets": presets,
        })
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(out, indent=1), encoding="utf-8")
    ok = [c for c in out if c["skel"]]
    print(f"{len(out)} characters ({len(ok)} spawnable), {sum(len(c['presets']) for c in out)} presets -> {OUT}")
    for c in out:
        clean = [pr for pr in c["presets"] if not pr["variant"]]
        print(f"  {c['id']:8} {c['code']}  {c['name']:14} presets={len(clean):3}+{len(c['presets']) - len(clean):<3} skel={'yes' if c['skel'] else 'NO '} general={'yes' if c['general'] else 'no'}{'  [DLC]' if c['dlc'] else ''}  {[pr['name'] for pr in clean[:4]]}")


if __name__ == "__main__":
    sys.exit(main())
