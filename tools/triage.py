#!/usr/bin/env python3
"""Group batch divergences by the objects around them, to find sim bugs shared by many levels.

  python tools/triage.py [--regress NAME] [--ids 1,2] [--dx 45] [--dy 60] [--top 40] [--resim]

For every batch run whose real GD path diverges from the sim (runs/<id>/verify.json), lists the
objects of levels/<id>.lvl within --dx/--dy of the player at the first divergence (real frame,
`pos.y = Y + 15`), marks ids the sim ignores (not in gd-sim/src/Objects/Object.cpp) with `*`, and
prints how often each id shows up near a divergence. Per-level lines go to build/re/batch/triage.txt.
--resim reruns each level through gd-sim-test (Debug build) into runs/<id>/simdbg.txt, whose `Mode`
column (vehicle c s b u w, + u upside down, m mini, g grounded) gives the sim's mode at the
divergence; without it the existing simdbg.txt files are used. --regress NAME takes the divergences
from `regress.py --save NAME` (the current sim, simdbg.txt) instead of the verifier's verify.json.
"""
import argparse
import collections
import json
import re
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parent.parent
DATA = REPO / "build" / "re" / "batch"
SIM_TEST = REPO / "build" / "gd-sim" / "Debug" / "gd-sim-test.exe"
sys.path.insert(0, str(Path(__file__).resolve().parent))
from gdtools import read_gdr2, read_path, to_inp  # noqa: E402


def sim_ids():
    """Object ids that Object::create turns into a sim object."""
    src = (REPO / "gd-sim" / "src" / "Objects" / "Object.cpp").read_text(encoding="utf-8")
    ids = set()
    for block in re.findall(r"objs\(\((.*?)\),\s*\w+,", src, re.S):
        block = re.sub(r"//.*", "", block)
        for a, b in re.findall(r"\{\s*(\d+)\s*,\s*(\d+)\s*\}", block):
            ids.update(range(int(a), int(b) + 1))
        for n in re.findall(r"\d+", re.sub(r"\{\s*\d+\s*,\s*\d+\s*\}", "", block)):
            ids.add(int(n))
    return ids


def level_objects(path):
    out = []
    for o in path.read_text(encoding="utf-8").split(";")[1:]:
        kv = o.split(",")
        d = dict(zip(kv[::2], kv[1::2]))
        if "1" in d and "2" in d:
            try:
                out.append((int(d["1"]), float(d["2"]), float(d.get("3", 0)), d))
            except ValueError:
                pass
    return out


def resim(run, lvl):
    """Debug sim trace of a run's macro -> runs/<id>/simdbg.txt (inputs padded so it runs to the end)."""
    _, inputs = read_gdr2(run / "macro.gdr2")
    inp = run / "simdbg.inp"
    inp.write_text(to_inp(inputs) + "0" * 40000)
    with open(run / "simdbg.txt", "w") as out:
        subprocess.run([str(SIM_TEST), str(lvl), str(inp)], stdout=out, stderr=subprocess.DEVNULL)


def sim_modes(path):
    """{sim frame: mode} from a simdbg.txt."""
    modes = {}
    if path.exists():
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("Frame "):
                parts = line.split()
                if "Mode" in parts:
                    modes[int(parts[1])] = parts[parts.index("Mode") + 1]
    return modes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ids")
    ap.add_argument("--dx", type=float, default=45)
    ap.add_argument("--dy", type=float, default=60)
    ap.add_argument("--top", type=int, default=40)
    ap.add_argument("--resim", action="store_true")
    ap.add_argument("--regress")
    a = ap.parse_args()
    known = sim_ids()
    near_count, unknown_count, mode_count = collections.Counter(), collections.Counter(), collections.Counter()
    lines = []
    want = set(a.ids.split(",")) if a.ids else None
    regress = json.loads((DATA / f"regress-{a.regress}.json").read_text(encoding="utf-8")) if a.regress else None
    for vpath in sorted((DATA / "runs").glob("*/verify.json")):
        lid = vpath.parent.name
        if want and lid not in want:
            continue
        v = json.loads(vpath.read_text(encoding="utf-8"))
        if regress is not None:
            if regress.get(lid, {}).get("diverge", -1) < 0:
                continue
            f = regress[lid]["diverge"]
            real, sim = read_path(vpath.parent / "real.txt"), read_path(vpath.parent / "simdbg.txt")
            v = dict(v, divergeFrame=f, divergeReal=real[f][:2], divergeSim=sim[f + 1][:2])
        if v.get("divergeFrame", -1) < 0 or not v.get("divergeReal"):
            continue
        lvl = DATA / "levels" / f"{lid}.lvl"
        if not lvl.exists():
            continue
        if a.resim:
            resim(vpath.parent, lvl)
        f = v["divergeFrame"]
        modes = sim_modes(vpath.parent / "simdbg.txt")
        mode = f"{modes.get(f, '?')}<{modes.get(f + 1, '?')}"  # sim frame f+1 = real frame f
        mode_count[mode.split("<")[1].rstrip("g")] += 1
        (rx, ry), (sx, sy) = v["divergeReal"], v["divergeSim"]
        py = ry + 15
        near = sorted((abs(x - rx), oid, x, y) for oid, x, y, _ in level_objects(lvl)
                      if abs(x - rx) <= a.dx and abs(y - py) <= a.dy)
        ids = sorted({oid for _, oid, _, _ in near})
        for i in ids:
            near_count[i] += 1
            if i not in known:
                unknown_count[i] += 1
        kind = "x" if abs(rx - sx) > abs(ry - sy) else "y"
        tag = " ".join(f"{i}{'*' if i not in known else ''}" for i in ids)
        lines.append(f"{lid:>8} {v['result']:>8} f{v['divergeFrame']:>6} x{rx:8.1f} y{ry:6.1f} "
                     f"d{kind} {rx - sx:+7.2f} {ry - sy:+7.2f} {mode:>9} | {tag}")
    (DATA / "triage.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"{len(lines)} diverging levels -> {DATA / 'triage.txt'}")
    print("sim mode at divergence: " + "  ".join(f"{m}:{n}" for m, n in mode_count.most_common()))
    print("ids near divergences (count, * = ignored by the sim):")
    print("  " + "  ".join(f"{i}{'*' if i not in known else ''}:{n}" for i, n in near_count.most_common(a.top)))
    print("ignored ids near divergences:")
    print("  " + "  ".join(f"{i}:{n}" for i, n in unknown_count.most_common(a.top)))


if __name__ == "__main__":
    main()
