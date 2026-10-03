#!/bin/sh
# Replays build/re/main_<n>.gdr2 in the Debug sim and compares with the verifier's GD recording
# of that macro (verify/main_<n>/real.txt in the mod save dir). Usage: tools/maincheck.sh 16 17 18
V="$LOCALAPPDATA/GeometryDash/geode/mods/camila314.pathfinder/verify"
for n in "$@"; do
  python tools/gdtools.py gdr2 "build/re/main_$n.gdr2" -o "build/re/main_$n.inp" >/dev/null
  build/gd-sim/Debug/gd-sim-test.exe "build/re/main_$n.lvl" "build/re/main_$n.inp" > "build/re/main_$n.sim.txt" 2>/dev/null
  echo "== main:$n"; python tools/gdtools.py compare "$V/main_$n/real.txt" "build/re/main_$n.sim.txt" --shift 1 | sed -n '2,4p'
done
