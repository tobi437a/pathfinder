#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <Player.hpp>

/// A button change in sim frame numbers (= gdr2 macro frames; GD's engine handles it before step frame - 1)
struct SimInput {
	int frame;
	bool down;
};

struct SimSearch {
	std::string levelString;
	/// Start from this state (Level::resume) instead of the level start
	std::optional<Player> start;
	/// Stop once the player reaches this x (or the level end, whichever comes first)
	float targetX = INFINITY;
	/// Seconds; 0 = no limit
	double timeLimit = 0;
	/// Give up after this many seconds without getting further; 0 = no limit
	double stallLimit = 0;
	/// 0 = random
	uint32_t seed = 0;
};

struct SimResult {
	/// Button changes after the start frame
	std::vector<SimInput> inputs;
	bool startButton = false;
	/// The path covers frames startFrame + 1 ..= endFrame
	int startFrame = 1;
	int endFrame = 1;
	float endX = 0;
	bool reachedTarget = false;
	bool reachedEnd = false;
};

/// The search itself. `progress` gets the percentage of the level reached.
SimResult pathfindSim(SimSearch const& search, std::atomic_bool& stop, std::function<void(double)> progress);

/// Searches the whole level and returns the best path as a .gdr2 macro. seed 0 = random
std::vector<uint8_t> pathfind(std::string const& lvlString, std::atomic_bool& stop, std::function<void(double)> callback, uint32_t seed = 0);

/// A .gdr2 macro from button changes (player 1, 240 tps)
std::vector<uint8_t> exportMacro(std::vector<SimInput> const& inputs);
