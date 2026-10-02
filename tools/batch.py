#!/usr/bin/env python3
"""Batch-test the pathfinder + simulator on rated online levels against real GD.

  python tools/batch.py fetch [--max-id 1619692] [--cached-only]   rated levels with id <= max-id -> index.json
  python tools/batch.py download                     candidates' level data (raw response + .lvl)
  python tools/batch.py pathfind [--time-limit 120] [--jobs 14]   pathfinder-cli on each candidate
  python tools/batch.py verify [--chunk 200]         play every macro in GD (tools/verify.ps1)
  python tools/batch.py report                       table of results, worst first -> report.md

Candidates are levels whose gameVersion (GD version of their last upload) is <= --max-game-version
(default 11: 7 = early 1.x, 10/11 = 1.7, 18 = 1.8, ...), so levels updated later are skipped.
Every stage caches per level and skips what's done (--redo to force); --ids 123,456 limits a stage.
Data lives in build/re/batch/ (gitignored): index.json, levels/<id>.txt|.lvl, runs/<id>/.
Needs build/Release/pathfinder-cli.exe:  cmake --build build --config Release --target pathfinder-cli
Stdlib only.
"""
import argparse
import concurrent.futures
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

sys.dont_write_bytecode = True  # tools/__pycache__ is tracked in git
sys.path.insert(0, str(Path(__file__).resolve().parent))
from gdtools import decode_level_string  # noqa: E402

REPO = Path(__file__).resolve().parent.parent
DATA = REPO / "build" / "re" / "batch"
CLI = REPO / "build" / "Release" / "pathfinder-cli.exe"
MOD_SAVE = Path(os.environ.get("LOCALAPPDATA", "")) / "GeometryDash" / "geode" / "mods" / "camila314.pathfinder"
SERVER = "https://www.boomlings.com/database/"
# Seconds between server requests. 1 s gets "429 Too Many Requests" after ~100 requests; every 429
# makes the gap longer for the rest of the run. The servers ban IPs that keep hammering them.
_gap = 2.0
_last_request = 0.0


def post(endpoint, **params):
    """POST to the GD servers like the game does (they reject requests with a User-Agent)."""
    global _last_request, _gap
    data = urllib.parse.urlencode(dict(secret="Wmfd2893gb7", gameVersion=22, binaryVersion=45, **params)).encode()
    for attempt in range(8):
        wait = _last_request + _gap - time.time()
        if wait > 0:
            time.sleep(wait)
        _last_request = time.time()
        try:
            req = urllib.request.Request(SERVER + endpoint, data=data, headers={"User-Agent": ""})
            body = urllib.request.urlopen(req, timeout=30).read().decode("utf-8", "replace")
            if body.strip() == "-1" or body.startswith("error"):
                return None
            return body
        except Exception as e:  # noqa: BLE001 - network errors, 429s: back off and retry
            if "429" in str(e):
                _gap = min(_gap * 1.5, 10)
            pause = 60 * (attempt + 1)
            print(f"\n  {endpoint}: {e}; retrying in {pause}s (gap now {_gap:.1f}s)", file=sys.stderr)
            time.sleep(pause)
    raise RuntimeError(f"{endpoint} kept failing")


def parse_kv(s, sep=":"):
    f = s.split(sep)
    return dict(zip(f[::2], f[1::2]))


def search_page(page, cached=True):
    """Rated levels (star=1), newest id first (type=4), 10 per page. Returns (levels, total).
    Pages are cached in pages/ so an interrupted fetch resumes (newly rated levels shift pages a
    little over time; fetch dedupes by id)."""
    cache = DATA / "pages" / f"{page}.txt"
    if cached and cache.exists():
        body = cache.read_text(encoding="utf-8")
    else:
        body = post("getGJLevels21.php", type=4, star=1, page=page, str="")
        if body:
            cache.parent.mkdir(parents=True, exist_ok=True)
            cache.write_text(body, encoding="utf-8")
    if not body:
        return [], 0
    parts = body.split("#")
    levels = [parse_kv(l) for l in parts[0].split("|") if l]
    total = int(parts[3].split(":")[0]) if len(parts) > 3 else 0
    return levels, total


def load_index():
    path = DATA / "index.json"
    if not path.exists():
        sys.exit("no index yet - run: python tools/batch.py fetch")
    return json.loads(path.read_text(encoding="utf-8"))


def candidates(args, index=None):
    index = index or load_index()
    out = [l for l in index if int(l.get("13", 0)) <= args.max_game_version]
    if args.ids:
        ids = set(args.ids.split(","))
        out = [l for l in out if l["1"] in ids]
    return out


def run_dir(level_id):
    d = DATA / "runs" / str(level_id)
    d.mkdir(parents=True, exist_ok=True)
    return d


# ---------------------------------------------------------------- stages

