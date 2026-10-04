#include <algorithm>
#include <Level.hpp>
#include <random>
#include <cmath>
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

bool isLevelEnd(Level2& lvl) {
	return lvl.latestState().pos.x >= lvl.length;
}

/**
 * A path that ended in a dead end (often a secret path that leads into a wall): the player's y on every frame of it.
 * After backing off from one, the search treats candidates that never leave it as having got a quarter as far.
 */
struct DeadPath {
	std::vector<float> y; // index: frame - 1
	bool active() const { return !y.empty(); }
	bool leaves(int frame, float py) const {
		return frame - 1 >= (int)y.size() || std::abs(py - y[frame - 1]) > 30;
	}
};

int tryInputs(Level2& lvl, InputSet inputs, DeadPath const& dead) {
	auto frame = lvl.currentFrame();
	auto press_before = lvl.press;
	bool left = !dead.active();

	while (!inputs.empty() && !lvl.gameStates.back().dead) {
		if (inputs.contains(lvl.currentFrame())) {
			lvl.press = !lvl.press;
			inputs.erase(lvl.currentFrame());
		}

		lvl.runFrame(lvl.press);
		if (!left)
			left = dead.leaves(lvl.currentFrame(), lvl.latestState().pos.y);
	}
	int final = lvl.currentFrame();
	float lastX = lvl.latestState().pos.x;
	float lastY = lvl.latestState().pos.y;

	lvl.rollback(frame);
	lvl.press = press_before;

	if (lastX < lvl.length && (lastY > 1300 || lastY < 0))
		return 0;

	if (!left)
		return frame + (final - frame) / 4;
	return final;
}

std::vector<uint8_t> pathfind(std::string const& lvlString, std::atomic_bool& stop, std::function<void(double)> callback, uint32_t seed) {
	Level2 lvl(lvlString);

	std::mt19937 rng(seed ? seed : std::random_device()());
	std::uniform_int_distribution<int> dist(0, 999);

	int trueBest = 0;
	int fail = 1;
	// Exponential back-off from a dead end: how far back from trueBest the last one went (0: none since trueBest)
	int backoff = 0;
	DeadPath dead;

	Level2 lvlBest = lvl;

	while (lvl.gameStates.back().pos.x < lvl.length) {
		auto frame = lvl.currentFrame();

		InputSet bestInputs;
		int bestFrame = frame;

		constexpr int iterations = 300; //30
		for (int i = 0; i < iterations; i++) {

			InputSet inputs;
			for (int i = 0; i < 30; i++) {
				inputs.insert(frame + dist(rng));
			}

			int nf = tryInputs(lvl, inputs, dead);

			if (nf > bestFrame) {
				bestFrame = nf;
				bestInputs = inputs;
				if (bestFrame - frame > 500 && fail < 1000)
					break;
			}
		}

		if (bestFrame == frame) {
			fail += fail > 100 ? 55 : 5;
			if (fail <= 1000) {
				// Back up a little (not before where the last back-off went, nor more than 1000 behind the best)
				int floor = backoff ? std::max(trueBest - backoff, 1) : trueBest - 1000;
				lvl.rollback(std::min(frame, std::max({frame - fail, floor, 1})));
			} else {
				// Backing up 1000 frames found no way on: a dead end. Go back twice as far as last time from the
				// furthest point, and steer away from the path that led there.
				dead.y.clear();
				for (auto& st : lvlBest.gameStates)
					dead.y.push_back(st.pos.y);
				backoff = backoff ? backoff * 2 : 2000;
				int to = trueBest - backoff;
				if (to <= 1) {
					to = 1;
					backoff = 0;
				}
				lvl.rollback(std::min(frame, to));
				fail = 1;
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
			backoff = 0;
			// Past the dead end: forget it
			if (trueBest > (int)dead.y.size())
				dead.y.clear();
		}
		if (lvl.currentFrame() > lvlBest.currentFrame()) {
			lvlBest = lvl;
		}

		if (callback)
			callback(std::min((lvl.latestState().pos.x / lvl.length) * 100, 100.0f));
		//std::cout << "\rStatus: " << std::min((lvl.latestFrame().pos.x / lvl.length) * 100, 100.0f) << "%            " << std::flush;

		if (stop)
			break;
	}

	// A path that reached the end is the one to export, even if a dead end that lvlBest kept took more frames
	if (isLevelEnd(lvl))
		lvlBest = lvl;

	// (Compared with the previous state in lvlBest itself: its states' prevPlayer() would read lvl's, which may
	// have been rolled back onto another path since)
	Replay2 output;
	for (size_t k = 1; k < lvlBest.gameStates.size(); ++k) {
		auto& i = lvlBest.gameStates[k];
		if (i.frame > 1 && i.button != lvlBest.gameStates[k - 1].button)
			output.inputs.push_back(gdr::Input(i.frame, 1, false, i.button));
	}

	return output.exportData().unwrapOr({});
}
