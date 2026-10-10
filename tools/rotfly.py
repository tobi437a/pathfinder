#!/usr/bin/env python3
"""Triage of ship / ufo / wave rotation divergences (regress.py --rotfly), one line per run.

  python tools/rotfly.py [--regress NAME] [--runs runs] [--ids 1,2] [--kind rotfly]

For each run whose first divergence (regress-NAME.json, default the newest) is of --kind, prints the real
frame, GD's and the sim's mode and rotation there and a step before, the turn's target each implies (inverting
the slerp: target = old + (new - old) / t, with t the mode's factor * 0.25), and the angle of the step's travel
in GD (this step's and the last one's), all in GD's clockwise degrees. Then the counts per sim/real mode pair.
Uses runs/<id>/real.txt and the current sim's runs/<id>/simdbg.txt (written by regress.py).
"""
import argparse
import collections
import json
import math
import sys
from pathlib import Path

sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parent.parent
DATA = REPO / "build" / "re" / "batch"


def frames(path, tag="Frame"):
    out = {}
    for line in open(path, encoding="utf-8", errors="replace"):
        q = line.split()
        if q and q[0] == tag:
            d = dict(zip(q[2::2], q[3::2]))
            out[int(q[1])] = (float(d["X"]), float(d["Y"]), float(d.get("Rot", 0)), d.get("Mode", "?"))
    return out


def factor(mode):
    v = mode[:1]
    small = "m" in mode[1:]
    return {"s": 0.15, "u": 0.07, "w": 0.4 if small else 0.25}.get(v, 0)


def target(old, new, mode):
    t = factor(mode) * 0.25
    if not t:
        return float("nan")
    d = (new - old + 180) % 360 - 180
    return old + d / t


def ang(a, b):
    """GD's clockwise angle of the travel from a to b."""
    return -math.degrees(math.atan2(b[1] - a[1], b[0] - a[0]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--regress")
    ap.add_argument("--runs", default="runs")
    ap.add_argument("--ids")
    ap.add_argument("--kind", default="rotfly")
    a = ap.parse_args()
    scores = sorted(DATA.glob("regress-*.json"), key=lambda p: p.stat().st_mtime)
    path = DATA / f"regress-{a.regress}.json" if a.regress else scores[-1]
    reg = json.loads(path.read_text(encoding="utf-8"))
    ids = a.ids.split(",") if a.ids else [k for k, v in reg.items() if v["kind"] == a.kind]
    counts = collections.Counter()
    print(f"{path.name}: {len(ids)} runs")
    for k in ids:
        run = DATA / a.runs / k
        f = reg[k]["diverge"]
        real, sim = frames(run / "real.txt"), frames(run / "simdbg.txt")
        if f not in real or f + 1 not in sim or f - 1 not in real:
            print(k, f, "?")
            continue
        r0, r1, rm = real[f - 1], real[f], real.get(f - 2, real[f - 1])
        s0, s1 = sim[f], sim[f + 1]
        rt = target(r0[2], r1[2], r1[3])
        st = target(-s0[2], -s1[2], s1[3])
        counts[(r0[3], r1[3], s1[3])] += 1
        print(f"{k:>8} {f:>6} real {r0[3]:>4}->{r1[3]:<4} sim {s0[3]:>4}->{s1[3]:<4} "
              f"rot {r1[2]:9.4f} sim {-s1[2]:9.4f} (was {r0[2]:9.4f} {-s0[2]:9.4f}) "
              f"target {rt:8.3f} sim {st:8.3f} travel {ang(r0, r1):8.3f} last {ang(rm, r0):8.3f}")
    print()
    for (a0, a1, b), n in counts.most_common():
        print(f"{n:4}  real {a0}->{a1}  sim {b}")


if __name__ == "__main__":
    main()
