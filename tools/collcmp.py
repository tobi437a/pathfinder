#!/usr/bin/env python3
"""GD's collision log (tools/gdcoll.py) next to the sim's per-object Trace lines, per real frame.

  python tools/collcmp.py <id> [from] [to]

GD: `Coll`/`SlopeColl` lines from build/re/batch/coll/<id>.real.txt (block / slope collisions, player y
before -> after). Sim: gd-sim-test's `Trace` lines (PF_TRACE=1: every object collision, player y after) for
the same real frame (sim frame = real frame + 1), plus both Frame lines. Without from/to: the window the log
was recorded for.
"""
import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DATA = REPO / "build" / "re" / "batch"


def main():
    lid = sys.argv[1]
    real = (DATA / "coll" / f"{lid}.real.txt").read_text(errors="replace").splitlines()
    coll = [l for l in real if l.startswith(("Coll ", "SlopeColl "))]
    frames = sorted({int(l.split()[1]) for l in coll})
    lo = int(sys.argv[2]) if len(sys.argv) > 2 else (frames[0] if frames else 0)
    hi = int(sys.argv[3]) if len(sys.argv) > 3 else (frames[-1] if frames else 0)
    run = DATA / "runs" / lid
    env = dict(os.environ, PF_TRACE="1")
    sim = subprocess.run([str(REPO / "build" / "gd-sim" / "Debug" / "gd-sim-test.exe"), str(DATA / "levels" / f"{lid}.lvl"),
                          str(run / "simdbg.inp")], capture_output=True, text=True, env=env).stdout.splitlines()
    by = {}
    for l in real:
        f = l.split()
        if len(f) > 1 and f[0] in ("Coll", "SlopeColl", "Frame", "P2") and f[1].isdigit():
            by.setdefault(int(f[1]), {}).setdefault("gd", []).append(l)
    for l in sim:
        f = l.split()
        if len(f) > 1 and f[0] in ("Trace", "Frame", "P2") and f[1].isdigit():
            fr = int(f[1]) - (1 if f[0] in ("Frame", "P2") else 1)
            by.setdefault(fr, {}).setdefault("sim", []).append(l)
    for fr in range(lo, hi + 1):
        print(f"--- real frame {fr}")
        for l in by.get(fr, {}).get("gd", []):
            print("  GD ", l[:170])
        for l in by.get(fr, {}).get("sim", []):
            print("  SIM", l[:170])


if __name__ == "__main__":
    main()
