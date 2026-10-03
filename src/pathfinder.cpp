#include <algorithm>
#include <chrono>
#include <Level.hpp>
#include <random>
#include <gdr/gdr.hpp>
#include "pathfinder.hpp"

class Replay2 : public gdr::Replay<Replay2, gdr::Input<"">> {
 public:
	Replay2() : Replay("Pathfinder", 1){}
};

struct Level2 : public Level {
	bool press = false;
	float highestY = 0;
	using Level::Level;

	Level2(std::string const& lvlString) : Level(lvlString) {
		// Find highest y
		for (auto& i : sections) {
			for (auto& j : i) {
				highestY = std::max(highestY, j->pos.y);
			}
		}
	}
};

/**
 * The toggle frames of one attempt. A tiny unordered set: with ~30 entries a linear scan beats
 * std::set's per-node allocations. Keys are uint16_t, so lookups wrap frame numbers like std::set did.
 */
struct InputSet {
	std::vector<uint16_t> frames;

	InputSet() { frames.reserve(32); }
	bool contains(uint16_t f) const { return std::find(frames.begin(), frames.end(), f) != frames.end(); }
	void insert(uint16_t f) { if (!contains(f)) frames.push_back(f); }
	void erase(uint16_t f) {
		if (auto it = std::find(frames.begin(), frames.end(), f); it != frames.end()) {
			*it = frames.back();
			frames.pop_back();
		}
	}
	bool empty() const { return frames.empty(); }
};

int tryInputs(Level2& lvl, InputSet inputs) {
	auto frame = lvl.currentFrame();
	auto press_before = lvl.press;

	while (!inputs.empty() && !lvl.gameStates.back().dead) {
		if (inputs.contains(lvl.currentFrame())) {
			lvl.press = !lvl.press;
			inputs.erase(lvl.currentFrame());
		}

		lvl.runFrame(lvl.press);
	}
	int final = lvl.currentFrame();
	float lastX = lvl.latestState().pos.x;
	float lastY = lvl.latestState().pos.y;

	lvl.rollback(frame);
	lvl.press = press_before;

	if (lastX < lvl.length && (lastY > 1300 || lastY < 0))
		return 0;

	return final;
}

SimResult pathfindSim(SimSearch const& search, std::atomic_bool& stop, std::function<void(double)> callback) {
	using Clock = std::chrono::steady_clock;
	auto started = Clock::now();
	auto lastGain = started;
	auto seconds = [](Clock::time_point since) { return std::chrono::duration<double>(Clock::now() - since).count(); };

	Level2 lvl(search.levelString);
	if (search.start) {
		lvl.resume(*search.start);
		lvl.press = search.start->button;
	}
	float goal = std::min(lvl.length, search.targetX);
	// No rolling back to before the start
	int first = lvl.baseFrame + 1;

	std::mt19937 rng(search.seed ? search.seed : std::random_device()());
	std::uniform_int_distribution<int> dist(0, 999);

	int trueBest = first;
	int fail = 1;
	int numAway = 1000;

	Level2 lvlBest = lvl;

	while (lvl.gameStates.back().pos.x < goal) {
		auto frame = lvl.currentFrame();

		InputSet bestInputs;
		int bestFrame = frame;

		constexpr int iterations = 300; //30
		for (int i = 0; i < iterations; i++) {

			InputSet inputs;
			for (int i = 0; i < 30; i++) {
				inputs.insert(frame + dist(rng));
			}

			int nf = tryInputs(lvl, inputs);

			if (nf > bestFrame) {
				bestFrame = nf;
				bestInputs = inputs;
				if (bestFrame - frame > 500 && fail < 1000)
					break;
			}
		}

		if (bestFrame == frame) {
			lvl.rollback(std::max(std::max(frame - fail, trueBest - numAway), first));

			fail += 5;

			if (fail > numAway + 1000) {
				numAway += 1000;
				fail = 1;

				if (numAway > 10000) {
					numAway = 1000;
					trueBest = first;
					lvl.rollback(first);
				}
			} else if (fail > 100) {
				fail += 50;
			}
		} else {
			for (int i = frame; i < bestFrame - (bestFrame - frame) / 1.5; ++i) {
				if (bestInputs.contains(i)) {
					lvl.press = !lvl.press;
				}
				lvl.runFrame(lvl.press);
			}
		}

		if (lvl.currentFrame() > trueBest) {
			trueBest = lvl.currentFrame();
			fail = 0;
			numAway = 1000;
		}
		if (lvl.currentFrame() > lvlBest.currentFrame()) {
			lvlBest = lvl;
			lastGain = Clock::now();
		}

		if (callback)
			callback(std::min((lvl.latestState().pos.x / lvl.length) * 100, 100.0f));
		//std::cout << "\rStatus: " << std::min((lvl.latestFrame().pos.x / lvl.length) * 100, 100.0f) << "%            " << std::flush;

		if (stop)
			break;
		if (search.timeLimit > 0 && seconds(started) > search.timeLimit)
			break;
		if (search.stallLimit > 0 && seconds(lastGain) > search.stallLimit)
			break;
	}

	// lvlBest's states still point at lvl (Player::level), so compare them with each other, not prevPlayer()
	SimResult result;
	auto& states = lvlBest.gameStates;
	result.startButton = states.front().button;
	result.startFrame = states.front().frame;
	for (size_t i = 1; i < states.size(); ++i) {
		if (states[i].button != states[i - 1].button)
			result.inputs.push_back({states[i].frame, states[i].button});
	}
	result.endFrame = states.back().frame;
	result.endX = states.back().pos.x;
	result.reachedEnd = result.endX >= lvlBest.length;
	result.reachedTarget = result.endX >= goal;
	return result;
}

std::vector<uint8_t> exportMacro(std::vector<SimInput> const& inputs) {
	Replay2 output;
	for (auto& i : inputs)
		output.inputs.push_back(gdr::Input(i.frame, 1, false, i.down));
	return output.exportData().unwrapOr({});
}

std::vector<uint8_t> pathfind(std::string const& lvlString, std::atomic_bool& stop, std::function<void(double)> callback, uint32_t seed) {
	SimSearch search;
	search.levelString = lvlString;
	search.seed = seed;
	return exportMacro(pathfindSim(search, stop, callback).inputs);
}
