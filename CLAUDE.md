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

The batch (`build
eatch`) is 453 rated 1.8 levels (all of them with gameVersion 18).
`runs-pre1.8\` is a snapshot of the runs recorded with the pre-dual pathfinder's macros (GD's
`real.txt` for each; the 173 that reach a dual portal with GD's player 2 too, `P2` lines): the
offline regression set, `regress.py --runs runs-pre1.8 [--p2] --base cur12|p2s`. On it 339/453
runs match GD to the end for player 1 (`regress-cur12.json`), 325 for both players
(`regress-p2s.json`).
- Dual (`Objects/DualPortal.cpp`, `Player::updateGround`, `Player::flipPartner`, `startDual`,
  `copyAttributes`, `Level::runFrame`) follows `toggleDualMode`, `updateDualGround`,
  `playerWillSwitchMode`, `GJBaseGameLayer::flipGravity` and the per-step order of
  `GJBaseGameLayer::update` (see "Verified GD behaviour").
- Pathfinder (`batch.py pathfind`, 120 s): 427/453 complete in the sim, 26 stuck. Several stuck ones
  end in a secret path that runs into a wall, with the real route branching off far back; the search
  backs off exponentially from a dead end and penalises staying on it (`DeadPath`), which gets some
  but not all of them. Search improvements are for after 1.8 support.
- Two-player levels (`kA10`, 4 in the batch) need player-2 inputs: the pathfinder only presses
  player 1, and the sim gives player 2 no button there.
- Open leads (`regress.py --runs runs-pre1.8`, then `diverge.py`/`gdtools compare --p2`): 2376766
  frame 1041 (4 copies of one level): an upside-down cube easing to 270 after a launch starts its
  falling spin in the sim, not in GD (some `updateRotation`/`updateJump` condition); the rest are
  one-offs.
- Known unexplained: a pad then a slope in the same step (2284379 frame 778) - GD keeps the pad's
  spin, so its slope collision skips `hitGround` there; the sim doesn't.
- `regress.py --rot` scores cube/ball rotation (for rotated objects); look for remaining rotation
  bugs with it too.
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
- real.txt also gets (sim coords; the tools skip lines not starting with `Frame`): per GD X-snap
  call `Snap <frame> obj <x> <y> prev <x> <y> offset <o> x <before> -> <after>`; per step
  `Cam <frame> <x> <y> vis <x> <y> flip <value> <dir>` and, while the last snap block's sprite is
  off its hitbox, `SnapObj <frame> <id> real <x> <y> sprite <x> <y> enter <r> <l> <channel> ...`;
  `Flip <frame> <on> ...` per mirror portal; once `Screen <w> <h> ... enter/exit <channel maps>`;
  `Acc <frame> <real> <sim>` whenever GD's `m_isAccelerating` changes; in dual mode
  `P2 <frame> X Y Vel Rot Mode` for player 2 (sim.txt / gd-sim-test too).

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
python tools\triage.py                       # sim mode + objects at each divergence -> triage.txt
python tools\diverge.py <id> [--frame F]     # one run: real vs sim per frame, button, nearby objects
```
A sim fix should make runs better and none worse; real paths only cover what the old macros did,
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

## Verified GD behaviour

