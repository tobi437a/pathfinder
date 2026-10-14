#include <algorithm>
#include <Level.hpp>
#include <random>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <gdr/gdr.hpp>
#include "pathfinder.hpp"

class Replay2 : public gdr::Replay<Replay2, gdr::Input<"">> {
 public:
	Replay2() : Replay("Pathfinder", 1){}
};

struct Level2 : public Level {
	bool press = false;
	/// Per 100-unit section of x: how high a player can be there without having flown off the level. 300 above the
	/// highest object the sim models within 600 in x, and at least 1300.
	std::vector<float> ceilingY;
	/// Frames run by the search (PathfindStats::frames)
	uint64_t framesRun = 0;
	using Level::Level;

	Level2(std::string const& lvlString) : Level(lvlString) {
		std::vector<float> top(sections.size(), 0);
		for (size_t i = 0; i < sections.size(); ++i)
			for (auto& j : sections[i])
				top[i] = std::max(top[i], j->pos.y);
		ceilingY.assign(sections.size(), 1300);
		for (int i = 0; i < (int)sections.size(); ++i)
			for (int j = std::max(0, i - 6); j <= std::min((int)sections.size() - 1, i + 6); ++j)
				ceilingY[i] = std::max(ceilingY[i], top[j] + 300);
	}

