#!/usr/bin/env python3
"""Offline helpers for Pathfinder work: GD save files, .gdr2 macros, GeometryDash.bro symbols.

  python tools/gdtools.py levels [filter]            list levels in the save (local + saved online)
  python tools/gdtools.py level <name|id> [-o f.lvl]  extract a decompressed level string
  python tools/gdtools.py gdr2 <macro.gdr2> [-o f.inp]  print header; write the sim's .inp format
  python tools/gdtools.py sym <0xRVA | regex>...     look up GD functions in GeometryDash.bro
  python tools/gdtools.py compare real.txt sim.txt [--tol 0.01] [--shift N]
                                                     first frame where GD and the simulator disagree

The .lvl/.inp pair is exactly what build/gd-sim/<Config>/gd-sim-test.exe takes:
  build/gd-sim/Debug/gd-sim-test.exe level.lvl input.inp
Stdlib only; no dependencies.
"""
import argparse
import base64
import json
import os
import re
import struct
import sys
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SAVE_DIR = Path(os.environ.get("GD_SAVE_DIR", Path(os.environ.get("LOCALAPPDATA", "")) / "GeometryDash"))


# ---------------------------------------------------------------- save files

def decode_save(path: Path) -> str:
    """CC*.dat: XOR 0x0B -> urlsafe base64 -> gzip -> plist-ish XML."""
    raw = bytes(b ^ 0x0B for b in path.read_bytes()).rstrip(b"\0")
    return zlib.decompress(base64.urlsafe_b64decode(raw + b"=" * (-len(raw) % 4)), 47).decode("utf-8", "replace")


def parse_value(el):
    tag = el.tag
    if tag == "d":
        kids = list(el)
        return {kids[i].text: parse_value(kids[i + 1]) for i in range(0, len(kids) - 1, 2)}
    if tag == "t":
        return True
    if tag == "f":
        return False
    if tag == "i":
        return int(el.text or 0)
    if tag == "r":
        return float(el.text or 0)
    return el.text or ""


def load_dict(path: Path) -> dict:
    top = ET.fromstring(decode_save(path)).find("dict")
    top.tag = "d"  # the root dict is spelled <dict>, nested ones <d>
    return parse_value(top)


def decode_level_string(s: str) -> str:
    """Level string k4 / server field 4: urlsafe base64 -> gzip, zlib or raw deflate."""
    if not s:
        return ""
    if s.startswith("kS") or ";" in s[:200]:
        return s  # already decoded
    data = base64.urlsafe_b64decode(s + "=" * (-len(s) % 4))
    try:
        return zlib.decompress(data, 47).decode()  # 47 = auto-detect gzip or zlib header
    except zlib.error:
        return zlib.decompress(data, -15).decode()  # raw deflate


def all_levels():
    """Yield (source, level dict) for every level in the save."""
    local = SAVE_DIR / "CCLocalLevels.dat"
    if local.exists():
        for k, lvl in load_dict(local).get("LLM_01", {}).items():
            if isinstance(lvl, dict):
                yield "local", lvl
    gm = SAVE_DIR / "CCGameManager.dat"
    if gm.exists():
        for k, lvl in load_dict(gm).get("GLM_03", {}).items():
            if isinstance(lvl, dict):
                yield "online", lvl


def cmd_levels(a):
    for src, lvl in all_levels():
        if "k2" not in lvl and "k4" not in lvl:
            continue  # GLM_03 also holds bare id stubs
        name = lvl.get("k2", "?")
        if a.filter and a.filter.lower() not in name.lower():
            continue
        print(f"{src:6}  id={lvl.get('k1', '-')!s:>10}  string={'yes' if lvl.get('k4') else 'NO '}  {name}")


def cmd_level(a):
    matches = [(s, l) for s, l in all_levels()
               if str(l.get("k1", "")) == a.level or l.get("k2", "").lower() == a.level.lower()]
    if not matches:
        matches = [(s, l) for s, l in all_levels() if a.level.lower() in l.get("k2", "").lower()]
    if not matches:
        raise SystemExit(f"no level matching {a.level!r} (try: gdtools.py levels)")
    if len(matches) > 1:
        print("several matches, taking the first:", [m[1].get("k2") for m in matches], file=sys.stderr)
    src, lvl = matches[0]
    s = decode_level_string(lvl.get("k4", ""))
    if not s:
        raise SystemExit("the save has no level string for this level (open it in GD once to download it)")
    safe = re.sub(r"[^\w.-]+", "_", lvl.get("k2", "level"))
    out = Path(a.o or f"{safe}.lvl")
    out.write_text(s, encoding="utf-8")
    print(f"{src} level {lvl.get('k2')!r} id={lvl.get('k1', '-')} -> {out} ({len(s)} chars, "
          f"{s.count(';') - 1} objects)")


