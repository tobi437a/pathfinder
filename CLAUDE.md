# Pathfinder (fork) - working notes for Claude

Rules for this file:
1. Max 500 lines. If an edit pushes it over, delete or trim the least important content.
2. Not a changelog: describe how things are now, never what was changed or fixed when.

Geode mod for Geometry Dash 2.2081 that bots levels: `gd-sim/` is a standalone GD physics
simulator (reverse-engineered from GD), `src/pathfinder.cpp` searches it for inputs that beat a
level. `src/engine/` is a second backend (from the Neoexm pathfinder, `Neoexm-Pathfinder/` is the
untouched original) that searches by running GD's own engine in-process: supports everything, but
~4k steps/s. Upstream: camila314/pathfinder. Fork: `tobi437a/pathfinder`.

**The user's goal: everything simulated, so it's fast.** Port every GD feature the sim lacks (robot,
spider, swing, dash orbs, spider pads/orbs, teleport portals, dual, triggers, 2.2 block flags, ...)
into `gd-sim`, using the engine backend as an oracle (see "Porting with the engine oracle"). The
hybrid search only keeps levels working meanwhile; the engine's share should shrink to zero.

## Layout

- `src/engine/ui/Buttons.cpp` - the pathfinder button (`LevelInfoLayer`, `EditLevelLayer`,
  `LevelPage` for main levels). Setting `search-mode`: Hybrid (default) / Engine only open the level
  and run `gdpf::Bot`; Simulator only opens `src/main.cpp`'s old offline popup (`pathfind()` on a
  thread, exports `.gdr2`, no check in GD).
- `src/pathfinder.cpp` - the sim search `pathfindSim(SimSearch)`: random toggle-frame sets, keep
  whichever survives longest, roll back on failure. Takes a start state (`Level::resume`), target x,
  time and stall limits; returns button changes (`SimInput`, gdr2 frames). `pathfind()` wraps it.
- `src/engine/` - engine backend, namespace `gdpf`: `Bot` (session state machine, segments,
  verification, playback, hybrid loop), `Solver` (engine search per segment), `Engine` (step/reset,
  `step()` = one `pl->update(1/240)`), `Snapshot` (save/restore PlayerObject + objects + items),
  `Hooks.cpp` (takes over `GJBaseGameLayer::update`, swallows deaths, disables other mods' gameplay
  hooks while searching), `Autorun` (launch-arg test modes), `Experiments`, `Corridor`/`Layout`/
  `*Plan` (solver heuristics), `ui/ProgressPopup`. Found paths: `paths/<key>.json` in the save dir
  (reused when `use-cache`), `.partial.json` for resuming; macro `<name> - pathfinder.gdr2`.
- `src/engine/Hybrid.cpp` - sim<->engine bridge: `scanLevel` (x of every object the sim doesn't
  model: `Object::supported(id)`, gameplay triggers not in `kVisualTriggers`, passable/no-touch/2.2
  flags), `capture` (GD player 1 -> sim `Player`), `toEngine`/`toSim`, `saveMacro`.
- `src/debug.cpp` - debug tooling (see "In-game debug loop").
- `src/verify.cpp` - verifier: plays a macro in real GD at full speed against the sim (see "Verifier").
- `src/checker.cpp` - commented out. `src/subprocess.hpp` - vendored lib, don't edit.
- `gd-sim/include/Level.hpp` - entry point: `Level(levelString)`, `runFrame(pressed)`,
  `rollback(frame)`; every `Player` state is kept in `gameStates`. `Player.hpp` = full state.
- `gd-sim/src/Objects/Object.cpp` - object id -> type + hitbox table (`Object::create`).
- `gd-sim/src/Vehicle.cpp` - per-gamemode physics. `Objects/Slope.cpp`, `Block.cpp` - collisions.
- `gd-sim/test/test.cpp` -> `gd-sim-test.exe <level.lvl> <input.inp>`: prints `Frame N X Y Vel
  Accel Rot Coll` per frame (when `lvl.debug`), "Macro failed at frame N" on death (stderr).
- `tools/` - helper scripts written for this work (below). `build/` is gitignored scratch;
  `build/re/` holds tool output.

## Hybrid search (Bot, `SimPhase`)

