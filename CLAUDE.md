# Pathfinder (fork) - working notes for Claude

Rules for this file:
1. Max 500 lines. If an edit pushes it over, delete or trim the least important content.
2. Not a changelog: describe how things are now, never what was changed or fixed when.

Geode mod for Geometry Dash 2.2081 that bots levels: `gd-sim/` is a standalone GD physics
simulator (reverse-engineered from GD), `src/pathfinder.cpp` searches it for inputs that beat a
level and exports a `.gdr2` macro. The user's goal is fixing simulator/search bugs and adding
support for newer GD features. Upstream: camila314/pathfinder. Fork: `tobi437a/pathfinder`.

## Layout

- `src/main.cpp` - UI: pathfinder button on `EditLevelLayer` (my levels) and `LevelInfoLayer`
  (online); runs `pathfind()` on a thread, saves the macro (into Eclipse's `replays` dir if loaded).
- `src/pathfinder.cpp` - the search: random toggle-frame sets, keep whichever survives longest,
  roll back on failure. Builds a `gdr::Replay` from the state history.
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
the level must be in the GD save (main levels always are). Each job plays the macro in a
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
  positive velocity GD only handles moving blocks. This also holds on the frame the player touches a
  gravity portal or a pad. The sim (`Block::collide`) still lands the player on the frame of an orb
  click (`padHitBefore`).
- `PlayerObject::flipGravity` multiplies y velocity by -0.5.
- `GJBaseGameLayer::checkCollisions` snaps a normal-gravity cube below the floor onto the floor
  (no velocity check) *before* `collisionCheckObjects`, so a pad touched while landing on the floor
  still starts from floor height. The sim does this in `Player::postCollision` (`hitPad`).
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
