#!/usr/bin/env python3
"""Replay every batch run through the current sim and score it against the recorded real GD path.

  python tools/regress.py [--save NAME] [--base NAME] [--ids 1,2] [--jobs 14] [--show 30] [--tol 0.01] [--rot] [--p2]

For each build/re/batch/runs/<id> with a real.txt (written by `batch.py verify`), runs the macro
through the Debug gd-sim-test (runs/<id>/simdbg.txt) and finds the first real frame where GD and
the sim are more than 0.01 apart (as `gdtools compare --shift 1`). Needs no game, so a sim fix can
be checked on every level in a minute: --base compares with a saved score file and lists the
levels that got better (diverge later) or worse; --save writes build/re/batch/regress-NAME.json.
--rot also counts a cube or ball whose rotation (mod 90: the hitbox is square) differs by more than
--tol as diverged, for checking the player rotation that rotated objects are tested against.
--rotfly also counts a ship's, ufo's or wave's rotation (the sim's is counter-clockwise, GD's clockwise).
--p2 also compares player 2 in dual mode (P2 lines of the traces).
A run whose sim dies at another step than GD (verify.json "died") counts as diverged at the earlier death.
Each divergence has a kind: p1 / p2 (position), rot, death-gd (GD dies first), death-sim (the sim does).
--runs DIR scores another copy of the runs (e.g. runs-pre1.8, a snapshot kept before re-pathfinding).
The real paths only cover what the old sim told the pathfinder to do, so a level whose divergence
moves later may still die further on; batch.py pathfind/verify is the real check.
"""
import argparse
import collections
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
ROT = False
ROTFLY = False
P2 = False


def modes(path: Path):
    """{frame: mode string} from a trace's Frame lines."""
    out = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("Frame ") and " Mode " in line:
            out[int(line.split()[1])] = line.rsplit(" Mode ", 1)[1].split()[0]
    return out


def p2_path(path: Path):
    """{frame: (x, y)} from a trace's P2 lines (player 2 in dual mode)."""
    out = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("P2 "):
            f = line.split()
            out[int(f[1])] = (float(f[3]), float(f[5]))
    return out


def score(run: Path):
    """(first diverging real frame or -1, compared frames, kind: p1 p2 rot death) for one run."""
    lvl = DATA / "levels" / f"{run.name}.lvl"
    _, inputs = read_gdr2(run / "macro.gdr2")
    inp = run / "simdbg.inp"
    enc = to_inp(inputs)
    inp.write_text(enc + enc[-1] * 40000)  # macros may end holding the button
    with open(run / "simdbg.txt", "w") as out:
        err = subprocess.run([str(SIM_TEST), str(lvl), str(inp)], stdout=out, stderr=subprocess.PIPE, text=True).stderr
    real, sim = read_path(run / "real.txt"), read_path(run / "simdbg.txt")
    # Deaths: GD's (verify.json "died": the last recorded step) and the sim's ("Macro failed", its last frame)
    # must be the same step
    sim_died = "Macro failed" in err
    try:
        real_died = json.loads((run / "verify.json").read_text(encoding="utf-8")).get("result") == "died"
    except (OSError, ValueError):
        real_died = False
    # GD keeps recording the frozen player after a death; stop where X stops changing
    last = max(real)
    while last - 1 in real and real[last - 1][0] == real[last][0]:
        last -= 1
    frames = [f for f in sorted(real) if f <= last and f + 1 in sim]
    # (GD stops the spin of a player it kills: no rotation on its death step)
    rot_last = last - 1 if real_died else last
    real_modes = modes(run / "real.txt") if ROT or ROTFLY else {}
    real2, sim2 = (p2_path(run / "real.txt"), p2_path(run / "simdbg.txt")) if P2 else ({}, {})
    for f in frames:
        (rx, ry, _, rr), (sx, sy, _, sr) = real[f], sim[f + 1]
        if abs(rx - sx) > TOL or abs(ry - sy) > TOL:
            return f, len(frames), "p1"
        if f in real2:
            if f + 1 not in sim2:
                return f, len(frames), "p2"
            (rx2, ry2), (sx2, sy2) = real2[f], sim2[f + 1]
            if abs(rx2 - sx2) > TOL or abs(ry2 - sy2) > TOL:
                return f, len(frames), "p2"
        if ROT and f <= rot_last and real_modes.get(f, "")[:1] in ("c", "b"):
            d = (rr - sr) % 90
            if min(d, 90 - d) > TOL:
                return f, len(frames), "rot"
        if ROTFLY and f <= rot_last and real_modes.get(f, "")[:1] in ("s", "u", "w"):
            d = (rr + sr) % 360
            if min(d, 360 - d) > TOL:
                return f, len(frames), "rotfly"
    if frames:
        real_death = max(real) if real_died else None
        sim_death = max(sim) - 1 if sim_died else None
        if not real_died and sim_death is not None and sim_death > frames[-1]:
            sim_death = None  # gd-sim-test plays on past the level end GD reached
        if real_death != sim_death:
            deaths = [d for d in (real_death, sim_death) if d is not None]
            kind = "death-gd" if real_death is not None and (sim_death is None or real_death < sim_death) else "death-sim"
            return min(min(deaths), frames[-1]), len(frames), kind
    return -1, len(frames), ""


def main():
    global TOL, ROT, ROTFLY, P2
    ap = argparse.ArgumentParser()
    ap.add_argument("--save")
    ap.add_argument("--base")
    ap.add_argument("--ids")
    ap.add_argument("--jobs", type=int, default=14)
    ap.add_argument("--show", type=int, default=30)
    ap.add_argument("--tol", type=float, default=TOL, help="position difference that counts as diverged")
    ap.add_argument("--rot", action="store_true", help="a cube's or ball's rotation (mod 90) counts too")
    ap.add_argument("--rotfly", action="store_true", help="a ship's, ufo's or wave's rotation counts too")
    ap.add_argument("--p2", action="store_true", help="player 2 (dual mode) counts too")
    ap.add_argument("--runs", help="runs directory under build/re/batch (default runs)")
    a = ap.parse_args()
    TOL, ROT, ROTFLY, P2 = a.tol, a.rot, a.rotfly, a.p2
    runs_dir = DATA / a.runs if a.runs else DATA / "runs"
    runs = [r for r in sorted(runs_dir.iterdir())
            if (r / "real.txt").exists() and (DATA / "levels" / f"{r.name}.lvl").exists()]
    if a.ids:
        want = set(a.ids.split(","))
        runs = [r for r in runs if r.name in want]
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
        results = dict(zip((r.name for r in runs), ex.map(score, runs)))
    out = {k: {"diverge": d, "frames": n, "kind": c} for k, (d, n, c) in results.items()}
    clean = sum(1 for v in out.values() if v["diverge"] < 0)
    kinds = collections.Counter(v["kind"] for v in out.values() if v["diverge"] >= 0)
    print(f"{len(out)} runs, {clean} match GD to the end of the recording ("
          + ", ".join(f"{k} {n}" for k, n in kinds.most_common()) + ")")
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
