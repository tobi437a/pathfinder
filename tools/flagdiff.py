#!/usr/bin/env python3
"""First frame where GD's and the sim's player state flags disagree, per batch run.

  python tools/flagdiff.py [--ids 1,2] [--fields og2,rotating,slope,boosted,acc] [--p2] [--show 40]

Compares the verifier's `Flags` lines in runs/<id>/real.txt (player 1; `Flags2` = player 2 with --p2)
with gd-sim-test's (PF_FLAGS=1) for the same macro, frame by frame (real frame N = sim frame N + 1),
up to the first position divergence of regress-<--regress>.json (default: the newest), and prints the
first frame where one of --fields differs for 3 frames running. Flags often go wrong before positions do. Only runs verified
with a verifier that writes Flags lines are compared.
"""
import argparse
import concurrent.futures
import json
import os
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parent.parent
DATA = REPO / "build" / "re" / "batch"
SIM_TEST = REPO / "build" / "gd-sim" / "Debug" / "gd-sim-test.exe"
SHIFT = 1
PERSIST = 3


def flags(lines, tag, shift):
    out = {}
    for line in lines:
        if line.startswith(tag + " "):
            q = line.split()
            out[int(q[1]) - shift] = dict(zip(q[2::2], q[3::2]))
    return out


def check(run, tag, fields, limit):
    real_lines = (run / "real.txt").read_text(encoding="utf-8", errors="replace").splitlines()
    real = flags(real_lines, tag, 0)
    if not real:
        return None
    env = dict(os.environ, PF_FLAGS="1")
    out = subprocess.run([str(SIM_TEST), str(DATA / "levels" / f"{run.name}.lvl"), str(run / "simdbg.inp")],
                         capture_output=True, text=True, env=env).stdout
    sim = flags(out.splitlines(), tag, SHIFT)

    def bad(f):
        return [k for k in fields if f in sim and real[f].get(k) != sim[f].get(k)]

    for f in sorted(real):
        if f < 3:
            continue  # the start state
        if limit is not None and limit >= 0 and f >= limit:
            break
        if f not in sim:
            break
        # Only a difference that lasts PERSIST frames: GD and the sim handle presses and landings a step apart
        b = bad(f)
        if b and all(set(b) & set(bad(f + i)) for i in range(1, PERSIST) if f + i in real):
            return f, {k: (real[f].get(k), sim[f].get(k)) for k in b}
    return -1, {}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ids")
    ap.add_argument("--fields", default="og,og2,rotating,slope,boosted,acc")
    ap.add_argument("--p2", action="store_true")
    ap.add_argument("--regress")
    ap.add_argument("--show", type=int, default=40)
    ap.add_argument("--jobs", type=int, default=14)
    a = ap.parse_args()
    scores = sorted(DATA.glob("regress-*.json"), key=lambda p: p.stat().st_mtime)
    reg = json.loads((DATA / f"regress-{a.regress}.json" if a.regress else scores[-1]).read_text(encoding="utf-8"))
    runs = [r for r in sorted((DATA / "runs").iterdir()) if (r / "real.txt").exists() and (r / "simdbg.inp").exists()]
    if a.ids:
        want = set(a.ids.split(","))
        runs = [r for r in runs if r.name in want]
    fields = a.fields.split(",")
    tag = "Flags2" if a.p2 else "Flags"
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
        res = dict(zip((r.name for r in runs),
                       ex.map(lambda r: check(r, tag, fields, reg.get(r.name, {}).get("diverge")), runs)))
    res = {k: v for k, v in res.items() if v is not None}
    bad = sorted((v[0], k, v[1]) for k, v in res.items() if v[0] >= 0)
    print(f"{len(res)} runs with Flags, {len(bad)} with a flag difference before the position divergence")
    for f, k, d in bad[:a.show]:
        print(f"{k} {f:6d}  " + "  ".join(f"{n} real {r} sim {s}" for n, (r, s) in d.items()))


if __name__ == "__main__":
    main()
