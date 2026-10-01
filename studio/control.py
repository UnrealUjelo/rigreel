"""Screen / window capture helpers for the Python tools (render.py standalone, prop thumbnails).

The Studio itself is now the Qt app (studio/qt, RigReelStudio.exe), which serves the same HTTP routes in C++
(src/core/ControlServer.cpp). The server half of this module belonged to the retired Python host (archived in
archive/react-ui-and-python-host-2026-09-23.zip) and is kept only for reference:
127.0.0.1:47931 (loopback only).
  GET  /state                    -> host+runtime state (same as the UI's get_state)
  GET  /data                     -> runtime data (scene, catalog, motions, joints, ...)
  GET  /flags                    -> host flags
  GET  /log?n=60                 -> runtime log tail
  POST /send      {op, ...}      -> queue a runtime command, returns its id
  POST /eval      {code}         -> evaluate JavaScript in the Studio WebView, returns the JSON result
  POST /focus     {target}       -> "game" | "studio"
  GET  /screenshot?target=game|studio|screen&scale=0.5  -> PNG bytes
"""
from __future__ import annotations

import ctypes
import ctypes.wintypes as wt
import io
import json
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

PORT = 47931

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)
SRCCOPY = 0x00CC0020
CAPTUREBLT = 0x40000000
DIB_RGB_COLORS = 0
BI_RGB = 0


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG), ("biPlanes", wt.WORD), ("biBitCount", wt.WORD),
                ("biCompression", wt.DWORD), ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", wt.LONG), ("biYPelsPerMeter", wt.LONG),
                ("biClrUsed", wt.DWORD), ("biClrImportant", wt.DWORD)]


class BITMAPINFO(ctypes.Structure):
    _fields_ = [("bmiHeader", BITMAPINFOHEADER), ("bmiColors", wt.DWORD * 3)]


def capture_rect(left: int, top: int, width: int, height: int) -> bytes:
    """Screen capture of a rectangle (physical px) -> PNG bytes."""
    from PIL import Image
    hdc = user32.GetDC(None)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, width, height)
    old = gdi32.SelectObject(mem, bmp)
    gdi32.BitBlt(mem, 0, 0, width, height, hdc, left, top, SRCCOPY | CAPTUREBLT)
    bmi = BITMAPINFO()
    bmi.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bmi.bmiHeader.biWidth = width
    bmi.bmiHeader.biHeight = -height  # top-down
    bmi.bmiHeader.biPlanes = 1
    bmi.bmiHeader.biBitCount = 32
    bmi.bmiHeader.biCompression = BI_RGB
    buf = ctypes.create_string_buffer(width * height * 4)
    gdi32.GetDIBits(mem, bmp, 0, height, buf, ctypes.byref(bmi), DIB_RGB_COLORS)
    gdi32.SelectObject(mem, old)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem)
    user32.ReleaseDC(None, hdc)
    img = Image.frombuffer("RGBA", (width, height), buf.raw, "raw", "BGRA", 0, 1)
    out = io.BytesIO()
    img.convert("RGB").save(out, format="PNG", optimize=True)
    return out.getvalue()


def capture_window(hwnd: int) -> bytes:
    """Client area of a window via PrintWindow(PW_RENDERFULLCONTENT): DWM hands us the last composited frame even when the
    window is covered by other windows (the game keeps presenting while the editor or a chat window sits on top)."""
    from PIL import Image
    r = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(r))
    w, h = max(1, r.right), max(1, r.bottom)
    hdc = user32.GetDC(hwnd)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    old = gdi32.SelectObject(mem, bmp)
    ok = user32.PrintWindow(hwnd, mem, 2)
    bmi = BITMAPINFO()
    bmi.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bmi.bmiHeader.biWidth = w
    bmi.bmiHeader.biHeight = -h
    bmi.bmiHeader.biPlanes = 1
    bmi.bmiHeader.biBitCount = 32
    bmi.bmiHeader.biCompression = BI_RGB
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(bmi), DIB_RGB_COLORS)
    gdi32.SelectObject(mem, old)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem)
    user32.ReleaseDC(hwnd, hdc)
    if not ok:
        x, y, ww, hh = window_rect(hwnd)
        return capture_rect(x, y, ww, hh)
    img = Image.frombuffer("RGBA", (w, h), buf.raw, "raw", "BGRA", 0, 1)
    out = io.BytesIO()
    img.convert("RGB").save(out, format="PNG", optimize=False, compress_level=1)
    return out.getvalue()


def window_rect(hwnd: int):
    r = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    return r.left, r.top, r.right - r.left, r.bottom - r.top


def scale_png(png: bytes, scale: float) -> bytes:
    if scale >= 0.999:
        return png
    from PIL import Image
    img = Image.open(io.BytesIO(png))
    img = img.resize((max(1, int(img.width * scale)), max(1, int(img.height * scale))), Image.LANCZOS)
    out = io.BytesIO()
    img.save(out, format="PNG", optimize=True)
    return out.getvalue()


