/**
 * pathfinder-cli <level.lvl> <out.gdr2> [--seed N] [--time-limit S]
 *
 * Runs the pathfinder outside GD (for tools/batch.py). Writes the best macro found and prints a
 * one-line JSON summary: whether the macro beats the level in the sim, where it dies otherwise.
 */
#include <Level.hpp>
#include <gdr/gdr.hpp>
#include "pathfinder.hpp"
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <thread>

class Replay2 : public gdr::Replay<Replay2, gdr::Input<"">> {
 public:
	Replay2() : Replay("Pathfinder", 1) {}
};

int main(int argc, char** argv) {
	if (argc < 3) {
		std::cerr << "usage: pathfinder-cli <level.lvl> <out.gdr2> [--seed N] [--time-limit S]" << std::endl;
		return 2;
	}

	uint32_t seed = 1;
	double timeLimit = 0;
	for (int i = 3; i + 1 < argc; i += 2) {
		std::string arg = argv[i];
		if (arg == "--seed")
			seed = std::stoul(argv[i + 1]);
		else if (arg == "--time-limit")
			timeLimit = std::stod(argv[i + 1]);
	}

	std::ifstream in(argv[1], std::ios::binary);
	std::string lvlString((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

	std::atomic_bool stop = false;
	std::mutex m;
	std::condition_variable cv;
	bool done = false;
	std::thread timer([&] {
		if (timeLimit <= 0)
			return;
		std::unique_lock lock(m);
		if (!cv.wait_for(lock, std::chrono::duration<double>(timeLimit), [&] { return done; }))
			stop = true;
	});

	auto start = std::chrono::steady_clock::now();
	auto macro = pathfind(lvlString, stop, nullptr, seed);
	double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	{
		std::lock_guard lock(m);
		done = true;
	}
	cv.notify_all();
	timer.join();

	std::ofstream(argv[2], std::ios::binary).write(reinterpret_cast<char const*>(macro.data()), macro.size());

	// Replay the macro in the sim the same way the verifier does (input at frame F from step F - 1)
	auto replay = Replay2::importData(macro);
	std::vector<std::pair<uint64_t, bool>> inputs;
	if (replay.isOk()) {
		for (auto& i : replay.unwrap().inputs)
			inputs.emplace_back(i.frame, i.down);
	}

	Level lvl(lvlString);
	bool held = false;
	size_t next = 0;
	while (!lvl.latestState().dead && lvl.latestState().pos.x < lvl.length && lvl.currentFrame() < 240 * 60 * 30) {
		auto frame = lvl.currentFrame() + 1;
		while (next < inputs.size() && inputs[next].first <= frame)
			held = inputs[next++].second;
		lvl.runFrame(held);
	}

	auto& end = lvl.latestState();
	std::cout << "{\"result\":\"" << (end.dead ? "stuck" : "complete") << "\""
			  << ",\"frames\":" << lvl.currentFrame() - 1
			  << ",\"x\":" << end.pos.x
			  << ",\"length\":" << lvl.length
			  << ",\"inputs\":" << inputs.size()
			  << ",\"seconds\":" << seconds
			  << ",\"timedOut\":" << (stop ? "true" : "false")
			  << ",\"seed\":" << seed << "}" << std::endl;
	return 0;
}
