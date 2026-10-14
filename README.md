# Pathfinder

A Geode mod for Geometry Dash that beats levels automatically. A physics simulator of GD
(`gd-sim/`) runs the level thousands of times faster than the game, the pathfinder searches it for
inputs that reach the end, and the result is exported as a macro. Pathfinder does not play
macros itself: you need a bot that can (e.g. Eclipse).

## Credits

Forked from [camila314/pathfinder](https://github.com/camila314/pathfinder). Pathfinder and its
simulator were originally created by camila314.

Buy iCreate Pro to support camila: https://icreate.pro/

## How to use

1. Open the level you want to beat.
2. Click the blue Pathfinder button to start the pathfinder.
3. Export the macro.
4. Import the macro in your bot and play it back.

## What is supported

Supported:
- Cube, ship, ball, UFO and wave
- Dual mode, gravity, size, mirror and speed portals
- Slopes, including upside-down ones
- Yellow, pink, red, blue, green and black orbs; yellow, pink, red and blue pads
- Spikes, sawblades and breakable blocks
- Start positions

Not supported yet:
- Robot, spider and swing
- Dash orbs, teleport portals, spider orbs/pads
- Triggers that affect gameplay (move, toggle, rotate, ...)
- Anything from 2.2
- Two-player levels
- Some objects added after 1.8 may be simulated wrongly

## Building

Needs the [Geode SDK](https://geode-sdk.org). A normal build (`cmake -B build` then
`cmake --build build`) produces just the mod.