# ---------------------------------------------------------------- .gdr2

class Reader:
    """GDReplayFormat binary_reader: integers/bools are LEB128 varints, floats are big-endian."""

    def __init__(self, data: bytes):
        self.d, self.p = data, 0

    def varint(self):
        r = shift = 0
        while True:
            b = self.d[self.p]
            self.p += 1
            r |= (b & 0x7F) << shift
            shift += 7
            if not b & 0x80:
                return r

    def string(self):
        n = self.varint()
        s = self.d[self.p:self.p + n].decode("utf-8", "replace")
        self.p += n
        return s

    def f32(self):
        v = struct.unpack(">f", self.d[self.p:self.p + 4])[0]
        self.p += 4
        return v

    def f64(self):
        v = struct.unpack(">d", self.d[self.p:self.p + 8])[0]
        self.p += 8
        return v

    def empty(self):
        return self.p >= len(self.d)


def read_gdr2(path: Path):
    r = Reader(path.read_bytes())
    if r.d[:3] != b"GDR":
        raise SystemExit(f"{path}: not a GDR2 file")
    r.p = 3
    h = {"version": r.varint(), "inputTag": r.string(), "author": r.string(), "description": r.string(),
         "duration": r.f32(), "gameVersion": r.varint(), "framerate": r.f64(), "seed": r.varint(),
         "coins": r.varint(), "ldm": bool(r.varint()), "platformer": bool(r.varint()),
         "bot": r.string(), "botVersion": r.varint(), "levelId": r.varint(), "levelName": r.string()}
    n = r.varint(); r.p += n  # replay extension
    deaths, p = [], 0
    for _ in range(r.varint()):
        p += r.varint()
        deaths.append(p)
    h["deaths"] = deaths
    r.varint()                             # total input count
    p1_left = r.varint()
    inputs, p = [], 0
    while not r.empty():
        packed = r.varint()
        if h["platformer"]:
            frame, button, down = (packed >> 3) + p, (packed >> 1) & 3, bool(packed & 1)
        else:
            frame, button, down = (packed >> 1) + p, 1, bool(packed & 1)
        if h["inputTag"]:
            n = r.varint(); r.p += n  # per-input extension
        inputs.append({"frame": frame, "button": button, "player2": p1_left == 0, "down": down})
        p = frame
        if p1_left > 0:
            p1_left -= 1
            if p1_left == 0:
                p = 0
    inputs.sort(key=lambda i: i["frame"])
    return h, inputs


def to_inp(inputs) -> str:
    """Same encoding as runTestSim in src/debug.cpp: inp[i] = button held on frame i (player 1, jump)."""
    ins = [i for i in inputs if not i["player2"] and i["button"] == 1]
    if not ins:
        return "0"
    out, hold, k = ["0"], False, 0
    for f in range(1, ins[-1]["frame"]):
        while k < len(ins) and ins[k]["frame"] == f:
            hold = ins[k]["down"]
            k += 1
        out.append("1" if hold else "0")
    return "".join(out)


def cmd_gdr2(a):
    h, inputs = read_gdr2(Path(a.file))
    print(json.dumps({k: v for k, v in h.items() if k != "deaths"}, indent=1))
    print(f"deaths: {len(h['deaths'])}  inputs: {len(inputs)}  last frame: {inputs[-1]['frame'] if inputs else 0}")
    if h["framerate"] != 240:
        print(f"WARNING: framerate {h['framerate']} - the simulator steps at 240 tps", file=sys.stderr)
    if a.o:
        Path(a.o).write_text(to_inp(inputs))
        print(f"-> {a.o}")


# ---------------------------------------------------------------- GeometryDash.bro

def bro_path() -> Path:
    gd = json.loads((REPO / "mod.json").read_text())["gd"]["win"]
    p = REPO / "build" / "_deps" / "bindings-src" / "bindings" / gd / "GeometryDash.bro"
    if not p.exists():
        raise SystemExit(f"{p} missing - configure the build first (it is fetched by CMake)")
    return p


