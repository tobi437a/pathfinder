"""Benchmark the pathfinder (search speed, not the sim) on the batch's levels, offline.

  python tools/bench.py run NAME[=EXE] [NAME[=EXE] ...] [--seeds 1,2] [--jobs 8] [--time-limit 120] [--ids 1,2]
  python tools/bench.py show NAME [NAME ...]      summary per run, side by side
  python tools/bench.py diff A B                  levels solved / lost, biggest time changes

`run` copies each variant's exe (EXE, or --exe: build/Release/pathfinder-cli.exe; a NAME without
=EXE reuses build/re/bench/NAME.exe if it exists) to build/re/bench/NAME.exe and runs it on every
level of build/re/batch/levels (macros are thrown away) -> build/re/bench/NAME.json. Times are wall
time with `--jobs` runs in parallel and vary a lot with the machine's load and clock: compare
variants run together (they alternate level by level), not runs made at different times.
`simFrames` (sim frames the search ran) doesn't depend on load. A level that isn't solved within the limit counts as the limit in the "all" statistics.
"""
import argparse
import concurrent.futures
import json
import shutil
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
LEVELS = REPO / "build" / "re" / "batch" / "levels"
OUT = REPO / "build" / "re" / "bench"


def run_one(exe, lvl, seed, limit):
    with tempfile.TemporaryDirectory() as tmp:
        try:
            proc = subprocess.run([str(exe), str(lvl), str(Path(tmp) / "m.gdr2"), "--seed", str(seed),
                                   "--time-limit", str(limit)],
                                  capture_output=True, text=True, timeout=limit + 120)
            r = json.loads(proc.stdout.strip().splitlines()[-1])
        except Exception as e:  # noqa: BLE001
            r = {"result": "error", "error": str(e)[:300]}
    r["id"], r["seed"] = lvl.stem, seed
    return r


def cmd_run(a):
    OUT.mkdir(parents=True, exist_ok=True)
    variants = {}  # name -> exe copy
    for v in a.variants:
        name, _, src = v.partition("=")
        exe = OUT / f"{name}.exe"
        if src or not exe.exists():
            shutil.copy(src or a.exe, exe)
        variants[name] = exe
    levels = sorted(LEVELS.glob("*.lvl"), key=lambda p: int(p.stem))
    if a.ids:
        levels = [p for p in levels if p.stem in a.ids.split(",")]
    seeds = [int(s) for s in a.seeds.split(",")]
    # Variants alternate level by level, so they share the machine's load (a laptop's clock varies a lot)
    jobs = [(name, l, s) for s in seeds for l in levels for name in variants]
    # Longest first (by the last benchmarks' time, if any) so the pool doesn't end on one long level
    prev = {}
    for f in OUT.glob("*.json"):
        for r in json.loads(f.read_text()).get("results", []):
            prev[(r["id"], r["seed"])] = max(prev.get((r["id"], r["seed"]), 0), r.get("seconds", 0))
    jobs.sort(key=lambda j: -prev.get((j[1].stem, j[2]), a.time_limit))
    results = {name: [] for name in variants}
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as pool:
        futs = {pool.submit(run_one, variants[n], l, s, a.time_limit): n for n, l, s in jobs}
        for k, f in enumerate(concurrent.futures.as_completed(futs), 1):
            r = f.result()
            results[futs[f]].append(r)
            print(f"\r  {k}/{len(jobs)} {futs[f]:10} {r['id']:>8} s{r['seed']} {r['result']:8} "
                  f"{r.get('seconds', 0):6.1f}s", end="", flush=True)
    print()
    for name in variants:
        data = {"name": name, "jobs": a.jobs, "timeLimit": a.time_limit, "seeds": seeds,
                "together": list(variants), "results": results[name]}
        (OUT / f"{name}.json").write_text(json.dumps(data))
    show(list(variants))


def pct(xs, p):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(p / 100 * len(xs)))]


