#!/usr/bin/env python3
"""Everything about one batch run's first divergence from GD, for working through regress.py results.

  python tools/diverge.py <level id> [--regress NAME] [--frame F] [--before 8] [--after 4]
                          [--dx 45] [--dy 60] [--inputs 120]

Prints real (GD) and sim side by side around the first divergence (from regress-NAME.json, default the
newest one, or --frame), with x/y/velocity/mode and the per-frame step (dx, dy) of each, the button
presses as real steps (a gdr input at frame F is pressed before real step F - 1), and the level's objects
around the player at the divergence (sim coords; `pos.y = Y + 15`). Uses runs/<id>/real.txt and the
current sim's runs/<id>/simdbg.txt (written by regress.py).
"""
import argparse
import json
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from gdtools import read_gdr2  # noqa: E402

DATA = Path(__file__).resolve().parent.parent / "build" / "re" / "batch"


def trace(path, shift):
    out = {}
    for line in open(path):
        q = line.split()
        if q and q[0] == "Frame":
            d = dict(zip(q[2::2], q[3::2]))
            out[int(q[1]) - shift] = (float(d["X"]), float(d["Y"]), float(d.get("Vel", 0)), d.get("Mode", "?"))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("id")
    ap.add_argument("--regress")
    ap.add_argument("--frame", type=int)
    ap.add_argument("--before", type=int, default=8)
    ap.add_argument("--after", type=int, default=4)
    ap.add_argument("--dx", type=float, default=45)
    ap.add_argument("--dy", type=float, default=60)
    ap.add_argument("--inputs", type=int, default=120)
    a = ap.parse_args()

    run = DATA / "runs" / a.id
    f0 = a.frame
    if f0 is None:
        scores = sorted(DATA.glob("regress-*.json"), key=lambda p: p.stat().st_mtime)
        path = DATA / f"regress-{a.regress}.json" if a.regress else scores[-1]
        f0 = json.loads(path.read_text(encoding="utf-8"))[a.id]["diverge"]
        print(f"divergence from {path.name}: real frame {f0}")
    real, sim = trace(run / "real.txt", 0), trace(run / "simdbg.txt", 1)

    print(f"{'frame':>6} | {'real X':>11} {'Y':>10} {'dy':>8} {'vel':>9} {'mode':>5} | "
          f"{'sim X':>11} {'Y':>10} {'dy':>8} {'vel':>9} {'mode':>5}")
    for f in range(f0 - a.before, f0 + a.after + 1):
        cols = []
        for t in (real, sim):
            if f in t:
                x, y, v, m = t[f]
                py = t[f - 1][1] if f - 1 in t else y
                cols.append(f"{x:11.4f} {y:10.4f} {y - py:+8.4f} {v:9.3f} {m:>5}")
            else:
                cols.append(f"{'-':>48}")
        print(f"{f:>6}{'<' if f == f0 else ' '}| {cols[0]} | {cols[1]}")

    _, inputs = read_gdr2(run / "macro.gdr2")
    steps = [(i["frame"] - 1, "down" if i["down"] else "up") for i in inputs
             if not i["player2"] and f0 - a.inputs <= i["frame"] - 1 <= f0 + a.after]
    print("button (real steps):", " ".join(f"{s}:{d}" for s, d in steps) or "none in range")

    if f0 in real:
        x, y = real[f0][0], real[f0][1] + 15
        print(f"objects within {a.dx} x / {a.dy} y of ({x:.1f}, {y:.1f}):")
        for o in (DATA / "levels" / f"{a.id}.lvl").read_text(encoding="utf-8").split(";")[1:]:
            kv = o.split(",")
            d = dict(zip(kv[::2], kv[1::2]))
            try:
                ox, oy = float(d["2"]), float(d.get("3", 0))
            except (KeyError, ValueError):
                continue
            if abs(ox - x) <= a.dx and abs(oy - y) <= a.dy:
                extra = " ".join(f"{k}={v}" for k, v in d.items() if k not in ("1", "2", "3"))
                print(f"  id {d['1']:>5} x {ox:9.2f} y {oy:7.2f}  {extra}")


if __name__ == "__main__":
    main()
