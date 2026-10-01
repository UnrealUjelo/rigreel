"""End-to-end control test of RigReel Studio through the MCP server (stdio), with assertions and screenshots.

Independent of the loaded save: it spawns its own cast member (a puppet Ashley) and removes it at the end.
"""
import asyncio
import base64
import json
import sys
import time
from pathlib import Path

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

HERE = Path(__file__).resolve().parent
SERVER = StdioServerParameters(command=sys.executable, args=[str(HERE / "mcp_server.py")])
SHOTS = HERE / "shots"
SUBJECT = "Test Ashley"
results = []


def ok(name, cond, detail=""):
    results.append((name, bool(cond), detail))
    print(("PASS " if cond else "FAIL ") + name + ("  - " + detail if detail else ""))


async def call(s, name, args=None):
    res = await s.call_tool(name, args or {})
    for c in res.content:
        if getattr(c, "type", "") == "text":
            try:
                return json.loads(c.text)
            except Exception:
                return c.text
        if getattr(c, "type", "") == "image":
            SHOTS.mkdir(exist_ok=True)
            p = SHOTS / f"{args.get('target', 'shot') if args else 'shot'}_{int(time.time() * 10)}.png"
            p.write_bytes(base64.b64decode(c.data))
            return str(p)
    return None


def find_subject(actors):
    return next((x for x in actors if (x.get("cast") or {}).get("name") == SUBJECT), None)


async def full_state(s):
    """Raw runtime state; empty Lua tables arrive as null, so normalise the lists we read."""
    st = (await call(s, "studio_state", {"full": True}))["state"]
    st["actors"] = st.get("actors") or []
    st["sequence"] = st.get("sequence") or {}
    st["sequence"]["tracks"] = st["sequence"].get("tracks") or []
    for a in st["actors"]:
        a["layers"] = a.get("layers") or []
        if a.get("cast"):
            a["cast"]["parts"] = a["cast"].get("parts") or []
    for t in st["sequence"]["tracks"]:
        t["clips"] = t.get("clips") or []
    return st