Offsets are `PlayerObject` members in 2.2081: `0x9a0` = y velocity (double, positive = up in the
world: `updateJump` subtracts sign * gravity; the sim's `velocity` is gravity-relative and 54x),
`0x9bf` = upside down.
- Slopes (type 25) are ported 1:1 in `Objects/Slope.cpp` (`preSlopeCollision`,
  `collidedWithSlopeInternal`), `Player::leaveSlope` (`postCollision`), `Player::slopeJump`
  (`updateJump`) and `Block::collideBlock` (`collidedWithObjectInternal`'s slope parts); comments
  there explain them. GD collides slopes in object order with pads/orbs/portals, before blocks, with a
  broadphase rect at twice their size. A flying player holding on an uphill slope gets y velocity >= 2
  and isn't landed, so it rides the surface and keeps its speed off the top; leaving a slope launches
  at `min(0.8 / angle, 1.1) * rise * 1.4` (x0.75 fly/ball, ramped 0.4 -> 1 over the first 0.1 s) if
  that's faster; leaving one off its bottom falls at its speed. Both are set after the step's move, so
  a player that landed on a block in that step takes no walk-off step. Pads clear the slope flags;
  `flipGravity` flips which way is downhill.
- `m_speedMultiplier` (`updateTimeMod`) is 5.980002 / 5.7700018882751465 / 5.870002 / 6.000002 /
  6.000002 for m_playerSpeed 0.7 / 0.9 / 1.1 / 1.3 / 1.6, so x per step = speed * mult * 60 / 240
  (the sim's `player_speeds`). `m_gravity` 0.940199 / 0.958199 / 0.957199 / 0.961199 / 0.961199.
- Landing on a block (`PlayerObject::collidedWithObjectInternal`) needs y velocity <= 0; with
  positive velocity GD only handles moving blocks. It lands if the player's bottom is at most
  `clip + distance fallen this step` below the block top (clip 10; 6 for ship/ufo/wave/swing; 5 in
  platformer). Ship/ufo/ball heads hit a block's underside with the same rule (rise instead of
  fall, velocity >= 0, so 0 after an earlier landing or head hit in the same step still counts, as does a ship's
  velocity reaching exactly 0 in the air); a ufo head-hitting a breakable block breaks it instead. This also holds on the frame the player touches a
  gravity portal or a pad. The sim (`Block::collide`) still lands the player on the frame of an orb
  click (`padHitBefore`), GD's click being a step later; but an orb first touched with the button
  already held fires in GD's collisions, before the block, so that player doesn't land. Likewise a
  blue orb: a fresh click lands the player on its old side first (X-snap included); a held one flips it before the
  blocks, which it can then land on in the new gravity in the same step.
- `PlayerObject::flipGravity` multiplies y velocity by -0.5.
- A yellow pad launches a ufo at 864 like a cube; the ufo's velocity cap (432, mini 508.248) cuts it on
  the next step, after that step's gravity, so the first step up moves 432 / 240.
- Orbs: `playerTouchedRing` appends each touched ring to `m_touchingRings` (0xa38); `resetTouchedRings`
  at the start of every step drops those not touched in the last step. A click (`pushButton`) tries
  them in that order (first touched first, custom rings type 36 before the rest), so with two orbs
  touched the older one wins, whatever the object order. A ring touched in the step a size portal
  was touched counts with the old (cached) hitbox size. A ball walking off a block as it clicks a
  blue orb still takes the walk-off step (a step of gravity) first; a ball jumping from a slope doesn't
  (it flips on the slope, `pushButton` -> `updateJump`).
- Blue pad (`collisionCheckObjects`, type 10) sets upside down = `!GameObject::isFacingDown(pad)`,
  only if that changes it, and `propellPlayer(0.8)` then `flipGravity`: -345.6 sim units at any
  speed (x0.8 mini, x0.6 ball). isFacingDown: rotation a multiple of 90 -> (|rot| == 180) xor flipY;
  otherwise rot in 91..269 or -269..-91, xor flipY. So a sideways pad's target depends on flipY.
  The flipped player lands on a block on its new side in the same step if it touches it; one that
  stood on a block has already moved a step of its old gravity into that block, so it can miss.
  Any pad launches a player standing on a block from that step into the block (no snap back),
  unless a slope earlier in the step's collisions put it back on its surface (launched from there).
- `m_isAccelerating` (0x952): set by a slope launch (`boostPlayer`) and a black (drop, type 32) or
  red ring, cleared by pads (not red) and, at the start of `updateJump`, for a ship/ufo/wave/swing
  whose y velocity is within [0, 8) up or (-6.4, 0] down (/ 0.85 mini). While set, the ship and ufo
  velocity caps (-6.4 / 8) don't apply and the ship's gravity factor changes: falling and not held
  -1 (its fall slows by 4.644 per step), rising and held 0.8 (it slows down). So a cube that left a
  slope earlier and enters a ship falling faster than the cap isn't clamped to it.
- Each `toggleFlyMode` / `toggleBirdMode` / `toggleDartMode` (ship, ufo, wave; on or off) halves y
  velocity; ball and cube have none. Per portal: a ship and a ufo portal touched in one step by a
  cube halve it three times (ship on, ship off, ufo on). A wave's `m_yVelocity` is +-round3(m_playerSpeed *
  m_speedMultiplier) (not doubled when mini), so leaving it starts from that (`VehiclePortal`).
- On a level's first step a ship on the floor is snapped there but keeps one step of falling
  velocity (-3.726, not grounded yet); it is grounded (velocity 0) from the second step on.
- The player's hitbox rect is cached and only recomputed after it is marked dirty (`setPosition`,
  `setScale`). `togglePlayerScale` sets the scale fields without dirtying it (it animates the
  sprite instead), so after a size portal the old size is still used for the rest of that step's
  portals and blocks until a block snap moves the player, e.g. standing on two blocks: the first
  snaps with the old size, the second with the new one. The sim models this with `resizePending`.
  A mini ship/ufo that grows just under the mode ceiling is only pushed under it on the next step.
  Pads and portals are all checked against the rect from before them, so a vehicle portal's size
  change waits too (leaving the wave keeps its small hitbox: a mini portal next to a ship portal is
  reached a step later). Switching into or out of ship/ufo/wave runs `updatePlayerScale`, which
  dirties the rect, so the blocks then see the new size (a cube entering ship + mini portals off a
  block doesn't land back on it). A walk-off step's gravity is halved by the switch like any velocity.
- Cube X-snap (`PlayerObject::checkSnapJumpToObject`, called on every cube step that lands or
  stands on a block): from the previous snap block to the new one, (dx, dy) = (A, +30), (B, -30) or
  (C, +60) within T snaps x to `new.x + offset` (clamped to +-T), offset = player x - block x stored
  at each call. The pattern uses the blocks' hitbox positions (`getRealPosition`), the offset and
  target their sprite positions (`getPosition`), which differ while a mirror portal flips the level.
  slow T1 A90 B120 C60; normal T1 A120 B150 C90; fast T2 A150 B195 C120; faster T2 A180 B225 C135;
  fastest T1 A120 B150 C90; mini makes A 90 (except slow/fastest). Only `resetObject` clears it.
  It only snaps if the previous snap object is a plain solid (`getType() == 0`): not from a
  breakable block (type 21), though onto one it does. The sim prints `Snap` lines like the verifier's.
- The level ends where `PlayLayer::checkForEnd` finds player 1's x at the end portal's spawn point:
  `createObjectsFromSetupFinished` puts the portal at max(screen right + 300, last object x + 340),
  the spawn point is 300 before it, so last object x (any object) + 40 (`Level::length`).
- Camera x (`m_gameState.m_cameraPosition`) = player x - (winWidth / 2 - 75) every step until the
  level end; winSize is 569x320 here. `PlayLayer::updateVisibility` (once per step, before the
  step's collisions, with the previous step's camera) sets block sprites: enter effects, and
  mid-flip `screenFlipObject`: sprite x = x + (569 - 2 (x - camera x)) * flip. Hitboxes don't move.
- Mirror portal (45/46, touched with a 44-wide hitbox) -> `toggleFlipped`: `m_levelFlipping`
  tweens to 1/0 over 0.4 s, ease in-out rate 1.2, value at the end of touch step + k =
  ease((k + 1) / 96); from 0/1 it starts at 1e-5/0.99999. After a completed flip to 1,
  `screenFlipObject` uses 1 - flip (PlayLayer 0x3204 = -1) until a flip back to 0 completes.
- Enter effect triggers set a per-channel type for objects entering from the right (`enter`) or
  leaving at the left (`exit`): 22 -> -2 (fade only, the default), 23 -> -6, 24 -> -5, 25 -> -7,
  26 -> -8, 27 -> -3, 28 -> -4, 55 -> -11, 56 -> -10, 57 -> -9, 58 -> -12, 59 -> -13. They only move
  sprites within 70 of the screen edges, so they don't reach a block the player stands on.
- Cube and ball fall: `updateJump` subtracts `float(0.225f * g * mult)` from m_yVelocity, g = `(float)m_gravity`
  for the cube (mult 1) and the fixed 0.958199 for the ball (mult 0.6), unrounded; then rounds (below).
  Ship/ufo use the fixed 0.958199 too and divide by 0.85 when mini; ufo mult 0.4 (0.6 above the
  velocity threshold). The sim computes all of these with `gdFall` (Vehicle.cpp).
- `PlayerObject::updateJump` rounds y velocity to 3 decimals (in 1/54 units) right after adding
  gravity, so a ball jump is rounded when gravity is applied on that step and not when the jump
  velocity is set without it (buffered jump, e.g. mini ball landing with the button held).
- `PlayerObject::hitGround` sets the on-ground flag (0x9c1; `flipGravity` clears it) only at
  gravity-relative y velocity <= 5 (270 in sim units); a ball jumps (flips) while the button is
  held and that flag is set, so a ball reaching the mode ceiling faster doesn't flip.
- A player whose inner hitbox hits a block within 0.1 s (`m_lastFlipTime`) of a gravity flip, with the
  block on its head side, isn't killed (and doesn't break a breakable block): `collidedWithObjectInternal`
  puts it on the block's far side (upside down: on its top) and calls `hitGround`.