	/// Whether a player at (x, y) flew off the level (above or below it)
	bool offLevel(float x, float y) const {
		if (y < 0)
			return true;
		if (ceilingY.empty())
			return y > 1300;
		int i = std::clamp((int)(0.01f * x), 0, (int)ceilingY.size() - 1);
		return y > ceilingY[i];
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

/// What sets two routes apart besides y: vehicle, gravity, size, speed and dual mode
uint16_t routeKey(Player const& p) {
	return (int)p.vehicle.type | p.upsideDown << 3 | p.small << 4 | p.dual << 5 | p.speed << 6;
}

/**
 * Paths of lvl (per state, index frame - 1): x, y and routeKey. Kept up to date from lvl by copying only the frames
 * past the lowest point lvl was rolled back to since the last update (copying the whole Level each time took most of
 * the search's time on long levels).
 */
struct Path {
	std::vector<bool> button;
	std::vector<float> x, y;
	std::vector<uint16_t> key;
	/// lvl.gameStates before this index are still the path's
	size_t unchanged = 0;

	int frames() const { return (int)y.size(); }
	void rolledBack(Level2 const& lvl) { unchanged = std::min(unchanged, lvl.gameStates.size()); }
	void take(Level2 const& lvl) {
		auto n = lvl.gameStates.size();
		button.resize(n);
		x.resize(n);
		y.resize(n);
		key.resize(n);
		for (size_t k = unchanged; k < n; ++k) {
			auto& st = lvl.gameStates[k];
			button[k] = st.button;
			x[k] = st.pos.x;
			y[k] = st.pos.y;
			key[k] = routeKey(st);
		}
		unchanged = n;
	}
	/// Whether a player is on this path's last `window` frames: where the path was at the player's x, it had the same
	/// routeKey and was within `dy` in y. Past the path's end isn't on it.
	bool has(float px, float py, uint16_t pkey, float dy, int window = 1 << 30) const {
		if (x.empty() || px > x.back() || px < x[std::max(0, frames() - window)])
			return false;
		size_t k = std::lower_bound(x.begin(), x.end(), px) - x.begin();
		return key[k] == pkey && std::abs(y[k] - py) <= dy;
	}
};

/**
 * Paths that ended in a dead end (often a secret path into a wall, a missed portal or the wrong gravity): the
 * furthest path since the last dead end, each time the search backs off from one. A candidate that ends on one of
 * them (same vehicle, gravity, size, speed and dual mode, and within two blocks in y, where that path was at the
 * candidate's x) counts as having got a quarter as far, so candidates that take another portal, gravity or lane win.
 * Only the stretch the search is backing off over counts (the last `window` frames of each): before that, every
 * candidate is on the dead path, and penalising all of them made the search stall in parts it had passed before.
 */
struct DeadPaths {
	std::vector<Path> paths;
	static constexpr size_t max = 8;
	static constexpr float dy = 60;
	int window = 3000;

	bool has(float px, float py, uint16_t pkey) const {
		for (auto& d : paths)
			if (d.has(px, py, pkey, dy, window))
				return true;
		return false;
	}
	bool has(Player const& p) const { return has(p.pos.x, p.pos.y, routeKey(p)); }
	/// Add a dead end (unless it ends on one already known)
	void add(Path const& path) {
		if (path.x.empty())
			return;
		for (auto& d : paths)
			if (d.has(path.x.back(), path.y.back(), path.key.back(), dy))
				return;
		if (paths.size() == max)
			paths.erase(paths.begin());
		paths.push_back(path);
	}
	/// Forget the dead ends the player is past
	void passed(float x) {
		std::erase_if(paths, [&](Path const& d) { return d.x.back() < x; });
	}
};

/**
 * The frames of a round before any toggle: every candidate starts by holding (or not) like before, until its first
 * toggle. They're simulated once per round, as far as a candidate needs, and copied into the others.
 */
struct Prefix {
	std::vector<Player> p1, p2; // states frame + 1, frame + 2, ... of the round starting at frame

	void reset() {
		p1.clear();
		p2.clear();
	}
};

int tryInputs(Level2& lvl, InputSet inputs, DeadPaths const& dead, Prefix& prefix) {
	auto frame = lvl.currentFrame();
	auto press_before = lvl.press;

	// Frames before the first toggle: copied from (or added to) the round's prefix
	int first = inputs.empty() ? 0 : 1 << 30;
	for (auto f : inputs.frames)
		first = std::min(first, (int)(uint16_t)(f - (uint16_t)frame));
	for (int k = 0; k < first && !lvl.gameStates.back().dead; ++k) {
		if (k < (int)prefix.p1.size()) {
			lvl.gameStates.push_back(prefix.p1[k]);
			lvl.gameStates2.push_back(prefix.p2[k]);
		} else {
			lvl.runFrame(lvl.press);
			++lvl.framesRun;
			prefix.p1.push_back(lvl.gameStates.back());
			prefix.p2.push_back(lvl.gameStates2.back());
		}
	}

	while (!inputs.empty() && !lvl.gameStates.back().dead) {
		if (inputs.contains(lvl.currentFrame())) {
			lvl.press = !lvl.press;
			inputs.erase(lvl.currentFrame());
		}

		lvl.runFrame(lvl.press);
		++lvl.framesRun;
	}
	int final = lvl.currentFrame();
	auto& last = lvl.latestState();

	// The caller rolls back (it keeps the frames of a candidate it takes)
	lvl.press = press_before;

	if (last.pos.x < lvl.length && lvl.offLevel(last.pos.x, last.pos.y))
		return 0;

	if (dead.has(last))
		return frame + (final - frame) / 4;
	return final;
}

std::vector<uint8_t> pathfind(std::string const& lvlString, std::atomic_bool& stop, std::function<void(double)> callback, uint32_t seed, PathfindStats* stats) {
	Level2 lvl(lvlString);

	std::mt19937 rng(seed ? seed : std::random_device()());
	std::uniform_int_distribution<int> dist(0, 999);

	int trueBest = 0;
	int fail = 1;
	// Exponential back-off from a dead end: how far back from trueBest the last one went (0: none since trueBest)
	int backoff = 0;
	DeadPaths dead;
	Prefix prefix;

	// The furthest path (exported), and the furthest since the last dead end (the next dead end's path)
	Path best, local;
	best.take(lvl);
	local.take(lvl);
	auto rolledBack = [&] {
		best.rolledBack(lvl);
		local.rolledBack(lvl);
	};
	// PF_LOG=1: trace the search's dead ends on stderr
	static bool const trace = std::getenv("PF_LOG") != nullptr;

	while (lvl.gameStates.back().pos.x < lvl.length) {
		auto frame = lvl.currentFrame();

		InputSet bestInputs;
		int bestFrame = frame;
		// The best candidate ended the round early and its frames are still in lvl
		bool kept = false;
		prefix.reset();

		constexpr int iterations = 300; //30
		for (int i = 0; i < iterations; i++) {

			InputSet inputs;
			for (int i = 0; i < 30; i++) {
				inputs.insert(frame + dist(rng));
			}

			int nf = tryInputs(lvl, inputs, dead, prefix);

			if (nf > bestFrame) {
				bestFrame = nf;
				bestInputs = inputs;
				if (bestFrame - frame > 500 && fail < 1000) {
					kept = true;
					break;
				}
			}
			lvl.rollback(frame);
		}

		if (stats)
			++stats->rounds;
		if (bestFrame == frame) {
			fail += fail > 100 ? 55 : 5;
			if (fail <= 1000) {
				// Back up a little (not before where the last back-off went, nor more than 1000 behind the best)
				int floor = backoff ? std::max(trueBest - backoff, 1) : trueBest - 1000;
				lvl.rollback(std::min(frame, std::max({frame - fail, floor, 1})));
				rolledBack();
			} else {
				// Backing up 1000 frames found no way on: a dead end. Go back twice as far as last time from the
				// furthest point, and steer away from the path that led there.
				if (trace) {
					std::fprintf(stderr, "dead end at frame %d x %.0f y %.0f key %x (best %d, backoff %d, %zu known)\n",
						local.frames(), local.x.back(), local.y.back(), local.key.back(), trueBest,
						backoff ? backoff * 2 : 2000, dead.paths.size());
				}
				dead.add(local);
				backoff = backoff ? backoff * 2 : 2000;
				dead.window = backoff + 1000;
				int to = trueBest - backoff;
				if (to <= 1) {
					to = 1;
					backoff = 0;
					dead.window = 3000;
				}
				lvl.rollback(std::min(frame, to));
				rolledBack();
				local.take(lvl);
				fail = 1;
				if (stats)
					++stats->deadEnds;
			}
		} else {
			// Take the first third of the best candidate. The one that ended the round is still simulated in lvl:
			// keep its frames instead of running them again.
			int end = frame;
			while (end < bestFrame - (bestFrame - frame) / 1.5)
				++end;
			if (kept)
				lvl.rollback(end);
			for (int i = frame; i < end; ++i) {
				if (bestInputs.contains(i)) {
					lvl.press = !lvl.press;
				}
				if (!kept) {
					lvl.runFrame(lvl.press);
					++lvl.framesRun;
				}
			}
		}

		if (lvl.currentFrame() > trueBest) {
			trueBest = lvl.currentFrame();
			fail = 0;
			backoff = 0;
			dead.window = 3000;
			dead.passed(lvl.latestState().pos.x);
		}
		if (lvl.currentFrame() > best.frames())
			best.take(lvl);
		if (lvl.currentFrame() > local.frames())
			local.take(lvl);

		if (callback)
			callback(std::min((lvl.latestState().pos.x / lvl.length) * 100, 100.0f));
		//std::cout << "\rStatus: " << std::min((lvl.latestFrame().pos.x / lvl.length) * 100, 100.0f) << "%            " << std::flush;

		if (stop)
			break;
	}

	// A path that reached the end is the one to export, even if a dead end that `best` kept took more frames
	if (isLevelEnd(lvl))
		best.take(lvl);
	if (stats)
		stats->frames = lvl.framesRun;

	// (State k is frame k + 1)
	Replay2 output;
	for (size_t k = 1; k < best.button.size(); ++k) {
		if (best.button[k] != best.button[k - 1])
			output.inputs.push_back(gdr::Input(k + 1, 1, false, best.button[k]));
	}

	return output.exportData().unwrapOr({});
}
