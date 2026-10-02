#!/usr/bin/env python3
"""Replay every batch run through the current sim and score it against the recorded real GD path.

  python tools/regress.py [--save NAME] [--base NAME] [--ids 1,2] [--jobs 14] [--show 30]

For each build/re/batch/runs/<id> with a real.txt (written by `batch.py verify`), runs the macro
through the Debug gd-sim-test (runs/<id>/simdbg.txt) and finds the first real frame where GD and
the sim are more than 0.01 apart (as `gdtools compare --shift 1`). Needs no game, so a sim fix can
be checked on every level in a minute: --base compares with a saved score file and lists the
levels that got better (diverge later) or worse; --save writes build/re/batch/regress-NAME.json.
The real paths only cover what the old sim told the pathfinder to do, so a level whose divergence
moves later may still die further on; batch.py pathfind/verify is the real check.
"""
import argparse
import concurrent.futures
import json
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from gdtools import read_gdr2, read_path, to_inp  # noqa: E402

REPO = Path(__file__).resolve().parent.parent
DATA = REPO / "build" / "re" / "batch"
SIM_TEST = REPO / "build" / "gd-sim" / "Debug" / "gd-sim-test.exe"
TOL = 0.01


def score(run: Path):
    """(first diverging real frame or -1, compared frames, sim mode there) for one run."""
    lvl = DATA / "levels" / f"{run.name}.lvl"
    _, inputs = read_gdr2(run / "macro.gdr2")
    inp = run / "simdbg.inp"
    inp.write_text(to_inp(inputs) + "0" * 40000)
    with open(run / "simdbg.txt", "w") as out:
        subprocess.run([str(SIM_TEST), str(lvl), str(inp)], stdout=out, stderr=subprocess.DEVNULL)
    real, sim = read_path(run / "real.txt"), read_path(run / "simdbg.txt")
    # GD keeps recording the frozen player after a death; stop where X stops changing
    last = max(real)
    while last - 1 in real and real[last - 1][0] == real[last][0]:
        last -= 1
    frames = [f for f in sorted(real) if f <= last and f + 1 in sim]
    for f in frames:
        (rx, ry, *_), (sx, sy, *_) = real[f], sim[f + 1]
        if abs(rx - sx) > TOL or abs(ry - sy) > TOL:
            return f, len(frames)
    return -1, len(frames)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--save")
    ap.add_argument("--base")
    ap.add_argument("--ids")
    ap.add_argument("--jobs", type=int, default=14)
    ap.add_argument("--show", type=int, default=30)
    a = ap.parse_args()
    runs = [r for r in sorted((DATA / "runs").iterdir())
            if (r / "real.txt").exists() and (DATA / "levels" / f"{r.name}.lvl").exists()]
    if a.ids:
        want = set(a.ids.split(","))
        runs = [r for r in runs if r.name in want]
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
        results = dict(zip((r.name for r in runs), ex.map(score, runs)))
    out = {k: {"diverge": d, "frames": n} for k, (d, n) in results.items()}
    clean = sum(1 for v in out.values() if v["diverge"] < 0)
    print(f"{len(out)} runs, {clean} match GD to the end of the recording")
    if a.save:
        path = DATA / f"regress-{a.save}.json"
        path.write_text(json.dumps(out, indent=0), encoding="utf-8")
        print(f"-> {path}")
    if a.base:
        base = json.loads((DATA / f"regress-{a.base}.json").read_text(encoding="utf-8"))

        def key(v):  # a run that never diverges is as good as its length
            return v["frames"] if v["diverge"] < 0 else v["diverge"]

        better, worse = [], []
        for k, v in out.items():
            if k not in base:
                continue
            b, n = key(base[k]), key(v)
            if n > b:
                better.append((k, base[k]["diverge"], v["diverge"]))
            elif n < b:
                worse.append((k, base[k]["diverge"], v["diverge"]))
        bclean = sum(1 for k in out if k in base and base[k]["diverge"] < 0)
        print(f"vs {a.base}: {len(better)} better, {len(worse)} worse, clean {bclean} -> {clean}")
        for name, rows in (("WORSE", worse), ("better", better)):
            if rows:
                print(f"{name}: " + "  ".join(f"{k} {b}->{n}" for k, b, n in rows[:a.show]))


if __name__ == "__main__":
    main()