def cmd_fetch(a):
    if a.cached_only:
        pages = sorted(int(p.stem) for p in (DATA / "pages").glob("*.txt"))
        print(f"building the index from {len(pages)} cached pages")
    else:
        _, total = search_page(0, cached=False)
        last = (total - 1) // 10
        print(f"{total} rated levels, {last + 1} pages")

        lo, hi = 0, last  # first page whose last (smallest) id is <= max-id
        while lo < hi:
            mid = (lo + hi) // 2
            levels, _ = search_page(mid)
            if levels and int(levels[-1]["1"]) > a.max_id:
                lo = mid + 1
            else:
                hi = mid
        print(f"ids <= {a.max_id} start on page {lo}; fetching {last - lo + 1} pages")
        pages = range(lo, last + 1)

    seen = {}
    for page in pages:
        for l in search_page(page)[0]:
            if int(l["1"]) <= a.max_id:
                l.pop("3", None)  # description
                seen[l["1"]] = l
        print(f"\r  page {page}: {len(seen)} levels", end="", flush=True)
    print()

    index = sorted(seen.values(), key=lambda l: int(l["1"]))
    DATA.mkdir(parents=True, exist_ok=True)
    (DATA / "index.json").write_text(json.dumps(index, indent=1), encoding="utf-8")
    by_gv = {}
    for l in index:
        by_gv[l.get("13")] = by_gv.get(l.get("13"), 0) + 1
    print(f"{len(index)} levels -> {DATA / 'index.json'}")
    print("gameVersion counts:", dict(sorted(by_gv.items(), key=lambda kv: int(kv[0] or 0))))
    print(f"candidates (gameVersion <= {a.max_game_version}): {len(candidates(a, index))}")


def cmd_download(a):
    todo = [l for l in candidates(a) if a.redo or not (DATA / "levels" / f"{l['1']}.lvl").exists()]
    (DATA / "levels").mkdir(parents=True, exist_ok=True)
    print(f"downloading {len(todo)} levels (~{len(todo) * _gap / 60:.0f}+ min)")
    for n, l in enumerate(todo, 1):
        body = post("downloadGJLevel22.php", levelID=l["1"])
        if not body:
            print(f"\n  {l['1']} {l['2']}: download failed")
            continue
        (DATA / "levels" / f"{l['1']}.txt").write_text(body, encoding="utf-8")
        fields = parse_kv(body.split("#")[0])
        (DATA / "levels" / f"{l['1']}.lvl").write_text(decode_level_string(fields.get("4", "")), encoding="utf-8")
        print(f"\r  {n}/{len(todo)} {l['1']} {l['2'][:30]:30}", end="", flush=True)
    print()


def pathfind_one(level, a):
    lvl = DATA / "levels" / f"{level['1']}.lvl"
    out = run_dir(level["1"])
    try:
        proc = subprocess.run([str(CLI), str(lvl), str(out / "macro.gdr2"), "--seed", str(a.seed),
                               "--time-limit", str(a.time_limit)],
                              capture_output=True, text=True, timeout=a.time_limit + 120)
        result = json.loads(proc.stdout.strip().splitlines()[-1])
    except Exception as e:  # noqa: BLE001 - crash, bad level string, hang
        result = {"result": "error", "error": str(e)[:300]}
    (out / "pathfind.json").write_text(json.dumps(result))
    return level, result


def cmd_pathfind(a):
    if not CLI.exists():
        sys.exit(f"missing {CLI}; build it: cmake --build build --config Release --target pathfinder-cli")
    todo = [l for l in candidates(a)
            if (DATA / "levels" / f"{l['1']}.lvl").exists()
            and (a.redo or not (DATA / "runs" / l["1"] / "pathfind.json").exists())]
    print(f"pathfinding {len(todo)} levels on {a.jobs} workers (limit {a.time_limit}s each)")
    counts = {}
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as pool:
        for n, (level, r) in enumerate(pool.map(lambda l: pathfind_one(l, a), todo), 1):
            counts[r["result"]] = counts.get(r["result"], 0) + 1
            pct = 100 * r.get("x", 0) / max(r.get("length", 1), 1)
            print(f"  {n}/{len(todo)} {level['1']:>8} {level['2'][:28]:28} {r['result']:8} {pct:5.1f}%"
                  f" {r.get('seconds', 0):6.1f}s")
    print(counts)


def cmd_verify(a):
    todo = [l for l in candidates(a)
            if (DATA / "runs" / l["1"] / "macro.gdr2").exists()
            and (a.redo or not (DATA / "runs" / l["1"] / "verify.json").exists())]
    print(f"verifying {len(todo)} levels in GD, {a.chunk} per GD launch")
    for i in range(0, len(todo), a.chunk):
        chunk = todo[i:i + a.chunk]
        jobs = {"jobs": [{"level": l["1"],
                          "levelFile": str((DATA / "levels" / f"{l['1']}.txt").resolve()),
                          "macro": str((DATA / "runs" / l["1"] / "macro.gdr2").resolve())} for l in chunk]}
        job_file = DATA / "verify-jobs.json"
        job_file.write_text(json.dumps(jobs, indent=1))
        print(f"  GD run {i // a.chunk + 1}: {len(chunk)} levels")
        subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                        str(REPO / "tools" / "verify.ps1"), str(job_file), "-Minimize",
                        "-TimeoutSeconds", str(60 * 60 * 3)], check=False)

        results = MOD_SAVE / "verify-results.jsonl"
        got = 0
        if results.exists():
            for line in results.read_text().splitlines():
                r = json.loads(line)
                d = run_dir(r["level"])
                (d / "verify.json").write_text(json.dumps(r))
                trace = MOD_SAVE / "verify" / r["level"]
                for f in ("real.txt", "sim.txt"):
                    if (trace / f).exists():
                        shutil.copy(trace / f, d / f)
                got += 1
        print(f"  {got}/{len(chunk)} results")
        if got < len(chunk):
            print("  some levels have no result (GD crashed or timed out?) - rerun verify to retry them")


