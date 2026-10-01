"""RigReel Studio MCP server (stdio).

Exposes the running Studio (RigReelStudio.exe, Qt) to an MCP client: runtime state, commands, UI automation via JavaScript,
screenshots of the game / editor, log access and the built-in self-tests. Requires Studio to be running
(it serves http://127.0.0.1:47931).

Register (Claude Code, project scope): see .mcp.json next to the game exe, or
  claude mcp add director-studio -- "<venv>\\python.exe" "<this file>"
"""
from __future__ import annotations

import json
import time
import urllib.request
from pathlib import Path

from mcp.server.mcpserver import Image, MCPServer

BASE = "http://127.0.0.1:47931"
SHOTS = Path(__file__).resolve().parent / "shots"

mcp = MCPServer("director-studio", instructions=(
    "Controls RigReel Studio, an animation program that makes films with the games the user owns (RE Engine games; "
    "Resident Evil 4 fully). By default the Studio renders the film itself from the game's files (standalone); "
    "studio.runtimeMode 'live' drives the running RE4 through the Director mod instead. "
    "Use studio_state first to see characters/cameras/sequence, studio_send to run runtime commands, "
    "studio_click / studio_eval to drive the Qt Studio UI (panels are docks: 'Animation Set Editor', 'Element Viewer', "
    "'Asset Browser', 'Properties', 'Timeline', 'Console'; menus via within='menu'), and studio_screenshot to look at the "
    "picture (target 'game' = the 3D viewport in standalone, the game window in live) or the editor."))


def _get(path: str, timeout=10):
    with urllib.request.urlopen(BASE + path, timeout=timeout) as r:
        return r.read()


def _get_json(path: str, timeout=10):
    return json.loads(_get(path, timeout).decode("utf-8"))


