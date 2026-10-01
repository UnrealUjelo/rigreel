"""Listen to the game while poking its sound system, so audio work can be verified without human ears.

    python audio_probe.py --seconds 2                       # how loud is the game right now
    python audio_probe.py --event 969309934 --seconds 3     # post an event, measure it
    python audio_probe.py --rtpc 3382120907 --value 0       # set a volume parameter, then measure

Prints the RMS and peak of what Windows played during the window.
"""
from __future__ import annotations

import argparse
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


def post(path: str, body: dict):
    req = urllib.request.Request(HOST + path, data=json.dumps(body).encode(), headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=15) as r:
        return json.loads(r.read().decode())


def get(path: str):
    with urllib.request.urlopen(HOST + path, timeout=15) as r:
        return json.loads(r.read().decode())


def lua(code: str, timeout_s: float = 6.0):
    cid = post("/send", {"op": "eval", "code": code})["id"]
    t0 = time.time()
    while time.time() - t0 < timeout_s:
        time.sleep(0.08)
        ev = (get("/data") or {}).get("eval") or {}
        if ev.get("id") == cid:
            return ev
    return {"ok": None, "error": "timeout"}


def level(seconds: float):
    import numpy as np
    rec = LoopbackRecorder(TMP).start()
    time.sleep(seconds)
    rec.stop()
    with wave.open(str(TMP), "rb") as w:
        data = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype("float32") / 32768.0
    if data.size == 0:
        return 0.0, 0.0
    return float((data ** 2).mean() ** 0.5), float(abs(data).max())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=float, default=2.0)
    ap.add_argument("--event", type=int)
    ap.add_argument("--trigger", type=int)
    ap.add_argument("--rtpc", type=int)
    ap.add_argument("--value", type=float)
    ap.add_argument("--warmup", type=float, default=0.3)
    args = ap.parse_args()

    if args.rtpc is not None and args.value is not None:
        r = lua(f'''
local t = sdk.find_type_definition("soundlib.SoundManager")
local go = sdk.call_native_func(nil, t, "get_SystemGameObjId")
local m = t:get_method("setRtpcValue")
local ok, err = pcall(function() m:call(nil, go, {args.rtpc}, {args.value}, false, 0.0, false) end)
return {{ ok = ok, err = tostring(err) }}''')
        print("rtpc:", json.dumps(r.get("result")))

    if args.event:
        post("/send", {"op": "sound_preview", "event": args.event, "trigger": args.trigger})
        time.sleep(args.warmup)

    rms, peak = level(args.seconds)
    print(f"rms {rms:.5f}  peak {peak:.4f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