- An upside-down cube below the ground (y 0) dies in `checkCollisions` before object collisions,
  unless it flipped gravity within 0.1 s: then it is put back on it with velocity 0, and grounded for
  a step (no gravity) if it was that slow.
- Ship/UFO/ball hitting the ceiling (without flipping): velocity becomes 0, and the next step adds gravity *chosen for
  velocity 0* (the lighter one) before moving, so the player leaves the ceiling on that step. A ufo
  jumping on that next step moves up into the ceiling instead and stays there one step longer.
- `GJBaseGameLayer::checkCollisions` snaps a normal-gravity cube below the floor onto the floor
  (no velocity check) *before* `collisionCheckObjects`, so a pad touched while landing on the floor
  still starts from floor height, and a block hitting the head after that can push the player
  below the floor (still grounded). A pad, orb or portal whose bottom edge is exactly at the head of a
  player on the floor counts as touched (GD's rects include edges); one standing on a block has moved
  a step into the block by then, so edge contact there doesn't count (`Object::touching`). The sim snaps in `Player::preCollision` (`floorSnapped`) and
  grounds in `postCollision`.
- An object rotated by a non-multiple of 90 (pad, orb, portal, ...) is touched only if its bounding
  rect (of the rotated box) overlaps the player's unrotated rect, edges included, and then its
  oriented box overlaps the player's (rotated) one (`collisionCheckObjects`, `OBB2D::overlaps1Way`).
- So the cube's and ball's rotation matters (the sim keeps it in GD's clockwise degrees; ship, ufo and
  wave rotations and objects' are counter-clockwise). `GJBaseGameLayer::update` calls
  `PlayerObject::updateRotation(dt)` after the step's collisions (dt 0.25 at 240 tps): the spin
  `m_rotationSpeed` (degrees/s) is added, times dt / 60, except that a cube on a slope and a cube
  on the ground (`m_isOnGround2`: set by landing, cleared by jumps, pads, rings, slope launches,
  fly-mode portals and falling past -4, so also rising after a gravity flip) slerp to the nearest slope
  angle / multiple of 90 (rate `m_playerSpeed * 0.175 * dt`, x3 on the ground; a slope collision
  does it once more, `updateSlopeRotation`). `runNormalRotation`: +-180 / 0.4333 (mini 0.3333),
  minus when upside down, from a jump, a pad, a ring, starting to fall (velocity past -0.25) and
  leaving a downhill slope; a slope launch (`boostPlayer`) spins -+180 / 0.8667 (mini 0.6667).
  Landing (`hitGround`) stops it. Ball: rolls at +-120 / (0.2 * f) on the ground (mini x1/0.8; f per
  speed 1.2406 1 0.8042 0.6658 0.5409) and spins -+340 / (0.8 * f) after a flip. A slope collision
  of a new angle restarts the roll at 1 / cos(angle) times that (above 2: 1 + 0.7 * the excess). A fresh orb click's
  new spin starts a step later (GD handles the click at its next step). Ship/ufo/wave portals (on
  or off) and leaving the ball set the rotation to 0; leaving the ship/ufo/wave for the cube spins it
  (`resetPlayerIcon`), for the ball doesn't (`toggleRollMode` stops it).
- Dual: `toggleDualMode` copies player 1 to player 2 (`copyAttributes`: position, mode, size, speed,
  y velocity, `m_jumpBuffered`; not the rotation or spin), flips player 2's gravity and gives it the
  opposite y velocity; a solo portal taken by player 2 copies player 2 onto player 1 and releases all
  of player 2's buttons. GD steps player 2 (update, collisions) right after player 1 (not once player 1
  died in that step), from the step after dual starts, so its first `updateJump` adds gravity before
  it moves. It copies player 1 as the portal finds it: one standing on a block (not a slope) has
  fallen a step of gravity into it by then (the block puts player 1 back later). In a level that starts
  in dual (`kA8`) player 2 is stepped from the first step, off the ground. `handleButton` presses and
  releases player 2 only while dual is on, but `m_jumpBuffered` is the held flag (`pushButton` sets
  it, `releaseButton` or an orb clears it), so a button held into a dual portal is held by both.
  `toggleDualMode` also sets `m_unkA99` (0xa99) on both players to the dual flag, which negates
  gravity in `playerIsFallingBugged` for a non-upside-down player: in dual, "falling" (the ship's
  and ufo's gravity factors, the end of `m_maybeIsBoosted`) is y velocity < -2g for both
  gravities, outside dual < 2g (upside down > 2g). Ground heights
  (`getGroundHeightForMode`): ship/ufo/wave/swing 300, ball 240, else 270 (= no ground outside dual);
  in dual max(own (ball as 270), other's), centred on the dual portal. In dual a player taking a
  vehicle portal of the mode the other player is in gets the other's opposite gravity
  (`playerWillSwitchMode`), and `checkCollisions` bounds every mode like a flying one: a cube past
  its ceiling dies unless gravity/mode changed within 0.2 s, which puts it back stopped (y velocity 0,
  both ground flags cleared). A speed portal (either player's) sets a pending speed that
  `GJBaseGameLayer::update` gives player 1 and, in dual, player 2 (`updateTimeMod`) at the start of the
  next step. Gravity portals, blue pads and blue/green orbs flip through
  `GJBaseGameLayer::flipGravity`: in dual, with both players in the same mode (it compares ship, ufo,
  ball, robot, spider, swing, not the wave), the other player takes the opposite gravity
  (`PlayerObject::flipGravity`: world y velocity x0.5); a fresh orb click does it at the start of
  GD's next step. Not in two-player mode (`kA10`, settings 0x154), where each player also gets only
  its own inputs (`handleButton`), so a player-1 macro never presses player 2.
- A solid block's hitbox is its sprite's size (`GameObject::customSetup` only overrides it for
  some types), so ids sharing a sprite shape share a hitbox: `invis_`/`color` variants match the
  plain one (204 `invis_plank_01_small` = 196 `plank_01_small`, 15x8). Check with `gdtools frame`.
  Portals and pads get theirs from `GameObject::setupSpriteSize`: a switch through a byte table at
  RVA 0x1a3fec indexed by id - 1 (read it from the exe). Vehicle 34x86, gravity 25x75, dual 41x91,
  mirror 44x92, size 31x90, speed 35x44 / 33x56 / 51x56 / 65x56, pads 25x4 / 25x5 / 25x6.

- `GJBaseGameLayer::update(dt)` runs `max(1, round(dt * 240))` physics steps regardless of real
  time (`getModifiedDelta`), so `update(1/240)` in a loop = one step per call. `processCommands`
  and `updateCamera` run once per step inside that loop, so `real.txt` is per step at any fps.
- `GJBaseGameLayer::handleButton` acts immediately (`PlayerObject::pushButton`/`releaseButton`);
  only real clicks go through the timestamped `m_queuedButtons`, which can split a step in two.
- GD moves the player on the step it handles a press; the sim one step later. A gdr input at
  frame F is pressed before real step F-1 (Eclipse playback) and sim step F-1.
- Losing focus pauses a `PlayLayer` (`AppDelegate::applicationWillResignActive`); minimising calls
  `applicationDidEnterBackground` -> `CCDirector::pause()`, after which `drawScene` stops updating
  the scheduler. `CCApplication::run` itself never sleeps. The verifier hooks both away.

## Working rules

- Fix one simulator behaviour at a time; for each, read the GD code (Ghidra), make the change, and
  check it against a real-GD recording (`compare`). Add GD facts to this file only once verified.
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