The sim searches from the current boundary to 60 units before the next unmodelled object
(worker thread, `sim-round-seconds`, 10 s stall limit); the engine plays that path from the
boundary snapshot (`Checking`) cutting it into 240-tick segments; on a death it keeps segments up to
120 ticks before it and the Solver continues; after each engine segment the sim takes over again if
the path so far replayed in the sim lands exactly where GD is (else `capture` + a 12-step check).
4 rounds in a row gaining < 1 s turn the sim off for the session. Every final path is replayed from
a fresh start in GD before it counts. Log lines: `[hybrid]`. Engine tick T = gdr2 frame T + 1 = sim
state frame T + 1; an engine input at tick T is applied before step T.
`capture` (copying GD's state field by field) still drifts 0.06-0.1 in y within 2-4 steps (cube and
ship, cause unknown: acceleration left 0? GD's double position?) - fix this first, the oracle needs it.

## Porting with the engine oracle (the plan)

Goal per feature: GD and the sim agree step for step, then the sim handles it and `scanLevel`
stops flagging it. Build a harness on `src/engine` (e.g. an `Experiments.cpp` mode or a verify
job): restore/construct a GD state, `capture` it into the sim, step both with the same inputs and
report the first differing field; sweep inputs (hold lengths, speeds, sizes, gravity) to read
constants off GD (y, velocity per tick); confirm the logic in Ghidra (`PlayerObject::updateJump`,
`collidedWithObjectInternal`, ...); then fuzz random states until nothing differs. Order: fix
`capture` drift; the sim's ship path in Hexagon Force (main:16) dies in GD at x 4265 (spike 145,
speed 1.1), which hands the rest of that level to the slow engine (~100-700 steps/s there even
optimised) - find that sim bug; then robot, spider, swing, dash orbs, spider pad/orb, teleport, dual, triggers.
New vehicles need `VehicleType` + `Vehicle.cpp`, portal ids in `Object.cpp`, `Player::modeString`
letters (r p i), `capture` mapping, and the verifier's `realMode`.
GD's fly-mode floor/ceiling are `GJBaseGameLayer::getMinPortalY/getMaxPortalY` (they depend on camera
zoom); `capture` uses them minus 90.

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
python tools\gdtools.py gdr2 "<macro>.gdr2" -o build\re\x.inp           # header + sim input file
build\gd-sim\Debug\gd-sim-test.exe build\re\x.lvl build\re\x.inp > build\re\x.sim.txt
python tools\gdtools.py compare real.txt build\re\x.sim.txt   # first frame GD and sim disagree
python tools\gdtools.py objects build\re\x.lvl 11260 11360 -y 150 260   # objects near a divergence
python tools\gdtools.py frame 196 204       # object id -> sprite frame name, read from the GD exe
```
Level-string object coords are sim coords (`pos`); the printed sim/real `Y` is `pos.y - 15`.
GD's player y (float) is sim y + 90, so it rounds coarser; the sim moves y through `Player::moveY`.
`Entity::rotation` (objects and player) is in degrees.
Macros live in `%LOCALAPPDATA%\GeometryDash\geode\mods\eclipse.eclipse-menu\replays\`. The
simulator runs 240 tps; a non-240 macro won't line up. GD's `real.txt` counts from 0 at reset and
the sim's first line is frame 1; real frame N lines up with sim frame N+1, so use
`compare --shift 1`. After a death GD keeps recording the frozen player, so the real death frame is
where X stops changing. Official levels (Stereo Madness etc.) aren't in the save, so `level` can't
extract them.

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
the level must be in the GD save (main levels always are). A job `{"level": ..., "mode": "solve",
"searchMode": "Hybrid" | "Engine only", "timeLimit": 300}` runs the bot's search instead (resume,
cache and macro export off) and logs found/failed, seconds, ticks and `simTicks` (how much came
from the sim); solved macros go to `verify\solved\`. Hybrid solves main:1, 3, 5 in ~2 s. Untested yet:
16 Hexagon Force (dual), 19/20 (robot, spider), 21 (dash orbs), 22 Dash (swing). Each job plays the macro in a
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
  `Flip <frame> <on> ...` per mirror portal; once `Screen <w> <h> ... enter/exit <channel maps>`.

## Batch testing online levels

```powershell
cmake --build build --config Release --target pathfinder-cli   # standalone pathfinder (no Geode)
python tools\batch.py fetch [--max-id 1619692]   # rated levels with id <= max-id from the GD servers
python tools\batch.py download                   # level data for candidates
python tools\batch.py pathfind [--time-limit 120] [--jobs 14]
python tools\batch.py verify                     # every macro through tools\verify.ps1 (GD minimised)
python tools\batch.py report                     # build\re\batch\report.md, sim bugs first
```
- Candidates are levels whose server `gameVersion` (field 13 = GD version of the *last* upload, so
  updates bump it) is <= 11: 7 = early 1.x, 10/11 = 1.7, 18 = 1.8, 20 = 2.0, 22 = 2.2.
  Giant level (1619692), the last level rated in 1.7, has gameVersion 21 while its version field
  (5 / save `k16`) is still 1, so the version field doesn't reveal updates; gameVersion does.
- The search (`getGJLevels21`, `type=4 star=1`) is rated levels newest id first, 10 per page;
  `fetch` binary-searches the page where ids drop below `--max-id`. Requests are spaced 1 s apart.
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
```
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

