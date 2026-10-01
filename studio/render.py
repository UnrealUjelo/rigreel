"""Frame-accurate offline render of the Director sequence.

The timeline is frame based, so instead of screen-recording in real time we step the sequence one frame at a time,
let the engine draw it, and save PNGs or encode them with ffmpeg. Live effects advance in native-timeline substeps,
then game time stays held while capture and disk writes happen.

Used by the Studio host (/render) and runnable on its own:
    python render.py --out "C:/path/to/shot.mp4" --fps 30 [--from 0 --to 420] [--width 1920 --height 1080]
Use --format png to save a numbered image sequence without encoding a movie.
Requires: Studio host running (127.0.0.1:47931), the game docked; MP4 output also needs ffmpeg on PATH.
"""
from __future__ import annotations

import argparse
import ctypes
import ctypes.wintypes as wt
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
from pathlib import Path

import control
from audio_capture import LoopbackRecorder

BASE = "http://127.0.0.1:47931"
user32 = ctypes.WinDLL("user32", use_last_error=True)

progress = {"running": False, "frame": 0, "total": 0, "out": None, "error": None, "stage": ""}


def _get(path, timeout=10):
    return json.loads(urllib.request.urlopen(BASE + path, timeout=timeout).read().decode("utf-8"))


def _post(path, body, timeout=30):
    req = urllib.request.Request(BASE + path, data=json.dumps(body).encode("utf-8"), headers={"Content-Type": "application/json"})
    return json.loads(urllib.request.urlopen(req, timeout=timeout).read().decode("utf-8"))


def send(op, **args):
    return _post("/send", {"op": op, **args})


def wait_render_step(token, timeout=5):
    """Wait for the runtime to draw the requested fixed-time simulation step and hold time again."""
    t0 = time.time()
    while time.time() - t0 < timeout:
        st = _get("/state").get("state") or {}
        clock = st.get("render_clock") or {}
        if clock.get("error"):
            raise RuntimeError("Render clock: " + str(clock["error"]))
        if clock.get("active") and clock.get("phase") == "held" and int(clock.get("ready") or 0) >= token:
            return True
        time.sleep(0.01)
    raise TimeoutError("The game did not finish the requested render frame")


def wait_render_clock_stopped(timeout=5):
    """Wait until the runtime has restored the engine timing settings."""
    t0 = time.time()
    while time.time() - t0 < timeout:
        st = _get("/state").get("state") or {}
        clock = st.get("render_clock") or {}
        if not clock.get("active"):
            if clock.get("error"):
                raise RuntimeError("Render clock restore: " + str(clock["error"]))
            return True
        time.sleep(0.01)
    raise TimeoutError("The game did not restore normal timing after rendering")


def _wait_sequence_position(target, timeout=5, stopped=False):
    t0 = time.time()
    latest = None
    while time.time() - t0 < timeout:
        latest = ((_get("/state").get("state") or {}).get("sequence") or {})
        at = float(latest.get("t") or 0)
        if abs(at - target) <= 0.35 and (not stopped or not latest.get("playing")):
            return latest
        time.sleep(0.02)
    raise TimeoutError(f"The timeline did not reach frame {target} during audio capture (last state: {latest})")


def _capture_game_audio(path: Path, a: float, b: float, seq_fps: int, seq: dict):
    """Play the range once in real time and capture the default Windows output mix."""
    old_range = seq.get("range")
    old_loop = bool(seq.get("loop"))
    old_speed = float(seq.get("speed") or 1)
    frozen = bool(((_get("/state").get("state") or {}).get("game") or {}).get("frozen"))
    recorder = LoopbackRecorder(path)
    try:
        send("seq_pause")
        send("seq_speed", value=1)
        send("seq_set", loop=False)
        send("seq_range", a=a, b=b)
        if frozen:
            send("game_freeze", value=False)
        send("seq_seek", t=a)
        _wait_sequence_position(a, timeout=5, stopped=True)
        recorder.start()
        offset = recorder.mark()
        send("seq_play")
        duration = max(0.1, (b - a) / seq_fps)
        _wait_sequence_position(b, timeout=max(10.0, duration * 2.5 + 5), stopped=True)
        return offset, recorder.device
    finally:
        try:
            send("seq_pause")
        except Exception:
            pass
        try:
            recorder.stop()
        finally:
            # Restore editing/playback preferences after the temporary real-time pass.
            try:
                send("seq_speed", value=old_speed)
                send("seq_set", loop=old_loop)
                if old_range:
                    send("seq_range", a=old_range[0], b=old_range[1])
                else:
                    send("seq_range", clear=True)
                if frozen:
                    send("game_freeze", value=True)
            except Exception:
                pass