def bro_functions():
    cls = None
    for n, line in enumerate(bro_path().read_text(encoding="utf-8").splitlines(), 1):
        m = re.match(r"^(?:\[\[.*?\]\]\s*)?class\s+([\w:]+)", line)
        if m:
            cls = m.group(1)
            continue
        if line.startswith("}"):
            cls = None
            continue
        w = re.search(r"\bwin\s+(0x[0-9a-fA-F]+)", line)
        if not w or "(" not in line:
            continue
        yield int(w.group(1), 16), (f"{cls}::" if cls else ""), line.strip(), n


def cmd_sym(a):
    funcs = sorted(bro_functions())
    for q in a.query:
        if re.fullmatch(r"(0x)?[0-9a-fA-F]+", q) and q.lower().startswith("0x"):
            v = int(q, 16)
            if v >= 0x140000000:
                v -= 0x140000000
            below = [f for f in funcs if f[0] <= v]
            if not below:
                print(f"{q}: below every known function")
                continue
            rva, cls, line, n = below[-1]
            print(f"{q}: {cls}{line}  (+0x{v - rva:x}, bro line {n})"
                  + ("  [offset is large - may be in an unnamed function after it]" if v - rva > 0x4000 else ""))
        else:
            pat = re.compile(q, re.I)
            for rva, cls, line, n in funcs:
                if pat.search(cls + line):
                    print(f"0x{rva:06x}  {cls}{line}")


# ---------------------------------------------------------------- real vs sim paths

FRAME_RE = re.compile(r"^Frame (\d+) X (\S+) Y (\S+)(?: Vel (\S+))?(?: Accel \S+)?(?: Rot (\S+))?")


def read_path(path: Path):
    """{frame: (x, y, vel, rot)} from a real.txt (GD, written by src/debug.cpp) or a sim.txt."""
    out = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = FRAME_RE.match(line)
        if m:
            f, x, y, v, r = m.groups()
            out[int(f)] = (float(x), float(y), float(v or 0), float(r or 0))
    return out


def cmd_compare(a):
    real, sim = read_path(Path(a.real)), read_path(Path(a.sim))
    frames = sorted(f for f in real if f + a.shift in sim)
    if not frames:
        raise SystemExit("no overlapping frames - check --shift (real frame f is compared to sim frame f+shift)")
    first, maxdx, maxdy = None, (0, 0), (0, 0)
    for f in frames:
        rx, ry, rv, _ = real[f]
        sx, sy, sv, _ = sim[f + a.shift]
        dx, dy = abs(rx - sx), abs(ry - sy)
        maxdx, maxdy = max(maxdx, (dx, f)), max(maxdy, (dy, f))
        if first is None and (dx > a.tol or dy > a.tol):
            first = f
    print(f"real frames {min(real)}..{max(real)}  sim frames {min(sim)}..{max(sim)}  compared {len(frames)}")
    print(f"max |dx| {maxdx[0]:.4f} @ {maxdx[1]}   max |dy| {maxdy[0]:.4f} @ {maxdy[1]}")
    if first is None:
        print(f"no divergence above tol {a.tol}")
        return
    print(f"FIRST DIVERGENCE (tol {a.tol}) at real frame {first}:")
    print(f"{'frame':>7} {'realX':>12} {'simX':>12} {'realY':>11} {'simY':>11} {'realV':>10} {'simV':>10}")
    for f in range(first - a.context, first + a.context + 1):
        if f in real and f + a.shift in sim:
            r, s = real[f], sim[f + a.shift]
            mark = " <" if f == first else ""
            print(f"{f:>7} {r[0]:>12.4f} {s[0]:>12.4f} {r[1]:>11.4f} {s[1]:>11.4f} {r[2]:>10.3f} {s[2]:>10.3f}{mark}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("levels"); p.add_argument("filter", nargs="?"); p.set_defaults(f=cmd_levels)
    p = sub.add_parser("level"); p.add_argument("level"); p.add_argument("-o"); p.set_defaults(f=cmd_level)
    p = sub.add_parser("gdr2"); p.add_argument("file"); p.add_argument("-o"); p.set_defaults(f=cmd_gdr2)
    p = sub.add_parser("sym"); p.add_argument("query", nargs="+"); p.set_defaults(f=cmd_sym)
    p = sub.add_parser("compare"); p.add_argument("real"); p.add_argument("sim")
    p.add_argument("--tol", type=float, default=0.01); p.add_argument("--shift", type=int, default=0)
    p.add_argument("--context", type=int, default=5); p.set_defaults(f=cmd_compare)
    a = ap.parse_args()
    a.f(a)


if __name__ == "__main__":
    main()