Offsets are `PlayerObject` members in 2.2081: `0x9a0` = y velocity (double, gravity-relative),
`0x9bf` = upside down.
- Landing on a block (`PlayerObject::collidedWithObjectInternal`) needs y velocity <= 0; with
  positive velocity GD only handles moving blocks. It lands if the player's bottom is at most
  `clip + distance fallen this step` below the block top (clip 10; 6 for ship/ufo/wave/swing; 5 in
  platformer). Ship/ufo/ball heads hit a block's underside with the same rule (rise instead of
  fall, velocity >= 0, so 0 after an earlier landing or head hit in the same step still counts); a ufo head-hitting a breakable block breaks it instead. This also holds on the frame the player touches a
  gravity portal or a pad. The sim (`Block::collide`) still lands the player on the frame of an orb
  click (`padHitBefore`).
- `PlayerObject::flipGravity` multiplies y velocity by -0.5.
- Blue pad (`collisionCheckObjects`, type 10) sets upside down = `!GameObject::isFacingDown(pad)`,
  only if that changes it. isFacingDown: rotation a multiple of 90 -> (|rot| == 180) xor flipY;
  otherwise rot in 91..269 or -269..-91, xor flipY. So a sideways pad's target depends on flipY.
  The flipped player lands on a block on its new side in the same step if it touches it; one that
  stood on a block has already moved a step of its old gravity into that block, so it can miss.
- On a level's first step a ship on the floor is snapped there but keeps one step of falling
  velocity (-3.726, not grounded yet); it is grounded (velocity 0) from the second step on.
- The player's hitbox rect is cached and only recomputed after it is marked dirty (`setPosition`,
  `setScale`). `togglePlayerScale` sets the scale fields without dirtying it (it animates the
  sprite instead), so after a size portal the old size is still used for the rest of that step's
  portals and blocks until a block snap moves the player, e.g. standing on two blocks: the first
  snaps with the old size, the second with the new one. The sim models this with `resizePending`.
- Cube X-snap (`PlayerObject::checkSnapJumpToObject`, called on every cube step that lands or
  stands on a block): from the previous snap block to the new one, (dx, dy) = (A, +30), (B, -30) or
  (C, +60) within T snaps x to `new.x + offset` (clamped to +-T), offset = player x - block x stored
  at each call. The pattern uses the blocks' hitbox positions (`getRealPosition`), the offset and
  target their sprite positions (`getPosition`), which differ while a mirror portal flips the level.
  slow T1 A90 B120 C60; normal T1 A120 B150 C90; fast T2 A150 B195 C120; faster T2 A180 B225 C135;
  fastest T1 A120 B150 C90; mini makes A 90 (except slow/fastest). Only `resetObject` clears it.
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
- `PlayerObject::updateJump` rounds y velocity to 3 decimals (in 1/54 units) right after adding
  gravity, so a ball jump is rounded when gravity is applied on that step and not when the jump
  velocity is set without it (buffered jump, e.g. mini ball landing with the button held).
- `PlayerObject::hitGround` sets the on-ground flag (0x9c1; `flipGravity` clears it) only at
  gravity-relative y velocity <= 5 (270 in sim units); a ball jumps (flips) while the button is
  held and that flag is set, so a ball reaching the mode ceiling faster doesn't flip.
- Ship/UFO/ball hitting the ceiling (without flipping): velocity becomes 0, and the next step adds gravity *chosen for
  velocity 0* (the lighter one) before moving, so the player leaves the ceiling on that step.
- `GJBaseGameLayer::checkCollisions` snaps a normal-gravity cube below the floor onto the floor
  (no velocity check) *before* `collisionCheckObjects`, so a pad touched while landing on the floor
  still starts from floor height, and a block hitting the head after that can push the player
  below the floor (still grounded). The sim snaps in `Player::preCollision` (`floorSnapped`) and
  grounds in `postCollision`.
- A solid block's hitbox is its sprite's size (`GameObject::customSetup` only overrides it for
  some types), so ids sharing a sprite shape share a hitbox: `invis_`/`color` variants match the
  plain one (204 `invis_plank_01_small` = 196 `plank_01_small`, 15x8). Check with `gdtools frame`.

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
