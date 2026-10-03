#!/usr/bin/env python3
"""Member offsets of GD classes in 2.2081, read off the Geode bindings by the compiler.

  python tools/offsets.py PlayerObject m_isOnSlope m_yVelocity     specific members
  python tools/offsets.py PlayerObject --match slope              every member whose name matches
  python tools/offsets.py PlayerObject --all                      every member the .bro declares

Ghidra shows members as raw offsets (`*(double *)(param_1 + 0x9a0)`); this maps names <-> offsets.
It compiles a probe with one failing `static_assert(offsetof(Class, m) == -1)` per member, using the
clang-cl command line MSBuild recorded for the mod (needs one Debug build of the mod first), and
reads each offset from clang's "expression evaluates to 'N == -1'". Members are taken from the
class's own block in GeometryDash.bro; inherited members (e.g. GameObject's in PlayerObject) need
`--class GameObject`-style queries of the base class instead.
"""
import argparse
import glob
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BRO = REPO / "build/_deps/bindings-src/bindings/2.2081/GeometryDash.bro"
TLOG = REPO / "build/pathfinder.dir/Debug/pathfinder.tlog/clang-cl.command.1.tlog"


def bro_members(cls):
    """Member names declared in `class <cls> ... { ... }` in the .bro (fields, not methods)."""
    text = BRO.read_text(encoding="utf-8")
    m = re.search(r"^class " + re.escape(cls) + r"\b[^{]*\{", text, re.M)
    if not m:
        raise SystemExit(f"class {cls} not in {BRO}")
    depth, i = 1, m.end()
    while depth:
        depth += {"{": 1, "}": -1}.get(text[i], 0)
        i += 1
    body = text[m.end():i - 1]
    names = []
    for line in body.splitlines():
        line = line.split("//")[0].strip()
        f = re.match(r"^[\w:<>,\s\*&]+?[\s\*&](m_\w+|unk\w*)\s*(\[[^\]]*\])?\s*;$", line)
        if f and "(" not in line:
            names.append(f[1])
    return names


def compile_command():
    if not TLOG.exists():
        raise SystemExit(f"{TLOG} missing - build the mod once (tools\\build.ps1)")
    lines = TLOG.read_bytes().decode("utf-16-le", "replace").splitlines()
    for i, line in enumerate(lines):
        if line.upper().endswith("\\SRC\\MAIN.CPP"):
            cmd = lines[i + 1]
            break
    else:
        raise SystemExit("no compile command for src/main.cpp in the tlog")
    # Drop the precompiled header, output and source arguments; keep includes/defines/flags.
    cmd = re.sub(r'/Y[uc]"[^"]*"|/Fp"[^"]*"|/Fo"[^"]*"|/FI\S+', "", cmd)
    cmd = re.sub(r"\S+MAIN\.CPP\s*$", "", cmd, flags=re.I)
    return cmd.replace("/c ", "/Zs ", 1)


def vs_file(pattern, what):
    hits = glob.glob("C:\\Program Files\\Microsoft Visual Studio\\*\\*\\" + pattern)
    if not hits:
        raise SystemExit(f"{what} not found")
    return hits[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cls")
    ap.add_argument("members", nargs="*")
    ap.add_argument("--match", help="regex over member names (case-insensitive)")
    ap.add_argument("--all", action="store_true")
    a = ap.parse_args()
    members = list(a.members)
    if a.match or a.all:
        members += [m for m in bro_members(a.cls) if a.all or re.search(a.match, m, re.I)]
    if not members:
        raise SystemExit("no members given")

    probe = REPO / "build/re/offsets_probe.cpp"
    probe.parent.mkdir(parents=True, exist_ok=True)
    src = ["#include <Geode/Geode.hpp>", "#include <cstddef>", "using namespace geode::prelude;"]
    src += [f"static_assert(offsetof({a.cls}, {m}) == -1, \"@{m}\");" for m in members]
    probe.write_text("\n".join(src) + "\n")
    # vcvars64 provides the MSVC/SDK include paths the recorded command line relies on.
    vcvars = vs_file(r"VC\Auxiliary\Build\vcvars64.bat", "vcvars64.bat")
    clang = vs_file(r"VC\Tools\Llvm\x64\bin\clang-cl.exe", "clang-cl.exe (VS 'C++ Clang tools')")
    bat = probe.with_suffix(".bat")
    bat.write_text(f'@call "{vcvars}" >nul\n"{clang}" {compile_command()} -Wno-invalid-offsetof '
                   f'-Wno-unknown-argument -Wno-unused-command-line-argument -ferror-limit=0 "{probe}"\n')
    r = subprocess.run(["cmd", "/c", str(bat)], capture_output=True, text=True)
    out = r.stdout + r.stderr
    found = {}
    # clang: "...: error: static assertion failed due to requirement '...': @m_name"
    #        "...: note: expression evaluates to '2464 == -1'"
    for name, val in re.findall(r"@(\w+)\W.*?\n(?:.*\n)*?.*?expression evaluates to '(\d+) == -1'", out):
        found.setdefault(name, int(val))
    for m in members:
        if m in found:
            print(f"0x{found[m]:04x}  {m}")
        else:
            print(f"   ?    {m}")
    if len(found) < len(members):
        errs = [l for l in out.splitlines() if "error" in l and "static assertion" not in l][:10]
        print("\n".join(errs), file=sys.stderr)


if __name__ == "__main__":
    main()
