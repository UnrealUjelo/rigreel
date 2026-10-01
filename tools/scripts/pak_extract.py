"""Extract files from RE4R paks by path (uses REasy's pure-Python PAK reader).

Usage:
  python pak_extract.py <out_dir> <path-or-regex> [more...]
  A pattern starting with 're:' is treated as a regex over the RE4 file list.
"""
import os, re, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REASY = Path(os.environ.get("RIGREEL_REASY", HERE.parent / "REasy-src"))
sys.path.insert(0, str(REASY))

from file_handlers.pak.reader import CachedPakReader  # noqa: E402

GAME = Path(os.environ.get("RIGREEL_GAME_DIR", r"C:\Program Files (x86)\Steam\steamapps\common\RESIDENT EVIL 4  BIOHAZARD RE4"))
FILE_LIST = Path(os.environ.get("RIGREEL_RE4_LIST", HERE.parent.parent / "extract" / "RE4_STM.list"))


def pak_set():
    # Highest priority first: newest patch → base, then DLC paks
    base = GAME / "re_chunk_000.pak"
    patches = sorted(GAME.glob("re_chunk_000.pak.patch_*.pak"), reverse=True)
    dlc = sorted((GAME / "dlc").glob("*.pak"), reverse=True)
    return [str(p) for p in patches + [base] + dlc]


def resolve_patterns(patterns):
    wanted = []
    listed = None
    for pat in patterns:
        if pat.startswith("re:"):
            if listed is None:
                listed = FILE_LIST.read_text(encoding="utf-8", errors="ignore").splitlines()
            rx = re.compile(pat[3:], re.I)
            wanted += [p for p in listed if rx.search(p)]
        else:
            wanted.append(pat)
    return wanted


def main():
    out_dir = Path(sys.argv[1])
    wanted = resolve_patterns(sys.argv[2:])
    print(f"{len(wanted)} file(s) requested")
    reader = CachedPakReader.from_paks(pak_set(), game="re4")
    ok = missing = 0
    for path in wanted:
        buf = reader.get_file(path)
        if buf is None:
            print("  MISSING", path); missing += 1; continue
        dst = out_dir / path
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(buf.getvalue())
        ok += 1
        print(f"  {len(buf.getvalue()):>10,d}  {path}")
    print(f"done: {ok} extracted, {missing} missing")


if __name__ == "__main__":
    main()
