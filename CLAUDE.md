# Pathfinder (fork) - working notes for Claude

Rules for this file:
1. Max 500 lines. If an edit pushes it over, delete or trim the least important content.
2. Not a changelog: describe how things are now, never what was changed or fixed when.

Geode mod for Geometry Dash 2.2081 that bots levels: `gd-sim/` is a standalone GD physics
simulator (reverse-engineered from GD), `src/pathfinder.cpp` searches it for inputs that beat a
level and exports a `.gdr2` macro. The user's goal is fixing simulator/search bugs and adding
support for newer GD features (robot, spider, swing, dash orbs, teleport portals, dual, triggers,
...). Upstream: camila314/pathfinder. Fork: `tobi437a/pathfinder`.

## Layout

- `src/main.cpp` - UI: pathfinder button on `EditLevelLayer` (my levels) and `LevelInfoLayer`
  (online); runs `pathfind()` on a thread, saves the macro (into Eclipse's `replays` dir if loaded).
- `src/pathfinder.cpp` - the search: random toggle-frame sets, keep whichever survives longest,
  roll back on failure. Builds a `gdr::Replay` from the state history.
- `src/debug.cpp` - debug tooling (see "In-game debug loop").
- `src/verify.cpp` - verifier: plays a macro in real GD at full speed against the sim (see "Verifier").
- `src/checker.cpp` - commented out. `src/subprocess.hpp` - vendored lib, don't edit.
- `gd-sim/include/Level.hpp` - entry point: `Level(levelString)`, `runFrame(pressed)`,
  `rollback(frame)`; every `Player` state is kept in `gameStates`, player 2's (dual mode) in
  `gameStates2` (same length; a copy of player 1 outside dual). `Player.hpp` = full state.
  `runFrame` = `stepPlayer(p1)`, then `stepPlayer(p2)` if dual was on before p1's step; `isP2`
  picks the lane for `prevPlayer()`, `partner` points at the other player during a frame.
- `gd-sim/src/Objects/Object.cpp` - object id -> type + hitbox table (`Object::create`).
- `gd-sim/src/Vehicle.cpp` - per-gamemode physics. `Objects/Slope.cpp`, `Block.cpp` - collisions.
- `tools/offsets.py` - GD member offsets via the compiler; `tools/maincheck.sh 16 17` - replay
  `build/re/main_<n>.gdr2` in the Debug sim and compare with the verifier's recording of it.
- `gd-sim/test/test.cpp` -> `gd-sim-test.exe <level.lvl> <input.inp>`: prints `Frame N X Y Vel
  Accel Rot Coll` per frame (when `lvl.debug`), "Macro failed at frame N" on death (stderr).
- `tools/` - helper scripts written for this work (below). `build/` is gitignored scratch;
  `build/re/` holds tool output.

## New GD features

New vehicles need `VehicleType` + `Vehicle.cpp`, portal ids in `Object.cpp`, `Player::modeString`
letters (r p i), and the verifier's `realMode`. GD's fly-mode floor/ceiling are
`GJBaseGameLayer::getMinPortalY/getMaxPortalY` (they depend on camera zoom). The bottom is set by
`animateInDualGroundNew`: `floor((portalY - height/2) / 30) * 30`, at least 90, in GD y (sim y + 90).

## Current work: 1.8 support (start here)

The batch (`build\re\batch`) is 453 rated 1.8 levels (all of them with gameVersion 18).
One offline regression set: `runs\`, GD's `real.txt` per level (with `P2` lines in dual) from the
latest end-to-end batch (the newest pathfinder's macros); the user wants only this one set, no copies
of older batches. Check: `regress.py --p2 --rot --rotfly --tol 1e-6 --jobs 6 --base <name>` (`--jobs 6`:
the default can run out of memory). A sim change must not make any run worse; the goal is every run
matching GD bit for bit to the end (players 1 and 2, positions, cube/ball and ship/ufo/wave rotation,
deaths). All 453 runs match (`--base m4`): 1.8 simulation is done for this batch.
- Last end-to-end batch (`batch.py report`): 428/453 beaten in GD, 23 stuck (GD agrees); the 2 sim
  bugs it showed (1760244, 2079873) are fixed in the sim since (the report reads the in-game sim of
  the verify run).
- `batch.py pathfind` at the default 14 workers can run the machine low on memory; `--jobs 8` is safer.
- `tools\rotfly.py [--p2] [--regress NAME] [--runs DIR]`: per diverged run, GD's and the sim's mode and
  rotation at a rotation divergence, the turn target each implies and the step's travel angle.
- GD collision logs for a run's frames: `tools\gdcoll.py <id>:<from>-<to>`
  (launches GD minimised; adds `Coll`/`SlopeColl` lines and GD's flags to a fresh real.txt).
- Dual (`Objects/DualPortal.cpp`, `Player::updateGround`, `Player::flipPartner`, `startDual`,
  `copyAttributes`, `Level::runFrame`) follows `toggleDualMode`, `updateDualGround`,
  `playerWillSwitchMode`, `GJBaseGameLayer::flipGravity` and the per-step order of
  `GJBaseGameLayer::update` (comments in that code).
- Pathfinder (`batch.py pathfind`, 120 s): 427/453 complete in the sim, 26 stuck. Several stuck ones
  end in a secret path that runs into a wall, with the real route branching off far back; the search
  backs off exponentially from a dead end and penalises staying on it (`DeadPath`), which gets some
  but not all of them. Search improvements are for after 1.8 support.
- Two-player levels (`kA10`, 4 in the batch) need player-2 inputs: the pathfinder only presses
  player 1, and the sim gives player 2 no button there.
- `regress.py` labels each divergence p1, p2, p2rot, rot, rotfly, death-gd or death-sim.
- End-to-end: rebuild Release `pathfinder-cli`, `batch.py --redo pathfind`, `batch.py --redo
  verify` (GD minimised, ~25 min; a long background job can get killed under memory pressure -
  then `verify --import-only` and verify the rest with `--ids`), `batch.py report`.

## Build & install

```powershell
tools\build.ps1            # Debug: DEBUG_MODE=1, also builds build\gd-sim\Debug\gd-sim-test.exe
tools\build.ps1 RelWithDebInfo
```
It configures on first use, prints only our own warnings/errors, and the Geode CMake step
installs `build\camila314.pathfinder.geode` into GD's mods folder. **GD must be restarted** to load
a new build; whichever config was built last is what GD loads. Configure by hand:
`cmake -B build -G "Visual Studio 18 2026" -A x64 -T ClangCL`.

Gotchas:
- Built with the **ClangCL** toolset (VS "C++ Clang tools" component), since the CMakeLists use
  clang-style flags. Without the VS C++ workload CMake falls back to NMake and fails.
- `mod.json` `"geode"` must equal the installed SDK version (5.8.2) or configure fails.
- Writing `mod.json` with PowerShell 5.1 `Set-Content -Encoding utf8` adds a BOM, which breaks the
  Geode CLI ("execute_process failed"). Edit it with the Edit tool.
- `target_compile_definitions` inside a generator expression must not have a `-D` prefix (it
  isn't stripped there).
- PowerShell 5.1: native stderr (e.g. Ghidra's JVM warnings) becomes an error under
  `$ErrorActionPreference='Stop'`.
- Python `\b` inside a non-raw string silently becomes a backspace - watch Windows paths in scripts.

## Offline sim loop (no game needed)

```powershell
python tools\gdtools.py levels [filter]                 # levels in the GD save (local + saved online)
python tools\gdtools.py level "Slopeman Adventures" -o build\re\x.lvl
python tools\gdtools.py level main:16 -o build\re\x.lvl  # official level, from GD's Resources\levels
python tools\gdtools.py gdr2 "<macro>.gdr2" -o build\re\x.inp           # header + sim input file
build\gd-sim\Debug\gd-sim-test.exe build\re\x.lvl build\re\x.inp > build\re\x.sim.txt
python tools\gdtools.py compare real.txt build\re\x.sim.txt   # first frame GD and sim disagree
python tools\gdtools.py objects build\re\x.lvl 11260 11360 -y 150 260   # objects near a divergence
python tools\gdtools.py frame 196 204       # object id -> sprite frame name, read from the GD exe
```
Level-string object coords are sim coords (`pos`); the printed sim/real `Y` is `pos.y - 15`.
GD places objects 90 higher as floats (`float(y) + 90`), so the sim keeps each object's y at that
precision (`Object::Object`): a block at 240.8 is at 330.79998779 in GD, and a player landing under
it at GD's y, not 0.00002 off (which loses contact on the next step).
GD's player y (float) is sim y + 90, so it rounds coarser; the sim moves y through `Player::moveY`, which
moves like `PlayerObject::update`: `float(m_yVelocity * float(0.25f * 0.9f))` added to the float y. With
that the sim matches GD's y bit for bit (`regress.py --tol 1e-6`).
`Entity::rotation` (objects and player) is in degrees.
Macros live in `%LOCALAPPDATA%\GeometryDash\geode\mods\eclipse.eclipse-menu\replays\`. The
simulator runs 240 tps; a non-240 macro won't line up. GD's `real.txt` counts from 0 at reset and
the sim's first line is frame 1; real frame N lines up with sim frame N+1, so use
`compare --shift 1`. After a death GD keeps recording the frozen player, so the real death frame is
where X stops changing.

## In-game debug loop (Debug build)

- Gray debug button on `EditLevelLayer` (My Levels -> a level): pick a `.gdr2`; runs it through
  `build\gd-sim\Debug\gd-sim-test.exe` (path baked in from `__FILE__`) and writes `sim.txt` +
  `.lvl`/`.inp` into the mod save dir `%LOCALAPPDATA%\GeometryDash\geode\mods\camila314.pathfinder\`.
- Every `PlayLayer` frame is recorded (`GJBaseGameLayer::updateCamera` hook); opening any level in
  the editor dumps it to `real.txt` in the same save dir. `PlayLayer::resetLevel` clears the
  recording, so it only holds the latest attempt.
- Editor pause menu guideline button draws sim path (green), real path (red), inputs (dots).
- Logs: `tools\logs.ps1` (newest Geode log, filtered), `tools\logs.ps1 -All -Tail 200`,
  `tools\logs.ps1 -Crash` (newest crash log; `GeometryDash.exe+off` resolved through the bro file;
  Pathfinder frames are already symbolised via its PDB).

## Verifier (real GD vs sim, unattended)

```powershell
tools\verify.ps1 jobs.json [-Minimize]   # launches GD via Steam, runs the jobs, GD exits, prints summary
```
`jobs.json`: `{"jobs": [{"level": "77239587" | "main:15" | "<local name>", "macro": "<path>.gdr2"}]}`;
the level must be in the GD save (main levels always are; GD writes the save only on exit, so a level
just downloaded isn't there yet: give it a `"levelFile"`, see "Batch testing"). `pathfinder-cli`
beats main:17 and 18 (verified); main:16 Hexagon Force needs dual. Untested: 19/20 (robot,
spider), 21 (dash orbs), 22 Dash (swing). Each job plays the macro in a
`PlayLayer` at ~20-30k physics steps/s and compares the player with the in-process sim every step.
Results: `verify-results.jsonl` in the mod save dir (`complete`/`died`/`timeout`, first divergence
frame + positions, max diff) and `verify\<level>\real.txt`, `sim.txt`, `level.lvl`, which
`gdtools compare real.txt sim.txt --shift 1` reads. Frame numbers are real frames (0-based).
- Manual run: small play-button icon on `LevelInfoLayer`/`EditLevelLayer` -> pick a `.gdr2`.
- Runs skip `levelComplete` (no stars) and set `m_isTestMode` (meant to stop % progress saving,
  not checked in GD's code); job mode exits GD without saving anyway.
- `-Minimize` waits until the mod has taken `verify.json` before minimising: minimised during the
  loading screen, GD pauses before mods load and never finishes loading.
- Eclipse hacks that change physics (TPS bypass, noclip) or a playing macro would skew results.
- GD must run with High texture quality: sawblade radii depend on the loaded sprite sheet (the sim uses
  the `-uhd` sizes, see `Object.cpp`).
- real.txt also gets (sim coords; the tools skip lines not starting with `Frame`): per GD X-snap
  call `Snap <frame> obj <x> <y> prev <x> <y> offset <o> x <before> -> <after>`; per step
  `Cam <frame> <x> <y> vis <x> <y> flip <value> <dir>` and, while the last snap block's sprite is
  off its hitbox, `SnapObj <frame> <id> real <x> <y> sprite <x> <y> enter <r> <l> <channel> ...`;
  `Flip <frame> <on> ...` per mirror portal; once `Screen <w> <h> ... enter/exit <channel maps>`;
  `Acc <frame> <real> <sim>` whenever GD's `m_isAccelerating` changes; in dual mode
  `P2 <frame> X Y Vel Rot Mode` for player 2 (sim.txt / gd-sim-test too); `Death <frame> <player>
  obj <id> <x> <y> rot <r> type <t>` per `PlayLayer::destroyPlayer` (obj -1: no object - a slope or the
  mode bounds); per step `Flags <frame> og og2 rotating spin slope <on><was> boosted acc btn` (player 1's
  state, the format of `gd-sim-test`'s `Flags` lines with env `PF_FLAGS=1`, which also prints `SimDeath` (the object that killed); `PF_SLOPE=1` prints
  `SlopeDbg` lines per slope collision, `PF_TRACE=1` a `Trace` line (player y, velocity) after every
  object collision); per step and player `GD <frame> <player> cws .. ground <id> ring <m_stateRingJump> pot <x y>
  slope <x y> bounds <getMinPortalY getMaxPortalY>` (GD's potential and current slope; gd-sim-test prints `Pot` lines with PF_FLAGS=1).

## Batch testing online levels

```powershell
cmake --build build --config Release --target pathfinder-cli   # standalone pathfinder (no Geode)
python tools\batch.py fetch                      # 500 rated 1.8 levels from the GD servers -> index.json
python tools\batch.py download                   # level data for candidates
python tools\batch.py pathfind [--time-limit 120] [--jobs 14]
python tools\batch.py verify                     # every macro through tools\verify.ps1 (GD minimised)
python tools\batch.py report                     # build\re\batch\report.md, sim bugs first
```
- The batch is 1.8 levels (dual, slopes): `fetch` takes rated levels with ids `--min-id`..`--max-id`
  (1628620..2808696, first and last rated in 1.8), keeps those whose server `gameVersion` is in
  `--game-versions` (18), and writes `--count` (500) of them, evenly spread by id, to `index.json`.
  gameVersion (field 13) is the GD version of the *last* upload, so updates bump it: 7 = early 1.x,
  10/11 = 1.7, 18 = 1.8, 19 = 1.9, 20 = 2.0, 22 = 2.2. The version field (5 / save `k16`) doesn't
  reveal updates (Giant level, last rated in 1.7: gameVersion 21, version 1).
- The search (`getGJLevels21`, `type=4 star=1`) is rated levels newest id first, 10 per page;
  `fetch` binary-searches the pages where ids cross `--max-id` and `--min-id`. Requests are spaced
  2 s apart (1 s gets 429s).
- Stages cache per level under `build\re\batch\` and skip finished levels (`--redo`, `--ids 1,2`).
  `fetch` caches pages too; `fetch --cached-only` indexes just the pages fetched so far.
  `verify --import-only` collects results of a GD run whose batch.py was interrupted.
- Per level: `runs\<id>\pathfind.json`, `macro.gdr2`, `verify.json`, `real.txt`, `sim.txt`
  (`gdtools compare runs\<id>\real.txt runs\<id>\sim.txt --shift 1`); `levels\<id>.lvl` for
  `gdtools objects`. Re-check one level after a sim fix:
  `batch.py pathfind --ids <id> --redo` then `batch.py verify --ids <id> --redo`.
- `pathfinder-cli <lvl> <out.gdr2> --seed N --time-limit S` prints a JSON line; seed 1 by default.
- Verifier jobs take `"levelFile"` (raw `downloadGJLevel22` response): the mod builds the level
  with `GameLevelManager::responseToDict` + `GJGameLevel::create(dict, true)`, like a download.

Offline loop over the batch (no game; uses the Debug `gd-sim-test`, ~1.5 min for 570 levels):
```powershell
python tools\regress.py --save base          # first real/sim divergence per run -> regress-base.json
python tools\regress.py --base base          # after a sim change: which runs diverge later / earlier
python tools\regress.py --rot --save r0       # also a cube's/ball's rotation (mod 90) must match GD's
python tools\regress.py --p2 --save p0        # also player 2 in dual mode (P2 lines)
python tools\regress.py --rotfly --save f0    # also a ship's / ufo's / wave's rotation
python tools\triage.py                       # sim mode + objects at each divergence -> triage.txt
python tools\diverge.py <id> [--frame F]     # one run: real vs sim per frame, button, nearby objects
python tools\diverge.py <id> --p2 --flags   # player 2 instead; GD's and the sim's Flags lines per frame
python tools\gdcoll.py <id>:<from>-<to> ...  # replay in GD with its collision log (Coll/SlopeColl lines per
                                             # block / slope collision) -> build\re\batch\coll\<id>.real.txt
```
A run whose sim dies at another step than GD counts as diverged there. A sim fix should make runs better and none worse; real paths only cover what the old macros did,
so finish with `batch.py pathfind`/`verify`. Traces (verifier and `gd-sim-test`) end with
`Mode` = vehicle `c s b u w` (+ `r p i` robot/spider/swing in GD) + `u` upside down, `m` mini,
`g` on ground.

## Reading GD's code (Ghidra)

GD exe is imported and analysed in Ghidra project `C:\Users\tobi4\GD-2.2081` (Ghidra 12.1.2 at
`C:\Users\tobi4\Downloads\ghidra_12.1.2_PUBLIC`, JDK 25). Function names were applied from
`GeometryDash.bro` with `import-bro` (5132 named; no types/signatures, so members show as raw
offsets like `*(float *)(param_1 + 0x...)`). A backup from before the import is at
`C:\Users\tobi4\GD-2.2081-backup-before-import`. Never re-import/re-analyse unless GD updates.

```powershell
tools\ghidra\ghidra.ps1 decomp out.c PlayerObject::updateJump 0x38f810 asm:0x388d10
tools\ghidra\ghidra.ps1 xrefs out.txt PlayerObject::flipGravity 0x14012345   # callers / refs
tools\ghidra\ghidra.ps1 find out.txt "PlayerObject::.*slope"                 # regex over names
python tools\gdtools.py sym 0x38f9a0 "GJBaseGameLayer::collision"            # instant, no Ghidra
python tools\offsets.py PlayerObject --all > build\re\PlayerObject.offsets.txt  # member offsets
python tools\gdtools.py annotate build\re\out.c param_1=PlayerObject param_3=GameObject -o named.c
```
- `offsets.py` gets member offsets from the compiler (failing `static_assert(offsetof)` probes built
  with the mod's recorded clang-cl command line; needs one Debug build). `--match slope` filters;
  only members the class itself declares in the .bro (query base classes separately).
- `annotate` renames `param_1 + 0x9a0`, `param_1[0x134]` (scaled by the declared pointer type) and
  `(longlong)param_1 + 0x9bf` to member names, per function, from those tables.
- Output goes to `build\re\<out>`; read it with Read/Grep (decomps are long - grep offsets).
- ~10-15 s per invocation (JVM + project open), so **batch several targets per call**.
- Fails with "locked" if the user has the project open in the Ghidra GUI - ask them to close it.
- `find` regexes must not contain `(` `)` (`analyzeHeadless.bat` chokes on them); use `[Ss]nap`.
- Read float/double constants (`DAT_14...`) from the exe: `python tools\gdtools.py const 140622c24 140623000:d`.
- Targets: `Class::method` (all overloads), RVA `0x38b900`, or absolute `0x14038b900`.
- Image base `0x140000000`. `GeometryDash.bro` (fetched by CMake) is at
  `build\_deps\bindings-src\bindings\2.2081\GeometryDash.bro`; it also lists member order.
- When GD updates: re-import the new exe into a new project, run
  `tools\ghidra\ghidra.ps1 import-bro bro.log` (after bumping `mod.json` `gd` + rebuilding so the
  new bindings exist), and point `$env:GD_GHIDRA_PROJECT` / the default in `ghidra.ps1` at it.
  For struct types too, the user can run the bindings' `SyncBromaScript` from the Ghidra GUI
  (Script Manager, add `build\_deps\bindings-src\scripts\ghidra`).

## Working rules

- Fix one simulator behaviour at a time; for each, read the GD code (Ghidra), make the change, and
  check it against a real-GD recording (`compare`). Document verified GD behaviour in code comments next to the sim code, not here.
- The pathfinder RNG is unseeded (`std::random_device`), so single runs are not comparable.
- Before finishing, if the sim changed, rebuild Debug (`tools\build.ps1`) so the user can test the
  fix in the game straight away (after restarting GD).
- Every time you fix something, commit as the very last step (one commit per fix, after the
  Debug rebuild). Don't push unless asked.

## Machine

Windows 11. Geode SDK 5.8.2 `C:\Users\tobi4\Documents\Geode` (`GEODE_SDK`, user-level env),
Geode CLI 3.8.0, profile `Steam`. GD: `C:\Program Files (x86)\Steam\steamapps\common\Geometry Dash`
(loader 5.10.1, mods also: Eclipse, node-ids, BetterInfo). GD save:
`%LOCALAPPDATA%\GeometryDash`. VS Community 2026 + ClangCL, CMake 4.4, Python 3.11.
