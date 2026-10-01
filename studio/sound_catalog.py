"""Search the REasy-generated RE4 Wwise event index without copying it into UI state."""
from __future__ import annotations

import gzip
import json
import threading
from pathlib import Path

INDEX = Path(__file__).resolve().parent.parent / "tools" / "REasy" / "resources" / "data" / "sound" / "re4.json.gz"


def _category(text: str) -> str:
    s = text.lower()
    if any(x in s for x in ("bgm", "music", "stinger")):
        return "music"
    if any(x in s for x in ("voice", "_vo_", "dialog", "talk", "conv", "radio")):
        return "voice"
    if any(x in s for x in ("roomtone", "ambient", "ambience", "environment", "wind", "rain", "river", "waterfall")):
        return "ambience"
    if any(x in s for x in ("gui", "menu", "system_se", "ui_")):
        return "ui"
    if any(x in s for x in ("csa", "cse", "cutscene")):
        return "cutscene"
    return "sfx"


class SoundCatalog:
    def __init__(self, index: Path = INDEX):
        self.index = index
        self._items: list[dict] | None = None
        self._lock = threading.Lock()

    def _load(self):
        if self._items is not None:
            return
        with self._lock:
            if self._items is not None:
                return
            if not self.index.is_file():
                raise FileNotFoundError(f"RE4 sound index is missing: {self.index}")
            with gzip.open(self.index, "rt", encoding="utf-8") as fh:
                raw = json.load(fh)
            names = raw.get("names", {}).get("event", {})
            banks = raw.get("banks", {})
            items = []
            for bank, events in raw.get("bank_events", {}).items():
                bank_info = banks.get(bank) or {}
                paths = bank_info.get("paths") or []
                for event_id, record in events.items():
                    event_names = list(dict.fromkeys((record.get("names") or []) + (names.get(str(event_id)) or [])))
                    title = event_names[0] if event_names else f"Event {event_id}"
                    hay = " ".join([title, *event_names, bank, str(event_id), *(str(x) for x in record.get("trigger_ids") or [])]).lower()
                    items.append({
                        "event": int(event_id), "name": title, "names": event_names[1:4], "bank": bank,
                        "category": _category(hay), "triggers": record.get("trigger_ids") or [],
                        "path": paths[0] if paths else "", "_hay": hay, "_named": bool(event_names),
                    })
            items.sort(key=lambda x: (not x["_named"], x["name"].lower(), x["bank"]))
            self._items = items

    def search(self, query: str = "", category: str = "", limit: int = 200):
        self._load()
        words = query.lower().split()
        out = []
        for item in self._items or []:
            if category and item["category"] != category:
                continue
            if words and not all(word in item["_hay"] for word in words):
                continue
            out.append({k: v for k, v in item.items() if not k.startswith("_")})
            if len(out) >= max(1, min(int(limit), 500)):
                break
        return {"results": out, "total": len(self._items or []), "query": query, "category": category}


catalog = SoundCatalog()