async def main():
    async with stdio_client(SERVER) as (r, w):
        async with ClientSession(r, w) as s:
            await s.initialize()
            st = await call(s, "studio_state")
            ok("connected", st.get("connected") and st["game"]["embedded"], f"game rect {st['game']['rect']}")

            # 0) leftovers from an aborted run
            for a in (await full_state(s))["actors"]:
                if (a.get("cast") or {}).get("name") == SUBJECT:
                    await call(s, "studio_send", {"op": "destroy_object", "args": {"addr": a["id"]}})
            await call(s, "studio_wait", {"seconds": 0.5})

            # 1) spawn the subject: a puppet Ashley (costume set 00), wait for parts + idle
            cast = (await call(s, "studio_data", {"section": "cast"}))["items"] or []
            ashley_entry = next((c for c in cast if c["id"] == "ch2a1z0" and not c["dlc"]), None)
            looks = [p["name"] for p in (ashley_entry or {}).get("presets") or []]
            ok("cast catalog has Ashley", ashley_entry is not None and bool(ashley_entry.get("skel")) and "Default" in looks, f"looks {looks[:4]}")
            await call(s, "studio_send", {"op": "spawn_cast", "args": {"id": "ch2a1z0", "preset": "Default", "name": SUBJECT}})
            subject = None
            for _ in range(20):
                full = await full_state(s)
                subject = find_subject(full["actors"])
                if subject and subject["cast"].get("pending", 1) == 0 and (subject.get("layers") or [{}])[0].get("name"):
                    break
                await call(s, "studio_wait", {"seconds": 0.5})
            parts = [p.split("/")[-1] for p in (((subject or {}).get("cast") or {}).get("parts") or [])]
            ok("cast: Ashley spawned (Default look)", subject is not None and subject["cast"]["pending"] == 0 and subject["cast"].get("preset_name") == "Default" and len(parts) >= 3, f"parts={parts}")
            ash = {"addr": subject["id"], "name": subject["name"]}
            l0 = (subject.get("layers") or [{}])[0]
            f_a = l0.get("frame") or 0
            await call(s, "studio_wait", {"seconds": 0.7})
            l0b = (find_subject((await full_state(s))["actors"]).get("layers") or [{}])[0]
            ok("cast: idle playing", bool(l0.get("name")) and (l0b.get("frame") or 0) > f_a, f"layer0={l0.get('name')} frame {f_a:.0f} -> {l0b.get('frame', 0):.0f}")
            st = await call(s, "studio_wait", {"seconds": 0.2})
            ok("subject selected", st["selection"].get("actor") == ash["addr"] and any(a["id"] == ash["addr"] for a in st["actors"]))

            # 2) load her general set explicitly via the catalog and play stand_loop
            await call(s, "studio_send", {"op": "catalog_search", "args": {"q": "cha1 general", "group": ""}})
            await call(s, "studio_wait", {"seconds": 0.6})
            cat = (await call(s, "studio_data", {"section": "catalog"}))["items"]
            entry = next((e for e in cat["results"] if e["n"] == "cha1_general"), None)
            ok("catalog search", entry is not None, entry and entry["p"])
            await call(s, "studio_send", {"op": "load_motlist", "args": {"path": entry["p"]}})
            mots = None
            for _ in range(10):
                await call(s, "studio_wait", {"seconds": 0.5})
                motions = (await call(s, "studio_data", {"section": "motions"}))["items"] or {}
                mots = motions.get(str(entry["bank"]))
                if mots:
                    break
            ok("motlist loaded", bool(mots), f"{len(mots or [])} motions")
            stand = next((m for m in mots if "0160_stand_loop" in m["name"]), None)
            await call(s, "studio_send", {"op": "play", "args": {"bank": entry["bank"], "mot": stand["id"], "layer": 0, "blend": 10, "speed": 1}})
            st = await call(s, "studio_wait", {"seconds": 1.0})
            a = next(x for x in st["actors"] if x["id"] == ash["addr"])
            ok("stand_loop playing", a["layer0"] == stand["name"], f"layer0={a['layer0']}")
            await call(s, "studio_screenshot", {"target": "game", "scale": 0.5})

            # 3) timeline clip + play
            st = await call(s, "studio_state")
            await call(s, "studio_send", {"op": "add_clip", "args": {"bank": entry["bank"], "mot": stand["id"], "layer": 0, "name": stand["name"], "endframe": stand["endframe"], "start": 0}})
            await call(s, "studio_send", {"op": "seq_stop"})
            await call(s, "studio_send", {"op": "seq_play"})
            st = await call(s, "studio_wait", {"seconds": 1.5})
            tr = next((t for t in st["sequence"]["tracks"] if t["kind"] == "anim" and t["actor"] == ash["name"]), None)
            ok("clip on timeline + playing", tr is not None and tr["clips"] >= 1 and st["sequence"]["playing"] and st["sequence"]["t"] > 30, f"t={st['sequence']['t']} clips={tr and tr['clips']}")
            await call(s, "studio_send", {"op": "seq_pause"})

            # 4) camera cut live (Camera 1 exists by default)
            await call(s, "studio_send", {"op": "cam_live", "args": {"i": 1}})
            st = await call(s, "studio_wait", {"seconds": 0.5})
            ok("camera 1 live", st["camera_override"] and any(c["live"] for c in st["cameras"]))
            shot_cam = await call(s, "studio_screenshot", {"target": "game", "scale": 0.5})
            await call(s, "studio_send", {"op": "cam_live", "args": {"i": 0}})
            st = await call(s, "studio_wait", {"seconds": 0.3})
            ok("back to game camera", not st["camera_override"])

            # 5) freeze world (while the sequence plays): layer frame must stop advancing, then resume
            await call(s, "studio_send", {"op": "seq_play"})
            await call(s, "studio_wait", {"seconds": 0.5})
            await call(s, "studio_send", {"op": "game_freeze", "args": {"value": True}})
            await call(s, "studio_wait", {"seconds": 0.3})
            f0 = await full_state(s)
            fa = next(x for x in f0["actors"] if x["id"] == ash["addr"])["layers"][0]["frame"]
            await call(s, "studio_wait", {"seconds": 1.0})
            f1 = await full_state(s)
            fb = next(x for x in f1["actors"] if x["id"] == ash["addr"])["layers"][0]["frame"]
            ok("freeze stops animation", abs(fb - fa) < 0.5 and f1["game"].get("frozen"), f"frame {fa:.1f} -> {fb:.1f}")
            await call(s, "studio_send", {"op": "game_freeze", "args": {"value": False}})
            await call(s, "studio_wait", {"seconds": 0.8})
            f2 = await full_state(s)
            fc = next(x for x in f2["actors"] if x["id"] == ash["addr"])["layers"][0]["frame"]
            ok("unfreeze resumes", abs(fc - fb) > 5, f"frame {fb:.1f} -> {fc:.1f}")
            await call(s, "studio_send", {"op": "seq_pause"})

            # 6) placement: move +1 m on X, then face camera
            p0 = a["pos"]
            await call(s, "studio_send", {"op": "set_transform", "args": {"pos": [p0[0] + 1.0, p0[1], p0[2]]}})
            st = await call(s, "studio_wait", {"seconds": 0.6})
            a2 = next(x for x in st["actors"] if x["id"] == ash["addr"])
            ok("moved +1 m", abs(a2["pos"][0] - (p0[0] + 1.0)) < 0.1, f"x {p0[0]:.2f} -> {a2['pos'][0]:.2f}")
            await call(s, "studio_send", {"op": "face_camera"})
            await call(s, "studio_wait", {"seconds": 0.4})

            # 6b) cast: change the whole look to another preset, then back
            other = next((n for n in looks if n != "Default" and "damaged" not in n), None)
            before_parts = list(parts)
            await call(s, "studio_send", {"op": "cast_preset", "args": {"addr": ash["addr"], "preset": other}})
            sub = None
            for _ in range(10):
                await call(s, "studio_wait", {"seconds": 0.5})
                sub = find_subject((await full_state(s))["actors"])
                if sub and sub["cast"].get("pending", 1) == 0 and sub["cast"].get("preset_name") == other:
                    break
            after_parts = [p.split("/")[-1] for p in (sub["cast"]["parts"] or [])]
            ok("cast: change look", sub is not None and sub["cast"].get("preset_name") == other and after_parts and after_parts != before_parts, f"{other}: {after_parts}")
            await call(s, "studio_send", {"op": "cast_preset", "args": {"addr": ash["addr"], "preset": "Default"}})
            await call(s, "studio_wait", {"seconds": 1.5})

            # 7) UI automation: click through the Qt Studio itself (Asset Browser dock)
            await call(s, "studio_eval", {"code": "studio.requestMenu('raise:assets'); 1"})
            r = await call(s, "studio_click", {"text": "Animations", "within": "Asset Browser"})
            ok("ui: Animations tab", (r.get("result") or {}).get("clicked") is not None, str(r))
            await call(s, "studio_type", {"selector_or_placeholder": "Search animations", "text": "ashley general"})
            await call(s, "studio_wait", {"seconds": 0.8})
            r = await call(s, "studio_click", {"text": "Ashley · General", "within": "Asset Browser"})
            ok("ui: open set", (r.get("result") or {}).get("clicked") is not None, str(r)[:120])
            await call(s, "studio_type", {"selector_or_placeholder": "Filter clips", "text": "stand loop"})
            for _ in range(8):  # rows appear once the set has loaded onto the actor
                await call(s, "studio_wait", {"seconds": 0.5})
                rows = (await call(s, "studio_ui_texts", {"filter": "stand loop", "within": "Asset Browser"})).get("result") or []
                if rows:
                    break
            ok("ui: clips listed", len(rows) > 0, f"{len(rows)} 'Stand loop' rows")
            st = await call(s, "studio_state")
            before = next((t["clips"] for t in st["sequence"]["tracks"] if t["kind"] == "anim" and t["actor"] == ash["name"]), 0)
            r = await call(s, "studio_click", {"text": "Add to the timeline", "nth": 0, "within": "Asset Browser"})
            ok("ui: + Timeline on a clip", (r.get("result") or {}).get("clicked") is not None, str(r)[:120])
            st = await call(s, "studio_wait", {"seconds": 0.6})
            after = next((t["clips"] for t in st["sequence"]["tracks"] if t["kind"] == "anim" and t["actor"] == ash["name"]), 0)
            ok("ui: clip count grew", after == before + 1, f"clips {before} -> {after}")
            # the Characters tab lists the cast
            await call(s, "studio_click", {"text": "Characters", "within": "Asset Browser"})
            await call(s, "studio_wait", {"seconds": 0.4})
            await call(s, "studio_click", {"text": "Cast", "within": "Asset Browser"})
            await call(s, "studio_wait", {"seconds": 0.5})
            texts = (await call(s, "studio_ui_texts", {"filter": "Spawn", "within": "Asset Browser"})).get("result") or []
            ok("ui: cast listed", isinstance(texts, list) and len(texts) >= 5, f"{len(texts) if isinstance(texts, list) else texts} spawn buttons")
            shot_ui = await call(s, "studio_screenshot", {"target": "studio", "scale": 0.5})

            # 8) errors?
            errs = await call(s, "studio_eval", {"code": "window.__errs"})
            ok("no UI errors", not errs.get("result"), str(errs.get("result"))[:200])

            # cleanup: remove the subject's track and the subject itself; hand the camera back
            await call(s, "studio_send", {"op": "seq_stop"})
            await call(s, "studio_send", {"op": "cam_live", "args": {"i": 0}})
            full = await full_state(s)
            for t in sorted(full["sequence"]["tracks"], key=lambda t: -t["idx"]):
                if t["kind"] == "anim" and t.get("actor_name") == ash["name"]:
                    await call(s, "studio_send", {"op": "remove_track", "args": {"idx": t["idx"]}})
            await call(s, "studio_send", {"op": "destroy_object", "args": {"addr": ash["addr"]}})
            st = await call(s, "studio_wait", {"seconds": 0.8})
            ok("cast: removed", not any(x["id"] == ash["addr"] for x in st["actors"]) and not any(t["kind"] == "anim" and t["actor"] == ash["name"] for t in st["sequence"]["tracks"]))
            await call(s, "studio_click", {"text": "Scene"})
            print("\nshots:", shot_cam, shot_ui)

    passed = sum(1 for _, c, _ in results if c)
    print(f"\n{passed}/{len(results)} passed")


if __name__ == "__main__":
    asyncio.run(main())
