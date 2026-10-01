"""Dump the bone name table from RE4R .mesh.221108797 files (header-driven, per the Noesis fmt_RE_MESH layout)."""
import struct, sys
from pathlib import Path

def read_cstr(b, off):
    end = b.index(b"\x00", off)
    return b[off:end].decode("ascii", "replace")

def bones_of(path):
    b = Path(path).read_bytes()
    if b[:4] != b"MESH":
        raise SystemExit(f"not a mesh: {path}")
    # RE4 (mesh version 3 in the Noesis plugin's terms): 64-bit offsets table after the 0x?? header.
    # Robust approach: locate the names offset table by scanning for the bone header block instead of
    # hardcoding offsets — read all candidate offsets from the header and pick the one whose targets
    # decode as null-terminated ASCII names.
    hdr = struct.unpack_from("<4sIIH2xIIIQ", b, 0)  # magic, version, fileSize, ..., then offsets follow
    # Scan the first 0x200 bytes for 8-byte offsets that point to an array of 8-byte string offsets.
    best = None
    for o in range(0x10, 0x200, 8):
        (p,) = struct.unpack_from("<Q", b, o)
        if not (0 < p < len(b) - 8) or p % 8:
            continue
        names = []
        q = p
        while q + 8 <= len(b) and len(names) < 2000:
            (so,) = struct.unpack_from("<Q", b, q)
            if not (0 < so < len(b)):
                break
            try:
                s = read_cstr(b, so)
            except ValueError:
                break
            if not s or not all(32 <= ord(c) < 127 for c in s):
                break
            names.append(s); q += 8
        if len(names) > 20 and (best is None or len(names) > len(best)):
            best = names
    return best or []

if __name__ == "__main__":
    sets = {}
    for p in sys.argv[1:]:
        names = bones_of(p)
        key = Path(p).name.split(".")[0]
        # the name table mixes material names + bone names; bones dominate. Keep all for comparison.
        sets[key] = names
        print(f"{key}: {len(names)} names; first 12: {names[:12]}")
    keys = list(sets)
    if len(keys) >= 2:
        base = set(sets[keys[0]])
        for k in keys[1:]:
            other = set(sets[k])
            print(f"\n{keys[0]} & {k}: {len(base & other)} shared | only in {keys[0]}: {len(base - other)} | only in {k}: {len(other - base)}")
            print(f"  sample only-in-{k}: {sorted(other - base)[:25]}")
            print(f"  sample only-in-{keys[0]}: {sorted(base - other)[:25]}")
