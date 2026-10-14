#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/// Work counters of one search (pathfinder-cli prints them)
struct PathfindStats {
	/// Sim frames run (runFrame calls), the search's cost independent of machine load
	uint64_t frames = 0;
	/// Batches of candidates tried
	uint64_t rounds = 0;
	/// Dead ends backed off from
	uint64_t deadEnds = 0;
};

/// seed 0 = random
std::vector<uint8_t> pathfind(std::string const& lvlString, std::atomic_bool& stop, std::function<void(double)> callback,
	uint32_t seed = 0, PathfindStats* stats = nullptr);
