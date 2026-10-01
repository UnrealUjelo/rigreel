"""Photograph every prop in the mesh catalogue, one at a time, through the running game.

The runtime's preview rig (bridge.lua) hangs a prop in front of the live camera and sizes it to fit. For the
catalogue we point a camera at empty sky, switch the HUD off and centre the prop in the picture, then grab the
game window once the fit has settled and crop the middle to a square thumbnail.

    python prop_thumbs.py --cat weapon
    python prop_thumbs.py --cat prop --start 0 --count 500
    python prop_thumbs.py --cat prop --resume

Thumbnails: tools/thumbs/<category>/<name>.jpg   Index: tools/thumbs/<category>/index.json

Works against a runtime that has the `thumb_stage` op (state.thumb tells us when a prop is ready) and against
an older one, where the same thing is driven with Lua and the readiness is measured by hand.
"""
from __future__ import annotations

import argparse
import io
import json
import os
import sys
import time
import urllib.request
from pathlib import Path

from PIL import Image, ImageEnhance, ImageOps

HOST = "http://127.0.0.1:47931"
ROOT = Path(__file__).resolve().parent.parent          # tools/
GAME = Path(os.environ.get("RIGREEL_GAME_DIR", r"C:\Program Files (x86)\Steam\steamapps\common\RESIDENT EVIL 4  BIOHAZARD RE4"))
CATALOG = Path(os.environ.get("RIGREEL_MESH_CATALOG", GAME / "reframework" / "data" / "director" / "catalog" / "meshes.json"))
OUT = ROOT / "thumbs"
THUMB = 420           # saved thumbnail size (square)
FIT = 1.15            # how big the prop is drawn, in metres, whatever its real size
ANCHOR = (0.0, 0.0, -1.9)


def post(path: str, body: dict):
    req = urllib.request.Request(HOST + path, data=json.dumps(body).encode(), headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=15) as r:
        return json.loads(r.read().decode())


def get(path: str):
    with urllib.request.urlopen(HOST + path, timeout=15) as r:
        return json.loads(r.read().decode())


def lua(code: str, timeout_s: float = 6.0):
    """Run Lua in the runtime and wait for its result (the bridge writes it into data.json)."""
    cid = post("/send", {"op": "eval", "code": code})["id"]
    t0 = time.time()
    while time.time() - t0 < timeout_s:
        time.sleep(0.08)
        ev = (get("/data") or {}).get("eval") or {}
        if ev.get("id") == cid:
            return ev
    return {"ok": None, "error": "timeout"}


def shot() -> Image.Image:
    with urllib.request.urlopen(HOST + "/screenshot?target=game&scale=1", timeout=25) as r:
        return Image.open(io.BytesIO(r.read())).convert("RGB")


def crop_middle(img: Image.Image) -> Image.Image:
    w, h = img.size
    side = int(h * 0.82)
    left, top = (w - side) // 2, (h - side) // 2
    im = img.crop((left, top, left + side, top + side)).resize((THUMB, THUMB), Image.LANCZOS)
    return develop(im)


def develop(im: Image.Image) -> Image.Image:
    """Props are photographed against the sky, which carries a heavy colour cast (RE4's sky has almost no red,
    so there is nothing to white-balance against). Stretch the levels keeping the tone, then pull most of the
    colour out: what matters in a catalogue picture is the shape and how light or dark the material is."""
    try:
        im = ImageOps.autocontrast(im, cutoff=(0.5, 6), preserve_tone=True)
        return ImageEnhance.Color(im).enhance(0.45)
    except Exception:
        return im


# --- staging ---------------------------------------------------------------------------------------
STAGE_ON = """
local pb = E.player_body()
local p = pb and pb:call("get_Transform"):call("get_Position")
local i = Cam.capture("Director thumbnails")
local c = Cam.cams[i]
c.mode = "static"; c.fov = 45
if p then c.pos = { p.x, p.y + 1.6, p.z } end
c.rot = E.quat_from_euler_deg(38, 0, 0)
Cam.set_active(i)
-- the preview rig lives either in bridge.lua (B.thumb) or in the session's hot patch (__director_preview_cfg)
if Director.Bridge.thumb then Director.Bridge.thumb.on = true end
if _G.__director_preview_cfg then
  _G.__director_preview_cfg.anchor = Vector3f.new(%f, %f, %f)
  _G.__director_preview_cfg.fit = %f
end
return { cam = i, rig = (Director.Bridge.thumb and "builtin") or (_G.__director_preview_cfg and "patch") or "none" }
"""

STAGE_OFF = """
if Director.Bridge.thumb then Director.Bridge.thumb.on = false end
if _G.__director_preview_cfg then
  _G.__director_preview_cfg.anchor = Vector3f.new(0.62, -0.30, -1.7)
  _G.__director_preview_cfg.fit = 0.45
end
Cam.stop()
for i = #Cam.cams, 1, -1 do if Cam.cams[i].name == "Director thumbnails" then Cam.remove(i) end end
return #Cam.cams
"""

STRAYS = """
local scene = sdk.call_native_func(sdk.get_native_singleton("via.SceneManager"), sdk.find_type_definition("via.SceneManager"), "get_CurrentScene")
local n = 0
for i = 1, 8 do
  local go = scene:call("findGameObject(System.String)", "Director_Preview")
  if not go then break end
  pcall(function() go:call("get_Transform"):call("set_Parent", nil); go:call("destroy", go) end)
  n = n + 1
end
return n
"""

