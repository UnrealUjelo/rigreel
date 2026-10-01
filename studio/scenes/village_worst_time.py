"""Build and validate the Village Square dialogue scene.

All edits go through RigReel Studio's control endpoint, so the result stays a
normal editable film project.
"""
from __future__ import annotations

import argparse
import json
import math
import time
import urllib.request
from pathlib import Path


BASE = "http://127.0.0.1:47931"
FPS = 60
PROJECT = "Everyone Arrives at the Worst Time"
LENGTH = 2100


def get(path: str):
    return json.loads(urllib.request.urlopen(BASE + path, timeout=20).read())


def post(path: str, body: dict):
    req = urllib.request.Request(
        BASE + path,
        data=json.dumps(body).encode(),
        headers={"Content-Type": "application/json"},
    )
    return json.loads(urllib.request.urlopen(req, timeout=60).read())


def send(op: str, **args):
    return post("/send", {"op": op, **args})


def state():
    data = get("/state")
    if not data.get("connected"):
        raise RuntimeError("RigReel Studio is not connected to the game")
    return data["state"]


def eval_lua(code: str, timeout: float = 20):
    cid = send("eval", code=code)["id"]
    end = time.time() + timeout
    while time.time() < end:
        time.sleep(0.1)
        result = (get("/data").get("eval") or {})
        if result.get("id") == cid:
            if not result.get("ok"):
                raise RuntimeError(result.get("error") or "Lua evaluation failed")
            return result.get("result")
    raise TimeoutError(code[:100])


def wait_actor(name: str, timeout: float = 35):
    end = time.time() + timeout
    while time.time() < end:
        for actor in state().get("actors") or []:
            cast = actor.get("cast") or {}
            if (cast.get("name") == name or actor.get("name") == name) and not cast.get("pending", 0):
                return actor
        time.sleep(0.25)
    raise TimeoutError(f"{name} did not finish spawning")


def ground(points: list[tuple[float, float]], around: float = 1.0):
    chunks = ['local X=require("Director.extras"); local o={}']
    for x, z in points:
        chunks.append(
            f'o[#o+1]={{x={x:.4f},z={z:.4f},y=X.ground_y({x:.4f},{around:.4f},{z:.4f},12,20)}}'
        )
    chunks.append("return o")
    return eval_lua(";".join(chunks))


def motions_of(addr: int, path: str, timeout: float = 25):
    bank = eval_lua(f'local a=Actor.get({addr}); return a:load_motlist("{path}")')
    end = time.time() + timeout
    while time.time() < end:
        ready = eval_lua(
            f'local a=Actor.get({addr}); local b=a.banks[{bank}]; '
            'return {ready=b and b.ready and b.motions~=nil,n=b and b.motions and #b.motions or 0}'
        )
        if ready.get("ready"):
            rows = eval_lua(
                f'local a=Actor.get({addr}); local o={{}}; for _,m in ipairs(a.banks[{bank}].motions) do '
                'o[#o+1]={m.id,m.name,m.endframe} end; return o'
            )
            return bank, [
                {"id": row[0], "name": row[1], "endframe": row[2]}
                for row in rows
                if isinstance(row, list) and len(row) >= 3
            ]
        time.sleep(0.25)
    raise TimeoutError(path)


def compact_matches(motions: list[dict], words: tuple[str, ...], limit: int = 120):
    return [m for m in motions if any(w in m["name"].lower() for w in words)][:limit]


def spawn(cast_id: str, preset: str, name: str):
    send("spawn_cast", id=cast_id, preset=preset, name=name, no_idle=True)
    return wait_actor(name)


def probe_ground():
    points = []
    for z in (-1, 2, 5, 8, 11):
        for x in (-14, -11, -8, -5, -2, 1, 4):
            points.append((x, z))
    print(json.dumps(ground(points), indent=2))


