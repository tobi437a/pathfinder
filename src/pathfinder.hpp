#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/// seed 0 = random
std::vector<uint8_t> pathfind(std::string const& lvlString, std::atomic_bool& stop, std::function<void(double)> callback, uint32_t seed = 0);