def _mux_audio(ffmpeg: str, video_only: str, out: str, duration: float, *, game_wav: Path | None = None,
               game_offset: float = 0.0, guide: dict | None = None, seq_start: float = 0.0, seq_fps: int = 60):
    """Mux the captured output mix, or the external guide when output capture is disabled."""
    cmd = [ffmpeg, "-y", "-i", video_only]
    filters, labels = [], []
    idx = 1
    if game_wav and game_wav.is_file():
        cmd += ["-ss", f"{game_offset:.6f}", "-i", str(game_wav)]
        filters.append(f"[{idx}:a]aresample=48000,apad,atrim=duration={duration:.6f},asetpts=PTS-STARTPTS[game]")
        labels.append("[game]")
        idx += 1
    elif guide and guide.get("path") and Path(guide["path"]).is_file():
        off = float(guide.get("offset") or 0)
        skip = max(0.0, (seq_start - off) / seq_fps)
        delay_ms = round(max(0.0, (off - seq_start) / seq_fps) * 1000)
        vol = float(guide.get("volume") if guide.get("volume") is not None else 1.0)
        cmd += ["-ss", f"{skip:.6f}", "-i", guide["path"]]
        filters.append(f"[{idx}:a]volume={vol:.4f},adelay={delay_ms}:all=1,apad,atrim=duration={duration:.6f},asetpts=PTS-STARTPTS[guide]")
        labels.append("[guide]")
    if not labels:
        shutil.copy2(video_only, out)
        return
    cmd += ["-filter_complex", ";".join(filters), "-map", "0:v:0", "-map", labels[0], "-c:v", "copy", "-c:a", "aac", "-b:a", "192k",
            "-t", f"{duration:.6f}", "-movflags", "+faststart", out]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("audio mux: " + result.stderr[-600:])