def probe_motions():
    send("project_new", name=PROJECT)
    cast = [
        ("ch0a0z0", "Default", "Leon", "_chainsaw/animation/ch/cha0/motlist/cha0_general.motlist"),
        ("ch2a1z0", "Default", "Ashley", "_chainsaw/animation/ch/cha1/motlist/cha1_general.motlist"),
        ("ch2a200", "Default", "Ada", "_mercenaries/animation/ch/cha8/motlist/cha8_general.motlist"),
        ("ch2a600", "Wesker", "Wesker", "_chainsaw/animation/ch/cha0/motlist/cha0_general.motlist"),
        ("ch1c0z0", "Tom\u00e1s", "Ganado", "_chainsaw/animation/ch/chc0/motlist/0th/chc0_general_0th_com.motlist"),
    ]
    query = (
        "stand", "idle", "talk", "gesture", "look", "turn", "smile", "laugh", "react", "observe",
        "walk_f_loop", "run_f_loop", "jump", "fall", "land", "drop", "nod", "head",
    )
    for cast_id, preset, name, path in cast:
        actor = spawn(cast_id, preset, f"Probe {name}")
        bank, motions = motions_of(actor["id"], path)
        print(json.dumps({"actor": name, "addr": actor["id"], "bank": bank, "path": path,
                          "matches": compact_matches(motions, query)}, indent=2))


def probe_cutscenes():
    st = state()
    ada = next((a for a in st.get("actors") or [] if (a.get("cast") or {}).get("name") == "Probe Ada"), None)
    if not ada:
        ada = spawn("ch2a200", "Default", "Probe Ada")
    wesker = next((a for a in st.get("actors") or [] if (a.get("cast") or {}).get("name") == "Probe Wesker"), None)
    if not wesker:
        wesker = spawn("ch2a600", "Wesker", "Probe Wesker")
    paths = [
        (ada, "Ada", "_chainsaw/event/cs/csa016/csa016_s00/chara/cha200_00/cha200_00.motlist"),
        (ada, "Ada", "_chainsaw/event/cs/csa036/csa036_s00/chara/cha200_00/cha200_00.motlist"),
        (ada, "Ada", "_chainsaw/event/cs/csa054/csa054_s00/chara/cha200_00/cha200_00.motlist"),
        (ada, "Ada", "_chainsaw/event/cs/csa055/csa055_s00/chara/cha200_00/cha200_00.motlist"),
        (ada, "Ada", "_chainsaw/event/cs/csa067/csa067_s00/chara/cha200_00/cha200_00.motlist"),
        (ada, "Ada", "_chainsaw/event/cs/csa071/csa071_s00/chara/cha200_00/cha200_00.motlist"),
        (ada, "Ada", "_chainsaw/event/cs/csa073/csa073_s00/chara/cha200_00/cha200_00.motlist"),
        (ada, "Ada", "_chainsaw/event/cs/csa076/csa076_s00/chara/cha200_00/cha200_00.motlist"),
        (ada, "Ada", "_chainsaw/event/cs/csa092/csa092_s00/chara/cha200_00/cha200_00.motlist"),
        (ada, "Ada", "_chainsaw/event/cs/csa159/csa159_s00/chara/cha200_00/cha200_00.motlist"),
        (ada, "Ada", "_chainsaw/event/cs/csb117/csb117_s00/chara/cha200_00/cha200_00.motlist"),
        (ada, "Ada", "_chainsaw/event/cs/csb118/csb118_s00/chara/cha200_00/cha200_00.motlist"),
        (wesker, "Wesker", "_chainsaw/event/cs/csa076/csa076_s00/chara/cha600_00/cha600_00.motlist"),
    ]
    for actor, who, path in paths:
        try:
            bank, motions = motions_of(actor["id"], path)
            print(json.dumps({"actor": who, "bank": bank, "path": path, "motions": motions}, indent=2))
        except Exception as exc:
            print(json.dumps({"actor": who, "path": path, "error": str(exc)}, indent=2))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("probe-ground", "probe-motions", "probe-cutscenes"))
    args = parser.parse_args()
    if args.command == "probe-ground":
        probe_ground()
    elif args.command == "probe-motions":
        probe_motions()
    else:
        probe_cutscenes()


if __name__ == "__main__":
    main()