def summary(data):
    rs = data["results"]
    limit = data["timeLimit"]
    solved = [r for r in rs if r["result"] == "complete"]
    t_all = [r["seconds"] if r["result"] == "complete" else limit for r in rs]
    t_ok = [r["seconds"] for r in solved]
    fr = sum(r.get("simFrames", 0) for r in rs)
    secs = sum(r.get("seconds", 0) for r in rs)
    # The 1% fastest / slowest: mean of that fraction of the runs (at least one)
    k = max(1, len(t_all) // 100)
    s = sorted(t_all)
    return {
        "runs": len(rs),
        "solved": len(solved),
        "errors": sum(r["result"] == "error" for r in rs),
        "total s": sum(t_all),
        "mean s": statistics.mean(t_all),
        "median s": statistics.median(t_all),
        "fastest 1% s": statistics.mean(s[:k]),
        "slowest 1% s": statistics.mean(s[-k:]),
        "p90 s": pct(t_all, 90),
        "solved mean s": statistics.mean(t_ok) if t_ok else 0,
        "solved median s": statistics.median(t_ok) if t_ok else 0,
        "solved slowest 1% s": statistics.mean(sorted(t_ok)[-max(1, len(t_ok) // 100):]) if t_ok else 0,
        "sim Mframes": fr / 1e6,
        "kframes/s": fr / secs / 1e3 if secs else 0,
        "dead ends": sum(r.get("deadEnds", 0) for r in rs),
    }


def load(name):
    return json.loads((OUT / f"{name}.json").read_text())


def show(names):
    sums = [summary(load(n)) for n in names]
    w = max(12, *(len(n) for n in names))
    print(f"{'':22}" + "".join(f"{n:>{w}}" for n in names))
    for key in sums[0]:
        vals = "".join(f"{s[key]:>{w}.2f}" if isinstance(s[key], float) else f"{s[key]:>{w}}" for s in sums)
        print(f"{key:22}{vals}")


def cmd_show(a):
    show(a.names)


def cmd_diff(a):
    A, B = load(a.a), load(a.b)
    ka = {(r["id"], r["seed"]): r for r in A["results"]}
    kb = {(r["id"], r["seed"]): r for r in B["results"]}
    common = sorted(set(ka) & set(kb))
    ok = lambda r: r["result"] == "complete"  # noqa: E731
    gained = [k for k in common if ok(kb[k]) and not ok(ka[k])]
    lost = [k for k in common if ok(ka[k]) and not ok(kb[k])]
    print(f"solved by {a.b} only: {len(gained)} {' '.join(f'{i}:s{s}' for i, s in gained)}")
    print(f"solved by {a.a} only: {len(lost)} {' '.join(f'{i}:s{s}' for i, s in lost)}")
    both = [k for k in common if ok(ka[k]) and ok(kb[k])]
    ch = sorted(both, key=lambda k: kb[k]["seconds"] - ka[k]["seconds"])
    print("biggest speed-ups:", ", ".join(f"{i}:s{s} {ka[i, s]['seconds']:.1f}->{kb[i, s]['seconds']:.1f}"
                                          for i, s in ch[:8]))
    print("biggest slow-downs:", ", ".join(f"{i}:s{s} {ka[i, s]['seconds']:.1f}->{kb[i, s]['seconds']:.1f}"
                                           for i, s in ch[::-1][:8]))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(required=True)
    r = sub.add_parser("run")
    r.add_argument("variants", nargs="+")
    r.add_argument("--seeds", default="1")
    r.add_argument("--jobs", type=int, default=8)
    r.add_argument("--time-limit", type=float, default=120)
    r.add_argument("--exe", default=str(REPO / "build" / "Release" / "pathfinder-cli.exe"))
    r.add_argument("--ids")
    r.set_defaults(f=cmd_run)
    s = sub.add_parser("show")
    s.add_argument("names", nargs="+")
    s.set_defaults(f=cmd_show)
    d = sub.add_parser("diff")
    d.add_argument("a")
    d.add_argument("b")
    d.set_defaults(f=cmd_diff)
    a = p.parse_args()
    a.f(a)


if __name__ == "__main__":
    sys.exit(main())