def classify(pf, v):
    """(rank, category); lower rank = more interesting: real GD disagrees with the sim in a way that matters."""
    if not pf:
        return 9, "not pathfound"
    if pf.get("result") == "error":
        return 6, "pathfinder error"
    if not v:
        return 8, "not verified"
    if v.get("result") == "error":
        return 7, "verify error"
    real_death = v["frames"] - 1 if v["result"] == "died" else None
    if pf["result"] == "complete":
        if v["result"] == "died":
            return 0, "SIM BUG: completes in sim, dies in GD"
        if v["result"] != "complete":
            return 7, "verify timeout"
        return (3, "completes, drifts") if v["divergeFrame"] >= 0 else (5, "OK")
    # The pathfinder got stuck: its macro dies in the sim at v["simDeathFrame"]
    sim_death = v["simDeathFrame"]
    if real_death is None or real_death > sim_death + 2:
        return 1, "SIM BUG: sim dies, GD survives"
    if real_death < sim_death - 2:
        return 2, "GD dies before the sim"
    return 4, "pathfinder stuck (GD agrees)"


def cmd_report(a):
    index = {l["1"]: l for l in load_index()}
    rows = []
    for l in candidates(a):
        d = DATA / "runs" / l["1"]
        pf = json.loads((d / "pathfind.json").read_text()) if (d / "pathfind.json").exists() else None
        v = json.loads((d / "verify.json").read_text()) if (d / "verify.json").exists() else None
        rank, cat = classify(pf, v)
        rows.append((rank, cat, l, pf, v))
    rows.sort(key=lambda r: (r[0], int(r[2]["1"])))

    counts = {}
    for r in rows:
        counts[r[1]] = counts.get(r[1], 0) + 1
    lines = [f"# Batch report ({len(rows)} candidate levels of {len(index)} fetched)", ""]
    lines += [f"- {cat}: {n}" for cat, n in sorted(counts.items(), key=lambda kv: min(r[0] for r in rows if r[1] == kv[0]))]
    lines += ["", "| category | id | name | gv | sim | sim % | GD | GD frames | diverge frame | diverge x | max diff |",
              "|---|---|---|---|---|---|---|---|---|---|---|"]
    for rank, cat, l, pf, v in rows:
        if rank == 5 and not a.all:
            continue
        pf, v = pf or {}, v or {}
        pct = f"{100 * pf['x'] / max(pf['length'], 1):.1f}" if "x" in pf else ""
        div = v.get("divergeFrame", -1)
        cells = [cat, l["1"], l["2"], l.get("13", ""), pf.get("result", ""), pct, v.get("result", ""),
                 str(v.get("frames", "")), str(div) if div >= 0 else "",
                 f"{v['divergeReal'][0]:.0f}" if v.get("divergeReal") else "",
                 f"{v['maxDiff']:.3f}" if "maxDiff" in v else ""]
        lines.append("| " + " | ".join(cells) + " |")
    text = "\n".join(lines) + "\n"
    (DATA / "report.md").write_text(text, encoding="utf-8")
    print("\n".join(lines[:len(counts) + 2]))
    print(f"full table -> {DATA / 'report.md'}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--max-game-version", type=int, default=11)
    ap.add_argument("--ids", help="comma-separated level ids to limit a stage to")
    ap.add_argument("--redo", action="store_true", help="redo levels that already have results")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("fetch"); p.add_argument("--max-id", type=int, default=1619692)
    p.add_argument("--cached-only", action="store_true", help="index only the pages fetched so far"); p.set_defaults(f=cmd_fetch)
    p = sub.add_parser("download"); p.set_defaults(f=cmd_download)
    p = sub.add_parser("pathfind"); p.add_argument("--time-limit", type=float, default=120)
    p.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 2)); p.add_argument("--seed", type=int, default=1)
    p.set_defaults(f=cmd_pathfind)
    p = sub.add_parser("verify"); p.add_argument("--chunk", type=int, default=200); p.set_defaults(f=cmd_verify)
    p = sub.add_parser("report"); p.add_argument("--all", action="store_true", help="include OK levels"); p.set_defaults(f=cmd_report)
    a = ap.parse_args()
    a.f(a)


if __name__ == "__main__":
    main()