def render(out: str, fps: int = 30, start: int | None = None, end: int | None = None, width: int = 1920, height: int = 1080,
           hide_hud: bool = True, keep_frames: bool = False, api=None, quality: str = "final", png_seq: bool = False,
           output_format: str = "mp4", game_audio: bool = False):
    """Blocking. `api` (host Api) lets us switch the game window in-process; standalone mode uses the /game endpoint."""
    progress.update(running=True, frame=0, total=0, out=out, error=None, stage="preparing", png_dir=None, output_format=output_format)
    tmp = None
    restore_game = False
    clock_active = False
    game_audio_wav = None
    game_audio_offset = 0.0
    try:
        if output_format not in ("mp4", "png"):
            raise ValueError("Output format must be mp4 or png")
        if not out or fps <= 0:
            raise ValueError("An output name and positive frame rate are required")
        ffmpeg = shutil.which("ffmpeg") if output_format == "mp4" else None
        if output_format == "mp4" and not ffmpeg:
            raise RuntimeError("ffmpeg not found on PATH")
        st = _get("/state")
        seq = (st.get("state") or {}).get("sequence") or {}
        seq_fps = int(seq.get("fps") or 60)
        length = int(seq.get("length") or 600)
        rng = seq.get("range") or None
        a = (rng[0] if rng else 0) if start is None else max(0, int(start))
        b = (min(length, rng[1]) if rng else length) if end is None else min(length, int(end))
        audio = seq.get("audio") or None
        # Fractional timeline positions preserve the requested output rate (e.g. 60 Hz timeline -> 2.5 frames at 24 fps).
        count = int((b - a) * fps / seq_fps) + 1
        frames = [a + i * seq_fps / fps for i in range(max(0, count))]
        if not frames:
            raise ValueError("The render range is empty")
        progress.update(total=len(frames))
        if output_format == "png":
            # Write directly to a fresh permanent folder; preserve completed frames on failure.
            base = Path(out).with_suffix("")
            base = base.parent / (base.name + "_frames")
            base.parent.mkdir(parents=True, exist_ok=True)
            frame_dir = base
            suffix = 1
            while True:
                try:
                    frame_dir.mkdir()
                    break
                except FileExistsError:
                    suffix += 1
                    frame_dir = base.with_name(f"{base.name}_{suffix}")
            progress.update(out=str(frame_dir), png_dir=str(frame_dir))
        else:
            tmp = Path(tempfile.mkdtemp(prefix="director_render_"))
            frame_dir = tmp
            if game_audio:
                progress.update(stage="recording audio", audio_device=None)
                game_audio_wav = tmp / "game_mix.wav"
                game_audio_offset, device = _capture_game_audio(game_audio_wav, a, b, seq_fps, seq)
                progress.update(audio_device=device)
        restore_game = True
        game_mode = _post("/game", {"action": "render", "w": width, "h": height}) if api is None else api.game_render_mode(True, width, height)
        # quiet the editor: no gizmos / camera markers, HUD off, sequence stopped at the first frame
        send("gizmo", enabled=False)
        send("show_cameras", value=False)
        if hide_hud:
            send("hud", value=False)
        send("seq_pause")
        send("render_clock_begin", fps=fps)
        clock_active = True
        # The first frame is evaluated and drawn at zero elapsed simulation time.
        send("render_clock_step", token=1, t=frames[0], seconds=0)
        wait_render_step(1)
        rect = game_mode.get("rect") if isinstance(game_mode, dict) else None
        hwnd = (game_mode.get("hwnd") if isinstance(game_mode, dict) else None) or (_get("/state").get("game") or {}).get("hwnd")
        x, y, w, h = rect or (0, 0, width, height)
        progress.update(stage="capturing")
        previous = frames[0]
        token = 1
        for i, f in enumerate(frames):
            if i:
                # Keep physics steps at or below the sequence's native frame.
                # GPU cloth is a fixed-step solver and becomes unstable when a
                # 30 fps capture jumps across two 60 Hz timeline frames at once.
                # Intermediate steps are simulated but only the requested
                # output frame is captured.
                while f - previous > 1e-7:
                    step_frames = min(1.0, f - previous)
                    previous = min(f, previous + step_frames)
                    token += 1
                    send("render_clock_step", token=token, t=previous, seconds=step_frames / seq_fps)
                    wait_render_step(token)
            png = control.capture_window(hwnd) if hwnd else control.capture_rect(x, y, w, h)
            (frame_dir / f"f{i:05d}.png").write_bytes(png)
            progress.update(frame=i + 1)
        if output_format == "png":
            return True
        progress.update(stage="encoding")
        Path(out).parent.mkdir(parents=True, exist_ok=True)
        preset, crf = ("veryfast", "26") if quality == "draft" else ("slow", "16")
        cmd = [ffmpeg, "-y", "-framerate", str(fps), "-i", str(tmp / "f%05d.png"), "-c:v", "libx264", "-preset", preset, "-crf", crf,
               "-pix_fmt", "yuv420p", "-movflags", "+faststart", out]
        if png_seq:
            # keep every frame next to the movie: <out>_frames/f00000.png ...
            seq_dir = Path(out).with_suffix("")
            seq_dir = seq_dir.parent / (seq_dir.name + "_frames")
            seq_dir.mkdir(parents=True, exist_ok=True)
            for p in tmp.glob("f*.png"):
                shutil.copy2(p, seq_dir / p.name)
            progress.update(png_dir=str(seq_dir))
        has_audio = bool(game_audio_wav and game_audio_wav.is_file()) or bool(audio and audio.get("path") and Path(audio["path"]).is_file())
        if has_audio:
            # Encode video first. The output mix was recorded during real-time playback;
            # when that is disabled, the external guide is muxed directly instead.
            video_only = str(tmp / "video.mp4")
            cmd[-1] = video_only
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            progress.update(error=r.stderr[-800:])
            return False
        if has_audio:
            progress.update(stage="audio")
            _mux_audio(ffmpeg, video_only, out, len(frames) / fps, game_wav=game_audio_wav, game_offset=game_audio_offset,
                       guide=audio, seq_start=a, seq_fps=seq_fps)
        return True
    except Exception as e:  # noqa: BLE001
        progress.update(error=repr(e))
        return False
    finally:
        if restore_game:
            # One failed restore must not prevent the other cleanup or leave progress stuck.
            restores = []
            if clock_active:
                def restore_clock():
                    send("render_clock_end")
                    wait_render_clock_stopped()
                restores.append(restore_clock)
            restores.extend([lambda: send("gizmo", enabled=True), lambda: send("show_cameras", value=True)])
            if hide_hud:
                restores.append(lambda: send("hud", value=True))
            restores.append(lambda: _post("/game", {"action": "embed"}) if api is None else api.game_render_mode(False, width, height))
            for restore in restores:
                try:
                    restore()
                except Exception as e:
                    progress.update(error=progress["error"] or f"Could not restore the editor: {e}")
        if tmp is not None and not keep_frames:
            shutil.rmtree(tmp, ignore_errors=True)
        progress.update(running=False, stage="error" if progress["error"] else "done")


def render_async(**kw):
    if progress["running"]:
        return False
    threading.Thread(target=render, kwargs=kw, daemon=True).start()
    return True


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--from", dest="start", type=int, default=None)
    ap.add_argument("--to", dest="end", type=int, default=None)
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--height", type=int, default=1080)
    ap.add_argument("--keep-frames", action="store_true")
    ap.add_argument("--game-audio", action="store_true")
    ap.add_argument("--format", choices=("mp4", "png"), default="mp4")
    ns = ap.parse_args()
    ok = render(ns.out, ns.fps, ns.start, ns.end, ns.width, ns.height, keep_frames=ns.keep_frames, output_format=ns.format, game_audio=ns.game_audio)
    print(json.dumps(progress), file=sys.stderr)
    sys.exit(0 if ok else 1)
