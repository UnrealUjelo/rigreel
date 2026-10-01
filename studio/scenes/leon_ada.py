"""Cinematic: Leon sits on the ground, Ada walks up to him. Slow low dolly with shallow depth of field. Renders 1080p to Downloads.

Everything goes through the Studio control endpoint (same ops the UI uses), so the result is a normal project you can edit.
    python scenes/leon_ada.py [--no-render] [--fps 30] [--out path/to/leon_ada.mp4]
"""
from __future__ import annotations

import argparse
import json
import math
import sys
import time
import urllib.request
from pathlib import Path

BASE = "http://127.0.0.1:47931"
FPS = 60                     # timeline fps
WALK_FRAMES = 300            # 5 s walk-in
HOLD_FRAMES = 150            # 2.5 s hold at the end
LENGTH = WALK_FRAMES + HOLD_FRAMES
ADA_SET = "_mercenaries/animation/ch/cha8/motlist/cha8_general.motlist"   # Ada's own locomotion (Separate Ways / Mercenaries)


def get(p):
    return json.loads(urllib.request.urlopen(BASE + p, timeout=15).read())


def post(p, b):
    r = urllib.request.Request(BASE + p, data=json.dumps(b).encode(), headers={"Content-Type": "application/json"})
    return json.loads(urllib.request.urlopen(r, timeout=60).read())


def send(op, **a):
    return post("/send", {"op": op, **a})


def state():
    st = get("/state")["state"]
    st["actors"] = st.get("actors") or []
    st["cameras"] = st.get("cameras") or []
    st["sequence"] = st.get("sequence") or {}
    st["sequence"]["tracks"] = st["sequence"].get("tracks") or []
    return st


def lua(code, timeout=8):
    cid = send("eval", code=code)["id"]
    t0 = time.time()
    while time.time() - t0 < timeout:
        time.sleep(0.1)
        ev = get("/data").get("eval") or {}
        if ev.get("id") == cid:
            if not ev.get("ok"):
                raise RuntimeError(ev.get("error"))
            return ev.get("result")
    raise TimeoutError(code[:60])


def wait_cast(name, timeout=20):
    t0 = time.time()
    while time.time() - t0 < timeout:
        for a in state()["actors"]:
            c = a.get("cast") or {}
            if c.get("name") == name and c.get("pending", 1) == 0:
                return a
        time.sleep(0.4)
    raise TimeoutError(f"{name} did not finish spawning")


def motions_of(addr, path, timeout=15):
    """Load a motlist on an actor and return its clip list [{id, name, endframe}]."""
    bank = lua(f'local a = Actor.get({addr}); local b = a:load_motlist("{path}"); return b')
    t0 = time.time()
    while time.time() - t0 < timeout:
        r = lua(f'local a = Actor.get({addr}); local b = a.banks[{bank}]; if b and b.ready and b.motions then return {{ ready = true, n = #b.motions }} end; return {{ ready = false }}')
        if r.get("ready"):
            mots = lua(f'local a = Actor.get({addr}); local out = {{}}; for _, m in ipairs(a.banks[{bank}].motions) do out[#out+1] = {{ m.id, m.name, m.endframe }} end; return out')
            return bank, [{"id": m[0], "name": m[1], "endframe": m[2]} for m in mots if isinstance(m, list)]
        time.sleep(0.3)
    raise TimeoutError(path)


def wait_clip(addr, start, timeout=6):
    """The runtime exports state a few frames after a command: wait for the clip that starts at `start` on this actor."""
    t0 = time.time()
    while time.time() - t0 < timeout:
        st = state()
        tr = next((t for t in st["sequence"]["tracks"] if t["kind"] == "anim" and t.get("actor") == addr), None)
        if tr:
            for c in tr.get("clips") or []:
                if abs(c["start"] - start) < 0.5:
                    return tr, c
        time.sleep(0.15)
    raise TimeoutError(f"clip at {start} on {addr} not exported")


def pick(mots, *patterns, exclude=()):
    for pat in patterns:
        for m in mots:
            n = m["name"].lower()
            if pat in n and not any(x in n for x in exclude):
                return m
    return None


