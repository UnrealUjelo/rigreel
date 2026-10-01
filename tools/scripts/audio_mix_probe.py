"""Work out which Wwise volume parameter is which, by muting one at a time and listening.

RE4 exposes five volume game parameters (soundlib.SoundManager._VolGameParIdList). This sets each one to 0 in
turn, measures what Windows is still playing, then restores it — the one that silences the game is the master,
the others show up as partial drops depending on what is audible at the time.
"""
from __future__ import annotations

import json
import sys
import time
import urllib.request
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "studio"))
from audio_capture import LoopbackRecorder  # noqa: E402

HOST = "http://127.0.0.1:47931"
TMP = Path(__file__).resolve().parent.parent / "thumbs" / "_audio_probe.wav"
IDS = [3382120907, 1100436643, 1269770271, 1065307318, 2606646140]


def post(path, body):
    req = urllib.request.Request(HOST + path, data=json.dumps(body).encode(), headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=15) as r:
        return json.loads(r.read().decode())


def get(path):
    with urllib.request.urlopen(HOST + path, timeout=15) as r:
        return json.loads(r.read().decode())


def lua(code, timeout_s=6.0):
    cid = post("/send", {"op": "eval", "code": code})["id"]
    t0 = time.time()
    while time.time() - t0 < timeout_s:
        time.sleep(0.08)
        ev = (get("/data") or {}).get("eval") or {}
        if ev.get("id") == cid:
            return ev
    return {"ok": None, "error": "timeout"}


def set_rtpc(rtpc: int, value: float):
    return lua(f'''
local t = sdk.find_type_definition("soundlib.SoundManager")
local go = sdk.call_native_func(nil, t, "get_SystemGameObjId")
local ok, err = pcall(function() t:get_method("setRtpcValue"):call(nil, go, {rtpc}, {value}, false, 0.0, true) end)
return {{ ok = ok, err = tostring(err) }}''')


def level(seconds=1.6):
    """COM has to be initialised on the thread that opens the loopback device."""
    import numpy as np
    try:
        import comtypes
        comtypes.CoInitialize()
    except Exception:
        pass
    rec = LoopbackRecorder(TMP).start()
    time.sleep(seconds)
    rec.stop()
    with wave.open(str(TMP), "rb") as w:
        d = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype("float32") / 32768.0
    return float((d ** 2).mean() ** 0.5) if d.size else 0.0


def main():
    base = level()
    print(f"baseline rms {base:.5f}")
    for rtpc in IDS:
        r = set_rtpc(rtpc, 0.0)
        if not (r.get("result") or {}).get("ok"):
            print(rtpc, "set failed", r)
            continue
        muted = level()
        set_rtpc(rtpc, 100.0)
        time.sleep(0.4)
        back = level()
        drop = 100 * (1 - muted / base) if base else 0
        print(f"{rtpc:>12}  muted {muted:.5f}  ({drop:5.1f}% quieter)   restored {back:.5f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