def make_handler(api):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *a):  # quiet
            pass

        def _cors(self):
            # the Studio UI is a file:// page (origin "null"); WebAudio needs CORS to decode the waveform
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Access-Control-Allow-Headers", "Content-Type, Range")
            self.send_header("Access-Control-Expose-Headers", "Content-Length, Content-Range, Accept-Ranges")

        def _json(self, code, obj):
            body = json.dumps(obj, default=str).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self._cors()
            self.end_headers()
            self.wfile.write(body)

        def do_OPTIONS(self):
            self.send_response(204)
            self._cors()
            self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
            self.end_headers()

        MEDIA_TYPES = {".mp3": "audio/mpeg", ".wav": "audio/wav", ".ogg": "audio/ogg", ".m4a": "audio/mp4", ".aac": "audio/aac", ".flac": "audio/flac", ".mp4": "video/mp4", ".webm": "video/webm", ".png": "image/png", ".jpg": "image/jpeg"}

        def _file(self, path: str):
            """Serve a local media file (audio guide track) with HTTP Range support so <audio> can seek."""
            p = Path(path)
            if not p.is_file() or p.suffix.lower() not in self.MEDIA_TYPES:
                return self._json(404, {"error": "no such media file"})
            size = p.stat().st_size
            start, end = 0, size - 1
            rng = self.headers.get("Range")
            partial = False
            if rng and rng.startswith("bytes="):
                a, _, b = rng[6:].partition("-")
                if a:
                    start = int(a)
                    end = int(b) if b else size - 1
                elif b:
                    start = max(0, size - int(b))
                end = min(end, size - 1)
                partial = True
            self.send_response(206 if partial else 200)
            self.send_header("Content-Type", self.MEDIA_TYPES[p.suffix.lower()])
            self.send_header("Accept-Ranges", "bytes")
            self.send_header("Content-Length", str(end - start + 1))
            if partial:
                self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
            self._cors()
            self.end_headers()
            with p.open("rb") as f:
                f.seek(start)
                left = end - start + 1
                while left > 0:
                    chunk = f.read(min(1 << 20, left))
                    if not chunk:
                        break
                    self.wfile.write(chunk)
                    left -= len(chunk)

        def _png(self, data: bytes):
            self.send_response(200)
            self.send_header("Content-Type", "image/png")
            self.send_header("Content-Length", str(len(data)))
            self._cors()
            self.end_headers()
            self.wfile.write(data)

        def _body(self):
            n = int(self.headers.get("Content-Length") or 0)
            raw = self.rfile.read(n) if n else b"{}"
            try:
                return json.loads(raw.decode("utf-8") or "{}")
            except Exception:
                return {}

        def do_GET(self):
            u = urlparse(self.path)
            q = {k: v[0] for k, v in parse_qs(u.query).items()}
            try:
                if u.path == "/state":
                    return self._json(200, api.get_state())
                if u.path == "/data":
                    return self._json(200, api.get_data())
                if u.path == "/sounds":
                    return self._json(200, api.sound_search(q.get("q", ""), q.get("category", ""), int(q.get("limit", "200"))))
                if u.path == "/flags":
                    return self._json(200, {**api.host_flags(), **api.host_info()})
                if u.path == "/render":
                    return self._json(200, api.render_progress())
                if u.path == "/import":
                    return self._json(200, api.import_progress_get())
                if u.path == "/log":
                    return self._json(200, {"log": api.runtime_log(int(q.get("n", "60")))})
                if u.path == "/media":
                    return self._file(q.get("path", ""))
                if u.path == "/screenshot":
                    target = q.get("target", "game")
                    scale = float(q.get("scale", "0.5"))
                    png = api.screenshot(target)
                    return self._png(scale_png(png, scale))
                return self._json(404, {"error": "unknown path"})
            except Exception as e:
                return self._json(500, {"error": repr(e)})

        def do_POST(self):
            u = urlparse(self.path)
            body = self._body()
            try:
                if u.path == "/send":
                    return self._json(200, {"id": api.send(body)})
                if u.path == "/eval":
                    return self._json(200, {"result": api.eval_js(body.get("code", ""))})
                if u.path == "/focus":
                    t = body.get("target", "studio")
                    return self._json(200, {"ok": api.focus_game() if t == "game" else api.focus_studio()})
                if u.path == "/game":
                    act = body.get("action")
                    if act == "render":
                        return self._json(200, api.game_render_mode(True, int(body.get("w", 1920)), int(body.get("h", 1080))))
                    if act == "embed":
                        return self._json(200, api.game_render_mode(False))
                    if act == "release":
                        return self._json(200, api.release_game())
                    return self._json(400, {"error": "action must be render|embed|release"})
                if u.path == "/import":
                    ok = api.import_anim(body.get("path"), body.get("addr"), body.get("fps", 60), body.get("fingers", True), body.get("loop", False), body.get("start"))
                    return self._json(200, {"started": ok, **api.import_progress_get()})
                if u.path == "/pick":
                    return self._json(200, {"path": api.pick_file(body.get("kind", "audio"))})
                if u.path == "/render":
                    ok = api.render(body.get("out"), body.get("fps", 30), body.get("from"), body.get("to"), body.get("width", 1920), body.get("height", 1080), body.get("quality", "final"), body.get("png_seq", False), body.get("output_format", "mp4"), body.get("game_audio", False))
                    return self._json(200, {"started": ok, **api.render_progress()})
                return self._json(404, {"error": "unknown path"})
            except Exception as e:
                return self._json(500, {"error": repr(e)})

    return Handler


def start(api):
    srv = ThreadingHTTPServer(("127.0.0.1", PORT), make_handler(api))
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    print(f"[control] http://127.0.0.1:{PORT}")
    return srv