def yaw_quat(deg):
    h = math.radians(deg) / 2
    return [math.cos(h), 0.0, math.sin(h), 0.0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(Path.home() / "Downloads" / "leon_ada.mp4"))
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--no-render", action="store_true")
    ns = ap.parse_args()

    st = state()
    if not get("/state")["connected"]:
        sys.exit("runtime not connected")
    player = next((a for a in st["actors"] if a.get("is_player")), None)
    if not player:
        send("refresh")
        time.sleep(1.0)
        # make sure the player is registered as an actor
        lua("local pb = E.player_body(); if pb then Actor.wrap(pb) end; return 1")
        st = state()
        player = next((a for a in st["actors"] if a.get("is_player")), None)
    if not player:
        sys.exit("no player in scene")
    P = player["pos"]
    yaw = math.radians(player["euler"][1])
    f = (math.sin(yaw), 0.0, math.cos(yaw))          # player forward (character +Z rotated by yaw)
    r = (f[2], 0.0, -f[0])                           # right
    def at(fd, rd, up=0.0):
        return [P[0] + f[0] * fd + r[0] * rd, P[1] + up, P[2] + f[2] * fd + r[2] * rd]
    face_back = math.degrees(math.atan2(-f[0], -f[2]))   # yaw facing the player (-forward)
    face_fwd = math.degrees(math.atan2(f[0], f[2]))

    # ---- clean slate: remove earlier takes of this scene ----
    for a in st["actors"]:
        if (a.get("cast") or {}).get("name") in ("Leon (scene)", "Ada (scene)"):
            send("destroy_object", addr=a["id"])
    send("seq_stop")
    time.sleep(0.5)
    st = state()
    for t in sorted(st["sequence"]["tracks"], key=lambda t: -t["idx"]):
        send("remove_track", idx=t["idx"])
    for c in sorted(st["cameras"], key=lambda c: -c["i"]):
        send("cam_remove", i=c["i"])
    time.sleep(0.5)
    send("seq_set", length=LENGTH, loop=False)

    # ---- Leon: sitting 6 m ahead, facing the player ----
    send("spawn_cast", id="ch0a0z0", preset="Default", name="Leon (scene)", no_idle=True)
    leon = wait_cast("Leon (scene)")
    send("select_actor", addr=leon["id"])
    send("set_transform", pos=at(6.0, 0.0), euler=[0, face_back, 0])
    # Leon's gameplay sets have no sitting; the boat idle (hands resting, seated low) reads as sitting on the ground
    sit_path = "_chainsaw/animation/ch/cha0/motlist/cha0_boat.motlist"
    sit_bank, mots = motions_of(leon["id"], sit_path)
    sit = pick(mots, "0300_navi_loop_vera", "navi_loop")
    if not sit:
        sys.exit("boat idle not found")
    print("Leon sits with:", sit["name"])
    send("add_clip", addr=leon["id"], bank=sit_bank, mot=sit["id"], layer=0, name=sit["name"], endframe=sit["endframe"], start=0)
    tr, clip = wait_clip(leon["id"], 0)
    send("update_clip", track=tr["idx"], id=clip["id"], fields={"dur": LENGTH, "loop": False, "blend": 0, "offset": 200})

    # ---- Ada: walks from 1.5 m ahead of the player to 1.2 m in front of Leon ----
    send("spawn_cast", id="ch2a200", preset="Default", name="Ada (scene)", no_idle=True)
    ada = wait_cast("Ada (scene)")
    send("select_actor", addr=ada["id"])
    A0, A1 = at(1.5, -0.6), at(4.9, -0.7)   # a step to the left of Leon's axis so she never blocks him from the camera
    send("set_transform", pos=A0, euler=[0, face_fwd, 0])
    bank, mots = motions_of(ada["id"], ADA_SET)
    walk = pick(mots, "walk_f_loop", "walk_front_loop", "walk_loop", "walk", exclude=("start", "end", "stop", "_b_", "_l_", "_r_", "turn", "stairs", "add", "curve", "diverse"))
    stand = pick(mots, "stand_loop", "idle_loop", "stand", "idle")
    if not walk:
        sys.exit("no walk clip in Ada's set")
    print("Ada walks with:", walk["name"], "then", stand and stand["name"])
    send("add_clip", addr=ada["id"], bank=bank, mot=walk["id"], layer=0, name=walk["name"], endframe=walk["endframe"], start=0)
    tr, wclip = wait_clip(ada["id"], 0)
    send("update_clip", track=tr["idx"], id=wclip["id"], fields={"dur": WALK_FRAMES, "loop": True, "blend": 0, "speed": 1.0})
    if stand:
        send("add_clip", addr=ada["id"], bank=bank, mot=stand["id"], layer=0, name=stand["name"], endframe=stand["endframe"], start=WALK_FRAMES)
        tr, sclip = wait_clip(ada["id"], WALK_FRAMES)
        send("update_clip", track=tr["idx"], id=sclip["id"], fields={"dur": HOLD_FRAMES, "loop": True, "blend": 12})
    # placement keys (explicit values: once a key exists the track drives her, so the live transform can't be trusted)
    def key_pos(addr, t, pos, yaw_deg):
        lua(f"local a = Actor.get({addr}); Seq.key_transform(a, {t}, Vector3f.new({pos[0]}, {pos[1]}, {pos[2]}), E.quat_from_euler_deg(0, {yaw_deg}, 0)); return 1")
    key_pos(ada["id"], 0, A0, face_fwd)
    key_pos(ada["id"], WALK_FRAMES, A1, face_fwd)
    send("seq_set", length=LENGTH, loop=False)
    send("seq_seek", t=0)

    # ---- camera: over-the-shoulder follow. Starts behind Ada's right shoulder, drifts forward slower than she walks,
    # aimed between the two of them; f/2.2 so she walks out of the blur into focus next to Leon ----
    send("select_actor", addr=leon["id"])
    send("cam_lookat")
    for _ in range(20):
        time.sleep(0.2)
        st = state()
        if st["cameras"]:
            break
    cam = st["cameras"][-1]
    ci = cam["i"]
    L = at(6.0, 0.0)
    aim = [(A1[0] - L[0]) * 0.45, 1.05, (A1[2] - L[2]) * 0.45]      # world offset from Leon's root to the aim point
    send("cam_update", i=ci, fields={"name": "Dolly", "fov": 46, "offset": aim})
    send("cam_dof_params", i=ci, f=2.2, focus="target")
    C0, C1 = at(0.2, 0.7, 1.55), at(2.1, 0.9, 1.2)
    lua(f"Cam.cams[{ci}].pos = {{{C0[0]}, {C0[1]}, {C0[2]}}}; Seq.key_camera({ci}, 0); Cam.cams[{ci}].pos = {{{C1[0]}, {C1[1]}, {C1[2]}}}; Seq.key_camera({ci}, {LENGTH}); Cam.cams[{ci}].pos = {{{C0[0]}, {C0[1]}, {C0[2]}}}; return 1")
    send("add_cut", cam=ci, t=0)
    send("cam_live", i=ci)
    send("seq_seek", t=0)
    send("project_save", name="leon_ada")
    time.sleep(0.5)
    print("scene built: Leon sitting, Ada walking in, Dolly camera with f/1.8 focus on Leon; project 'leon_ada' saved")

    if ns.no_render:
        return
    # ---- render ----
    r = post("/render", {"out": ns.out, "fps": ns.fps, "from": 0, "to": LENGTH, "width": 1920, "height": 1080})
    print("render started:", r.get("started"))
    while True:
        time.sleep(1.0)
        p = get("/render")
        print(f"\r  {p.get('stage')} {p.get('frame')}/{p.get('total')}", end="", flush=True)
        if not p.get("running"):
            break
    print()
    if p.get("error"):
        sys.exit("render failed: " + str(p["error"]))
    print("saved", ns.out)


if __name__ == "__main__":
    main()
