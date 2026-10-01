"""Lay captured prop thumbnails out on 16:9 contact sheets, 20 per sheet, each numbered and labelled.

    python prop_sheets.py --cat weapon
    python prop_sheets.py --cat prop --from 0 --to 400

Sheets: tools/thumbs/<cat>/sheets/sheet_0001.jpg  (+ sheets.json: which id sits in which cell)
The numbering is what gets read back when naming: "sheet 3, cell 12 = wooden crate".
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent / "thumbs"
COLS, ROWS = 5, 4
SHEET_W, SHEET_H = 1920, 1080
BG = (18, 18, 22)
LABEL_H = 34


def font(size: int):
    for name in ("segoeui.ttf", "arial.ttf", "DejaVuSans.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except Exception:
            continue
    return ImageFont.load_default()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cat", default="prop")
    ap.add_argument("--from", dest="start", type=int, default=0)
    ap.add_argument("--to", dest="end", type=int, default=0)
    args = ap.parse_args()

    src = ROOT / args.cat
    files = sorted(p for p in src.glob("*.jpg") if not p.name.startswith("_"))
    if args.end:
        files = files[args.start:args.end]
    elif args.start:
        files = files[args.start:]
    if not files:
        print("no thumbnails")
        return 1

    out = src / "sheets"
    out.mkdir(exist_ok=True)
    f_small, f_num = font(19), font(22)
    cell_w, cell_h = SHEET_W // COLS, SHEET_H // ROWS
    img_side = min(cell_w, cell_h - LABEL_H) - 8

    sheets = {}
    per = COLS * ROWS
    for si in range(0, len(files), per):
        chunk = files[si:si + per]
        n = si // per + 1
        sheet = Image.new("RGB", (SHEET_W, SHEET_H), BG)
        d = ImageDraw.Draw(sheet)
        cells = {}
        for k, fp in enumerate(chunk):
            cx, cy = (k % COLS) * cell_w, (k // COLS) * cell_h
            im = Image.open(fp).convert("RGB").resize((img_side, img_side), Image.LANCZOS)
            sheet.paste(im, (cx + (cell_w - img_side) // 2, cy + 4))
            d.rectangle([cx + 1, cy + 1, cx + cell_w - 2, cy + cell_h - 2], outline=(52, 52, 62))
            # big cell number, then the asset id
            d.text((cx + 8, cy + 6), str(k + 1), fill=(255, 220, 120), font=f_num)
            d.text((cx + 6, cy + cell_h - LABEL_H + 4), fp.stem, fill=(190, 190, 200), font=f_small)
            cells[str(k + 1)] = fp.stem
        d.text((SHEET_W - 220, SHEET_H - 26), f"{args.cat} · sheet {n}", fill=(120, 120, 132), font=f_small)
        path = out / f"sheet_{n:04d}.jpg"
        sheet.save(path, quality=90)
        sheets[str(n)] = cells
        print(path)

    (out / "sheets.json").write_text(json.dumps(sheets, indent=1), encoding="utf-8")
    print(f"{len(sheets)} sheets, {len(files)} thumbnails")
    return 0


if __name__ == "__main__":
    sys.exit(main())
