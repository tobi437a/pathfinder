#!/usr/bin/env python3
"""Replay batch runs in real GD with GD's own collision log around given frames.

  python tools/gdcoll.py 2240089:8795-8800 2170717:8478-8484 [--no-run]

Plays runs/<id>/macro.gdr2 through tools/verify.ps1 (GD minimised, ~1 min per level) with the job's
"coll" window, so real.txt gets a `Coll` line per PlayerObject::collidedWithObjectInternal call (blocks;
a slope's side) and a `SlopeColl` line per collidedWithSlopeInternal call in those real frames (see
src/verify.cpp). The result goes to build/re/batch/coll/<id>.real.txt (runs/<id>/real.txt is left alone).
--no-run only copies the last results.
"""
import argparse
import json
import os
import shutil
import subprocess
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DATA = REPO / "build" / "re" / "batch"
SAVE = Path(os.environ["LOCALAPPDATA"]) / "GeometryDash" / "geode" / "mods" / "camila314.pathfinder"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("runs", nargs="+", help="<id>:<from>-<to> (real frames)")
    ap.add_argument("--no-run", action="store_true")
    a = ap.parse_args()
    jobs = []
    for r in a.runs:
        lid, _, win = r.partition(":")
        lo, _, hi = win.partition("-")
        jobs.append({"level": lid, "levelFile": str((DATA / "levels" / f"{lid}.txt").resolve()),
                     "macro": str((DATA / "runs" / lid / "macro.gdr2").resolve()),
                     "coll": [int(lo), int(hi or lo)]})
    if not a.no_run:
        job_file = DATA / "coll-jobs.json"
        job_file.write_text(json.dumps({"jobs": jobs}, indent=1))
        subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                        str(REPO / "tools" / "verify.ps1"), str(job_file), "-Minimize"], check=False)
    out = DATA / "coll"
    out.mkdir(exist_ok=True)
    for j in jobs:
        src = SAVE / "verify" / j["level"] / "real.txt"
        if src.exists():
            shutil.copy(src, out / f"{j['level']}.real.txt")
            n = sum(1 for l in src.read_text(errors="replace").splitlines() if l.startswith(("Coll ", "SlopeColl ")))
            print(f"{j['level']}: {n} collision lines -> {out / (j['level'] + '.real.txt')}")
        else:
            print(f"{j['level']}: no real.txt")


if __name__ == "__main__":
    main()