PROBE = """
local scene = sdk.call_native_func(sdk.get_native_singleton("via.SceneManager"), sdk.find_type_definition("via.SceneManager"), "get_CurrentScene")
local go = scene and scene:call("findGameObject(System.String)", "Director_Preview")
if not go then return { none = true } end
local comp = go:call("getComponent(System.Type)", sdk.typeof("via.render.Mesh"))
if not comp then return { none = true } end
local ready = comp:call("get_MeshReady") and comp:call("get_MaterialReady")
local box = comp:call("get_WorldAABB")
local lo, hi = box:get_field("minpos"), box:get_field("maxpos")
local span = math.max(hi.x - lo.x, hi.y - lo.y, hi.z - lo.z)
local ls = go:call("get_Transform"):call("get_LocalScale")
return { addr = go:get_address(), ready = ready, span = span, size = { (hi.x-lo.x)/ls.x, (hi.y-lo.y)/ls.x, (hi.z-lo.z)/ls.x } }
"""


def wait_ready(name: str, prev_addr, timeout: float, builtin: bool):
    """Wait until the hovered prop is loaded and the rig has settled on its size."""
    deadline = time.time() + timeout
    good = 0
    size = None
    addr = prev_addr
    while time.time() < deadline:
        if builtin:
            s = (get("/state").get("state") or {})
            t = s.get("thumb") or {}
            if t.get("name") == name and (t.get("stable") or 0) >= 3:
                return True, ((s.get("mesh_preview") or {}) or {}).get("size"), addr
            time.sleep(0.09)
            continue
        r = lua(PROBE, timeout_s=4).get("result") or {}
        if r.get("none"):
            time.sleep(0.05)
            continue
        addr = r.get("addr")
        if addr != prev_addr and r.get("ready") and abs((r.get("span") or 0) / FIT - 1) < 0.1:
            good += 1
            size = r.get("size")
            if good >= 2:
                return True, size, addr
        else:
            good = 0
    return False, size, addr


def load_catalog(cat: str):
    d = json.loads(CATALOG.read_text(encoding="utf-8"))
    meshes = d.get("meshes") or []
    return [m for m in meshes if cat in ("", "all") or m.get("c") == cat]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cat", default="prop")
    ap.add_argument("--start", type=int, default=0)
    ap.add_argument("--count", type=int, default=0)
    ap.add_argument("--resume", action="store_true")
    ap.add_argument("--timeout", type=float, default=5.0)
    ap.add_argument("--keep-stage", action="store_true")
    args = ap.parse_args()

    items = load_catalog(args.cat)
    if args.count:
        items = items[args.start:args.start + args.count]
    elif args.start:
        items = items[args.start:]
    if not items:
        print("nothing to do")
        return 1

    outdir = OUT / args.cat
    outdir.mkdir(parents=True, exist_ok=True)
    index_path = outdir / "index.json"
    index = json.loads(index_path.read_text(encoding="utf-8")) if index_path.exists() else {}

    if not get("/state").get("connected"):
        print("the Director runtime is not connected")
        return 2

    lua(STRAYS)   # leftovers from an earlier session would sit in every picture
    post("/send", {"op": "hud", "value": False})
    builtin = bool(lua("return Director.Bridge.thumb ~= nil").get("result"))
    if builtin:
        post("/send", {"op": "thumb_stage", "value": True})
        time.sleep(0.6)
        rig, stage = "builtin", lua("return { cam = Director.Bridge.thumb.cam, fit = Director.Bridge.thumb.fit }").get("result") or {}
    else:
        stage = lua(STAGE_ON % (ANCHOR[0], ANCHOR[1], ANCHOR[2], FIT)).get("result") or {}
        rig = stage.get("rig")
    print(f"stage ready (rig: {rig}, camera {stage.get('cam')})", flush=True)
    if rig == "none":
        print("no preview rig in the runtime — reset the scripts and try again")
        return 3
    time.sleep(1.0)

    done, missed, addr, t0 = 0, [], None, time.time()
    try:
        for i, m in enumerate(items):
            name = m["n"]
            dest = outdir / f"{name}.jpg"
            if args.resume and dest.exists():
                continue
            post("/send", {"op": "mesh_preview", "mesh": m["p"], "mdf": m.get("mdf"), "name": name})
            ok, size, addr = wait_ready(name, addr, args.timeout, builtin)
            if not ok:
                missed.append(name)
                continue
            try:
                crop_middle(shot()).save(dest, quality=88)
            except Exception as e:
                missed.append(f"{name} ({e})")
                continue
            index[name] = {"path": m["p"], "mdf": m.get("mdf"), "cat": m.get("c"), "size": size}
            done += 1
            if done % 25 == 0:
                index_path.write_text(json.dumps(index, indent=1), encoding="utf-8")
                rate = done / max(0.001, time.time() - t0)
                left = (len(items) - i - 1) / max(0.01, rate)
                print(f"{done}/{len(items)}  {rate:.2f}/s  ~{left/60:.0f} min left  missed {len(missed)}", flush=True)
    finally:
        post("/send", {"op": "mesh_preview_clear"})
        if not args.keep_stage:
            if builtin:
                post("/send", {"op": "thumb_stage", "value": False})
            else:
                lua(STAGE_OFF)
                post("/send", {"op": "hud", "value": True})
        index_path.write_text(json.dumps(index, indent=1), encoding="utf-8")

    print(f"captured {done}, missed {len(missed)}")
    if missed:
        (outdir / "missed.txt").write_text("\n".join(missed), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
