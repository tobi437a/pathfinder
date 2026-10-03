#pragma once

// The bridge between the simulator (gd-sim) and GD's engine, used by the hybrid search in Bot:
// the sim searches wherever it models the level, the engine checks its paths and solves the rest.

#include <Geode/Geode.hpp>
#include <Level.hpp>
#include <cmath>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../pathfinder.hpp"
#include "Session.hpp"

namespace gdpf::hybrid {

/// Where the sim stops modelling the level: x of every object it leaves out that can change gameplay
struct Horizon {
    std::vector<float> xs;
    /// False if the sim can't be used anywhere (platformer, ...)
    bool usable = true;
    std::string reason;

    /// The first such x at or after x (INFINITY if none)
    float next(float x) const;
};

/// Read the level's objects (call while it's at its start, before triggers move anything)
Horizon scanLevel(PlayLayer* pl);

/// Whether the level's start state is one the sim models (start mode, dual, ...)
bool simStartSupported(PlayLayer* pl, std::string& why);

/**
 * Player 1 of GD's engine as a sim state at engine tick `tick` (= sim frame tick + 1), holding `button`
 * (the button state the engine applies before step `tick`). Nothing if the sim can't represent it.
 * `lvl` must be built from the same level string as the search (used orbs/pads/portals are looked up
 * there by position).
 */
std::optional<Player> capture(PlayLayer* pl, Level& lvl, int tick, bool button, std::string& why);

/// Engine inputs (player 1 jump button) for sim button changes
InputList toEngine(std::vector<SimInput> const& inputs);
/// Sim button changes (gdr2 frames) for engine inputs
std::vector<SimInput> toSim(InputList const& inputs);

/// Save engine inputs as a .gdr2 macro for Eclipse (its replays folder if loaded, else the mod's
/// save dir). Returns the path written.
geode::Result<std::filesystem::path> saveMacro(InputList const& inputs, std::string const& levelName);

}