def _post(path: str, body: dict, timeout=30):
    req = urllib.request.Request(BASE + path, data=json.dumps(body).encode("utf-8"), headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8"))


def _compact_state(h: dict) -> dict:
    st = h.get("state") or {}
    actors = [{"id": a.get("id"), "name": a.get("name"), "kind": a.get("kind"), "player": a.get("is_player"), "ai_off": a.get("puppet"),
               "paused": a.get("paused"), "stay": a.get("root_lock"), "pos": [round(v, 2) for v in (a.get("pos") or [0, 0, 0])],
               "yaw": round((a.get("euler") or [0, 0, 0])[1], 1), "banks": [b.get("name") for b in (a.get("banks") or [])],
               "layer0": (next((l for l in (a.get("layers") or []) if l.get("idx") == 0), {}) or {}).get("name")}
              for a in (st.get("actors") or [])]
    cams = [{"i": c.get("i"), "name": c.get("name"), "mode": c.get("mode"), "fov": round(c.get("fov", 0), 1), "live": c.get("live")} for c in (st.get("cameras") or [])]
    seq = st.get("sequence") or {}
    tracks = [{"idx": t.get("idx"), "kind": t.get("kind"), "actor": t.get("actor_name"), "layer": t.get("layer"),
               "clips": len(t.get("clips") or []), "keys": len(t.get("keys") or []), "cuts": len(t.get("cuts") or [])} for t in (seq.get("tracks") or [])]
    return {
        "connected": h.get("connected"), "game": h.get("game"), "pending": h.get("pending"),
        "selection": st.get("selection"), "actors": actors, "cameras": cams, "camera_override": st.get("camera_override"),
        "sequence": {"length": seq.get("length"), "t": round(seq.get("t") or 0, 1), "playing": seq.get("playing"), "loop": seq.get("loop"), "tracks": tracks},
        "gizmo": st.get("gizmo"), "gamestate": st.get("game"), "tests": st.get("tests"), "test_running": st.get("test_running"),
    }


@mcp.tool()
def studio_state(full: bool = False) -> dict:
    """Current Studio + runtime state: connection, selected actor/camera, actors (name, AI, position), cameras, sequence tracks.
    full=True returns the raw state object."""
    h = _get_json("/state")
    return h if full else _compact_state(h)


@mcp.tool()
def studio_data(section: str = "scene") -> dict:
    """Runtime data as {"section", "items"}. section: scene (nearby characters), objects (nearby props), catalog (last animation search:
    {query, results}), motions (clips of loaded sets for the selected actor, keyed by bank id), joints, poses, projects, meshes (last mesh search), cast (spawnable characters: id, code, name, presets[{name, parts}]), log, all."""
    d = _get_json("/data")
    if section == "all":
        return d
    return {"section": section, "items": d.get(section)}


@mcp.tool()
def studio_send(op: str, args: dict | None = None) -> dict:
    """Queue a runtime command (same ops the UI uses). Examples:
    select_actor {addr}, set_puppet {addr?, value}, catalog_search {q, group?}, load_motlist {path}, play {bank, mot, layer, blend, speed},
    add_clip {bank, mot, layer, name?, endframe?, start?}, seq_play/seq_pause/seq_stop/seq_toggle, seq_seek {t}, seq_set {length, loop},
    add_track {kind: anim|pose|xform|camera, layer?, addr?}, remove_track {idx}, update_clip {track, id, fields}, remove_clip {track, id},
    cam_capture {name?}, cam_orbit, cam_lookat, cam_live {i (0 = game camera)}, cam_fly {value, speed?, sens?} (WASD/QE/mouse fly of the live camera; needs game focus),
    cam_work {value, from_view?} (viewport on the Work Camera: free, never rendered or keyed), cam_scene (viewport follows the film's cuts / shot cameras),
    seq_range {a?, b?, fin?, fout?, clear?} (time selection In/Out and its falloff frames), shot_add {a?, b?, cam?, name?}, shot_update {id, fields}, shot_go {id},
    cam_dof_params {i?, f (aperture) | false, focus: 'target' | metres | false}, key_camera {i?, t?} (camera move key), remove_cam_key {track, t}, hud {value}, cam_update {i, fields}, cam_dof {i, value}, add_cut {cam, t?},
    set_transform {pos?, euler?}, move_to_camera, face_camera, flip_facing, key_transform {t}, lookat_set {fields}, select_joint {name},
    set_joint_euler {name, euler}, keyframe_pose {t}, save_pose {name}, load_pose {name}, gizmo {target, op, mode, snap, enabled},
    game_freeze {value} (time-scale 0: world, AI and the timeline all hold; unfreeze resumes in sync), want_objects {value}, mesh_search {q, cat}, spawn_mesh {mesh, mdf?, name?},
    spawn_cast {id (e.g. ch2a1z0), tree?, preset? (index or name from studio_data('cast')), name?} (a complete game-authored look, no AI; parts attach over ~1-3 s, then an idle plays),
    cast_preset {addr?, preset} (change the look), destroy_object {addr?}, destroy_cast_all,
    project_save {name}, project_load {name}, run_test {name}, release_actor, release_all."""
    body = {"op": op, **(args or {})}
    return _post("/send", body)


@mcp.tool()
def studio_wait(seconds: float = 1.0) -> dict:
    """Wait (max 15 s) and return the compact state afterwards. Use after commands that need frames to take effect."""
    time.sleep(max(0, min(15, seconds)))
    return _compact_state(_get_json("/state"))


@mcp.tool()
def studio_eval(code: str) -> dict:
    """Evaluate JavaScript in the Studio's QML engine. The value of the last expression is returned.
    Globals: `studio` (state, data, cmd(op, args), send(op, args), editor, keySel, ...) and window.__director:
    texts(filter, within) lists visible controls; click(text, nth, within) clicks one; type(placeholder, text) fills a field."""
    return _post("/eval", {"code": code})


@mcp.tool()
def studio_click(text: str, nth: int = 0, within: str = "") -> dict:
    """Click the nth visible UI element (button, tab, list row, chip, menu action) whose text (incl. tooltip) contains `text`
    (case-insensitive). `within` restricts the search to a panel by title, e.g. 'Asset Browser', 'Animation Set', 'Properties',
    'Timeline', or 'menu' for the menu bar (actions read like 'Scene > New Camera from View')."""
    code = f"(window.__director && window.__director.click({json.dumps(text)}, {int(nth)}, {json.dumps(within)})) || 'no __director helper'"
    return _post("/eval", {"code": code})


@mcp.tool()
def studio_ui_texts(filter: str = "", within: str = "") -> dict:
    """List the visible clickable UI texts (buttons, tabs, rows, chips, menu actions), optionally filtered by substring and/or a panel (`within`)."""
    code = f"(window.__director && window.__director.texts({json.dumps(filter)}, {json.dumps(within)})) || 'no __director helper'"
    return _post("/eval", {"code": code})


@mcp.tool()
def studio_type(selector_or_placeholder: str, text: str) -> dict:
    """Type into a text field found by its placeholder text (or objectName), then commit it as if Enter was pressed."""
    code = f"(window.__director && window.__director.type({json.dumps(selector_or_placeholder)}, {json.dumps(text)})) || 'no __director helper'"
    return _post("/eval", {"code": code})


@mcp.tool()
def studio_screenshot(target: str = "game", scale: float = 0.5, save: bool = True) -> Image:
    """Screenshot as PNG. target: game (the docked RE4 picture), studio (whole editor window), screen. scale 0.1-1."""
    png = _get(f"/screenshot?target={target}&scale={max(0.1, min(1.0, scale))}", timeout=30)
    if save:
        SHOTS.mkdir(exist_ok=True)
        (SHOTS / f"{target}_{int(time.time())}.png").write_bytes(png)
    return Image(data=png, format="png")


@mcp.tool()
def studio_lua(code: str, timeout_s: float = 6) -> dict:
    """Run a Lua chunk inside the Director runtime (REFramework) and return what it `return`s (tables/objects summarized).
    Globals available: sdk, E (engine helpers: E.player_body(), E.component(go, type), E.load_prefab(path, cb), E.instantiate(pfb, pos)),
    Actor, Cam, Seq, B (bridge state), Log, Catalog, Pose, dump(v). Async work must be scheduled (E.defer / callbacks) and read back with a later call."""
    cid = _post("/send", {"op": "eval", "code": code})["id"]
    t0 = time.time()
    while time.time() - t0 < timeout_s:
        time.sleep(0.1)
        ev = _get_json("/data").get("eval") or {}
        if ev.get("id") == cid:
            return ev
    return {"ok": None, "error": f"no result within {timeout_s}s (runtime busy or script error; check studio_log)"}


@mcp.tool()
def studio_render(out: str, fps: int = 30, start: int | None = None, end: int | None = None, width: int = 1920, height: int = 1080, game_audio: bool = True) -> dict:
    """Render the sequence frame by frame (the game window is switched to exactly width x height, every frame is captured after
    the engine draws it, ffmpeg encodes an H.264 MP4 at `out`). Returns immediately; poll studio_render_progress."""
    return _post("/render", {"out": out, "fps": fps, "from": start, "to": end, "width": width, "height": height, "game_audio": game_audio}, timeout=60)


@mcp.tool()
def studio_render_progress() -> dict:
    """Progress of the running/last render: running, stage, frame/total, out, error."""
    return _get_json("/render")


@mcp.tool()
def studio_focus(target: str = "studio") -> dict:
    """Give keyboard/mouse focus to 'game' or 'studio' (also frees the cursor)."""
    return _post("/focus", {"target": target})


@mcp.tool()
def studio_log(n: int = 60) -> dict:
    """Tail of the runtime log (Lua side) as {"log": [...]}."""
    return _get_json(f"/log?n={int(n)}")


@mcp.tool()
def studio_run_test(name: str, timeout_s: float = 20) -> dict:
    """Run a built-in runtime self-test and wait for its result. name: pause, layering, cutscene, camera, sequence, pose, placement."""
    before = {t["name"]: t.get("time") for t in (_get_json("/state").get("state", {}).get("tests") or [])}
    _post("/send", {"op": "run_test", "name": name})
    t0 = time.time()
    while time.time() - t0 < timeout_s:
        time.sleep(1.0)
        st = _get_json("/state").get("state", {})
        for t in st.get("tests") or []:
            if t["name"] == name and t.get("time") != before.get(name):
                return t
    return {"name": name, "ok": None, "detail": "timed out waiting for result", "running": _get_json("/state").get("state", {}).get("test_running")}


if __name__ == "__main__":
    mcp.run()
