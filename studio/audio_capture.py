"""Capture the Windows default output device through WASAPI loopback."""
from __future__ import annotations

import threading
import time
import wave
from pathlib import Path


class LoopbackRecorder:
    """Small threaded PCM recorder used for the real-time game-audio pass."""

    def __init__(self, path: str | Path, samplerate: int = 48_000, channels: int = 2, blocksize: int = 2048):
        self.path = Path(path)
        self.samplerate = samplerate
        self.channels = channels
        self.blocksize = blocksize
        self._stop = threading.Event()
        self._ready = threading.Event()
        self._thread: threading.Thread | None = None
        self._error: BaseException | None = None
        self._started_at = 0.0
        self.device = ""

    def start(self, timeout: float = 5.0):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._thread = threading.Thread(target=self._run, name="Director game audio", daemon=True)
        self._thread.start()
        if not self._ready.wait(timeout):
            raise TimeoutError("The Windows loopback recorder did not start")
        if self._error:
            raise RuntimeError(f"Could not capture game audio: {self._error}") from self._error
        return self

    def mark(self) -> float:
        """Return seconds from device capture start to this point."""
        return max(0.0, time.perf_counter() - self._started_at)

    def stop(self, timeout: float = 5.0):
        self._stop.set()
        if self._thread:
            self._thread.join(timeout)
            if self._thread.is_alive():
                raise TimeoutError("The Windows loopback recorder did not stop")
        if self._error:
            raise RuntimeError(f"Game-audio capture failed: {self._error}") from self._error

    def _run(self):
        try:
            import numpy as np
            import soundcard as sc

            # WASAPI is COM: the recording thread needs its own apartment, or a second recording in the
            # same process fails with CO_E_NOTINITIALIZED (0x800401f0).
            try:
                import ctypes
                ctypes.windll.ole32.CoInitializeEx(None, 0x2)   # COINIT_APARTMENTTHREADED
            except Exception:
                pass

            speaker = sc.default_speaker()
            if speaker is None:
                raise RuntimeError("Windows has no default output device")
            mic = sc.get_microphone(speaker.id, include_loopback=True)
            if mic is None:
                raise RuntimeError(f"No loopback device for {speaker.name}")
            self.device = speaker.name
            with wave.open(str(self.path), "wb") as wav:
                wav.setnchannels(self.channels)
                wav.setsampwidth(2)
                wav.setframerate(self.samplerate)
                with mic.recorder(samplerate=self.samplerate, channels=self.channels, blocksize=self.blocksize) as recorder:
                    self._started_at = time.perf_counter()
                    self._ready.set()
                    while not self._stop.is_set():
                        block = recorder.record(numframes=self.blocksize)
                        pcm = (np.clip(block, -1.0, 1.0) * 32767.0).astype("<i2", copy=False)
                        wav.writeframesraw(pcm.tobytes())
        except BaseException as exc:  # propagate device/thread failures to the render worker
            self._error = exc
            self._ready.set()
