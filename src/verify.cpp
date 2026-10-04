/**
 * Verifier: plays a macro in real GD as fast as possible and compares the player with the
 * simulator every physics step.
 *
 * GJBaseGameLayer::update(dt) runs round(dt * 240) physics steps no matter how much real time
 * passed, so calling update(1/240) in a loop runs exactly one step per call. Inputs go through
 * GJBaseGameLayer::handleButton right before each step's processCommands, which acts immediately
 * (no timestamped queue). Real step c (0-based since resetLevel, as in real.txt) is sim state
 * gameStates[c + 1]. A macro input at gdr frame F is held from real step F - 1 on (as Eclipse
 * plays it) but from sim step F - 1 = real step F - 2: GD moves the player on the step it handles
 * a press, the sim one step later, which is the usual `compare --shift 1`.
 *
 * Runs come from the "verify macro" button or from a job file: if `verify.json` exists in the
 * mod save dir at startup, it is renamed to `verify.json.taken` and its jobs are run one by one:
 *   {"jobs": [{"level": "77239587" | "main:3" | "<local level name>", "macro": "<path>.gdr2",
 *              "levelFile": "<raw downloadGJLevel22 response>" (optional, then "level" is just a label)}],
 *    "exit": true}
 * Each run appends a line to `verify-results.jsonl` and writes real.txt / sim.txt / level.lvl to
 * `verify/<level>/` (`gdtools compare real.txt sim.txt --shift 1` reads them).
 */
#include <Geode/Geode.hpp>
#include <Geode/modify/AppDelegate.hpp>
#include <Geode/modify/EditLevelLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <UIBuilder.hpp>
#include <gdr/gdr.hpp>
#include <Level.hpp>
#include <chrono>

using namespace geode::prelude;

namespace {

class VerifyReplay : public gdr::Replay<VerifyReplay, gdr::Input<"">> {
 public:
	VerifyReplay() : Replay("Pathfinder", 1) {}
};

using Clock = std::chrono::steady_clock;

/// Real time spent stepping physics per rendered frame
constexpr auto frameBudget = std::chrono::milliseconds(25);
/// Positions further apart than this count as a divergence (same default as gdtools compare)
constexpr float tolerance = 0.01f;

struct Job {
	std::string level;
	std::filesystem::path macro;
	/// Raw downloadGJLevel22 response, for levels that aren't in the save
	std::filesystem::path levelFile;
};

struct Run {
	std::string levelKey;
	std::string levelName;
	std::filesystem::path macro;
	std::string levelString;
	/// (gdr frame, down) for player 1 jump inputs, sorted by frame
	std::vector<std::pair<uint64_t, bool>> inputs;
	uint64_t lastInputFrame = 0;

	std::unique_ptr<Level> sim;
	int step = 0;
	bool realHeld = false;
	bool pendingCompare = false;
	bool finished = false;
	int realAccelerating = -1; // last m_isAccelerating written to realTrace

	std::string result;
	int divergeFrame = -1;
	float divergeReal[2] = {}, divergeSim[2] = {};
	float maxDiff = 0;
	int simDeathFrame = -1;
	int simEndFrame = -1;

	std::string realTrace, simTrace;
	Clock::time_point started;
	double stepSeconds = 0;

	bool holdAt(uint64_t frame) const {
		bool held = false;
		for (auto& [f, down] : inputs) {
			if (f > frame)
				break;
			held = down;
		}
		return held;
	}

	void reset() {
		sim = std::make_unique<Level>(levelString);
		step = 0;
		realHeld = false;
		pendingCompare = false;
		divergeFrame = simDeathFrame = simEndFrame = -1;
		maxDiff = 0;
		realTrace.clear();
		simTrace.clear();
		stepSeconds = 0;
	}
};

std::deque<Job> s_jobs;
bool s_exitWhenDone = false;
bool s_jobMode = false;
std::unique_ptr<Run> s_run;
bool s_runBound = false;

bool verifying() {
	return s_run || !s_jobs.empty();
}

Run* activeRun(GJBaseGameLayer* layer) {
	if (!s_run || !s_runBound || static_cast<GJBaseGameLayer*>(PlayLayer::get()) != layer)
		return nullptr;
	return s_run.get();
}

/// Same format as Player::modeString in the sim
std::string realMode(PlayerObject* p) {
	char vehicle = p->m_isShip ? 's' : p->m_isBall ? 'b' : p->m_isBird ? 'u' : p->m_isDart ? 'w'
		: p->m_isRobot ? 'r' : p->m_isSpider ? 'p' : p->m_isSwing ? 'i' : 'c';
	std::string mode(1, vehicle);
	if (p->m_isUpsideDown) mode += 'u';
	if (p->m_vehicleSize != 1.f) mode += 'm';
	if (p->m_isOnGround) mode += 'g';
	return mode;
}

GJGameLevel* resolveLevel(std::string const& key) {
	if (key.starts_with("main:"))
		return GameLevelManager::get()->getMainLevel(numFromString<int>(key.substr(5)).unwrapOr(0), false);

	if (!key.empty() && std::all_of(key.begin(), key.end(), ::isdigit)) {
		// Without level data (not in the save) the PlayLayer never starts: use "levelFile" instead
		auto level = GameLevelManager::get()->getSavedLevel(numFromString<int>(key).unwrapOr(0));
		return level && !std::string(level->m_levelString).empty() ? level : nullptr;
	}

	for (auto level : CCArrayExt<GJGameLevel*>(LocalLevelManager::get()->m_localLevels)) {
		if (std::string(level->m_levelName) == key)
			return level;
	}
	return nullptr;
}

/// A level from a raw downloadGJLevel22 response (`1:id:2:name:...:4:<level string>...#hashes`).
/// Physics only needs the level string; GameLevelManager::responseToDict doesn't parse this format.
GJGameLevel* levelFromResponse(std::filesystem::path const& path) {
	auto response = file::readString(path);
	if (response.isErr())
		return nullptr;
	auto& str = response.unwrap();
	auto fields = utils::string::split(str.substr(0, str.find('#')), ":");

	auto level = GJGameLevel::create();
	level->m_levelType = GJLevelType::Saved;
	for (size_t i = 0; i + 1 < fields.size(); i += 2) {
		if (fields[i] == "1")
			level->m_levelID = numFromString<int>(fields[i + 1]).unwrapOr(0);
		else if (fields[i] == "2")
			level->m_levelName = fields[i + 1];
		else if (fields[i] == "4")
			level->m_levelString = fields[i + 1];
	}
	return level;
}

Result<std::vector<std::pair<uint64_t, bool>>> readMacro(std::filesystem::path const& path) {
	GEODE_UNWRAP_INTO(auto data, file::readBinary(path));
	auto replay = VerifyReplay::importData(data);
	if (replay.isErr())
		return Err("can't read macro: {}", replay.unwrapErr());
	if (replay.unwrap().framerate != 240)
		return Err("macro is {} tps, the verifier needs 240", replay.unwrap().framerate);

	std::vector<std::pair<uint64_t, bool>> inputs;
	for (auto& i : replay.unwrap().inputs) {
		if (i.button == 1 && !i.player2)
			inputs.emplace_back(i.frame, i.down);
	}
	std::stable_sort(inputs.begin(), inputs.end(), [](auto& a, auto& b) { return a.first < b.first; });
	return Ok(inputs);
}

std::string safeName(std::string name) {
	for (auto& c : name) {
		if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_')
			c = '_';
	}
	return name;
}

void writeResult(Run& run) {
	auto steps = std::max(run.step, 1);
	auto json = matjson::makeObject({
		{"level", run.levelKey},
		{"name", run.levelName},
		{"macro", utils::string::pathToString(run.macro)},
		{"result", run.result},
		{"frames", run.step},
		{"simDeathFrame", run.simDeathFrame},
		{"simEndFrame", run.simEndFrame},
		{"divergeFrame", run.divergeFrame},
		{"divergeReal", run.divergeFrame >= 0 ? matjson::Value(std::vector<matjson::Value>{run.divergeReal[0], run.divergeReal[1]}) : matjson::Value(nullptr)},
		{"divergeSim", run.divergeFrame >= 0 ? matjson::Value(std::vector<matjson::Value>{run.divergeSim[0], run.divergeSim[1]}) : matjson::Value(nullptr)},
		{"maxDiff", run.maxDiff},
		{"stepsPerSecond", run.stepSeconds > 0 ? steps / run.stepSeconds : 0.0},
	});
	auto line = json.dump(matjson::NO_INDENTATION);
	log::info("verify: {}", line);

	auto saveDir = Mod::get()->getSaveDir();
	std::ofstream(saveDir / "verify-results.jsonl", std::ios::app) << line << "\n";

	auto dir = saveDir / "verify" / safeName(run.levelKey);
	(void)file::createDirectoryAll(dir);
	(void)file::writeString(dir / "real.txt", run.realTrace);
	(void)file::writeString(dir / "sim.txt", run.simTrace);
	(void)file::writeString(dir / "level.lvl", run.levelString);
	(void)file::writeString(dir / "result.json", json.dump());

	std::string summary = run.result == "complete" && run.divergeFrame < 0
		? "matches the sim"
		: run.divergeFrame >= 0 ? fmt::format("diverged at frame {}", run.divergeFrame) : "no divergence";
	Notification::create(fmt::format("{}: {}, {}", run.levelName, run.result, summary),
		run.result == "complete" && run.divergeFrame < 0 ? NotificationIcon::Success : NotificationIcon::Warning, 4)->show();
}

Result<> startRun(std::string const& levelKey, GJGameLevel* level, std::filesystem::path const& macro) {
	if (!level)
		return Err("level {} not found", levelKey);

	auto run = std::make_unique<Run>();
	run->levelKey = levelKey;
	run->levelName = level->m_levelName;
	run->macro = macro;
	run->levelString = ZipUtils::decompressString(level->m_levelString, true, 0);
	if (run->levelString.empty())
		return Err("level {} has no level string", levelKey);

	GEODE_UNWRAP_INTO(run->inputs, readMacro(macro));
	run->lastInputFrame = run->inputs.empty() ? 0 : run->inputs.back().first;
	run->reset();

	s_run = std::move(run);
	s_runBound = false;
	CCDirector::get()->replaceScene(PlayLayer::scene(level, false, false));
	return Ok();
}

void finishRun(Run& run, std::string result) {
	run.finished = true;
	run.result = std::move(result);
	writeResult(run);

	queueInMainThread([] {
		if (auto pl = PlayLayer::get())
			pl->onQuit();
		s_run.reset();
		s_runBound = false;
	});
}

/// Drives the job queue between runs (a run itself is driven by the hooks below)
class JobRunner : public CCObject {
 public:
	static JobRunner* get() {
		static JobRunner* runner = [] {
			auto r = new JobRunner();
			CCScheduler::get()->scheduleSelector(schedule_selector(JobRunner::tick), r, 0.25f, false);
			return r;
		}();
		return runner;
	}

	void tick(float) {
		if (!s_jobMode || s_run || PlayLayer::get())
			return;
		if (typeinfo_cast<CCTransitionScene*>(CCDirector::get()->getRunningScene()))
			return;

		if (s_jobs.empty()) {
			s_jobMode = false;
			log::info("verify: all jobs done");
			if (s_exitWhenDone)
				game::exit(false);
			return;
		}

		auto job = s_jobs.front();
		s_jobs.pop_front();
		auto level = job.levelFile.empty() ? resolveLevel(job.level) : levelFromResponse(job.levelFile);
		if (auto res = startRun(job.level, level, job.macro); res.isErr()) {
			log::error("verify: {}: {}", job.level, res.unwrapErr());
			auto line = matjson::makeObject({
				{"level", job.level},
				{"macro", utils::string::pathToString(job.macro)},
				{"result", "error"},
				{"error", res.unwrapErr()},
			}).dump(matjson::NO_INDENTATION);
			std::ofstream(Mod::get()->getSaveDir() / "verify-results.jsonl", std::ios::app) << line << "\n";
		}
	}
};

/// Job mode runs in the background: silence menu music and sound effects (GD exits afterwards)
void muteAll() {
	auto engine = FMODAudioEngine::get();
	FMOD::ChannelGroup* master = nullptr;
	bool muted = engine && engine->m_system && engine->m_system->getMasterChannelGroup(&master) == FMOD_OK
		&& master && master->setMute(true) == FMOD_OK;
	if (muted)
		master->getMute(&muted);
	log::info("verify: sound muted: {}", muted);
}

void loadJobFile() {
	auto path = Mod::get()->getSaveDir() / "verify.json";
	if (!std::filesystem::exists(path))
		return;

	auto taken = path;
	taken += ".taken";
	std::error_code ec;
	std::filesystem::rename(path, taken, ec);

	auto json = file::readJson(taken);
	if (json.isErr()) {
		log::error("verify: bad job file: {}", json.unwrapErr());
		return;
	}
	auto& root = json.unwrap();
	s_exitWhenDone = root["exit"].asBool().unwrapOr(false);
	if (auto jobs = root["jobs"].asArray(); jobs.isOk()) {
		for (auto& j : jobs.unwrap()) {
			s_jobs.push_back({
				j["level"].isString() ? j["level"].asString().unwrapOr("") : std::to_string(j["level"].asInt().unwrapOr(0)),
				j["macro"].asString().unwrapOr(""),
				j["levelFile"].asString().unwrapOr(""),
			});
		}
	}
	log::info("verify: loaded {} jobs", s_jobs.size());
	s_jobMode = true;
}

// Read the jobs as soon as the mod loads, so the no-pause hooks already cover the loading screen
// (minimising GD there pauses the director and loading never finishes). They start at the menu.
$on_mod(Loaded) {
	loadJobFile();
	if (s_jobMode)
		muteAll();
}

void pickAndVerify(std::string levelKey, Ref<GJGameLevel> level) {
	async::spawn([levelKey, level]() -> arc::Future<void> {
		auto picked = co_await file::pick(file::PickMode::OpenFile, {});
		if (picked.isErr() || !picked.unwrap().has_value())
			co_return;
		auto path = *picked.unwrap();
		queueInMainThread([levelKey, level, path] {
			if (auto res = startRun(levelKey, level, path); res.isErr())
				Notification::create(res.unwrapErr(), NotificationIcon::Error)->show();
		});
	});
}

std::string levelKeyOf(GJGameLevel* level) {
	if (level->m_levelType == GJLevelType::Main)
		return fmt::format("main:{}", level->m_levelID.value());
	if (level->m_levelID.value() > 0)
		return std::to_string(level->m_levelID.value());
	return level->m_levelName;
}

} // namespace

/// Used by debug.cpp to skip its per-frame log line (tens of thousands per second while verifying)
bool verifierRunning() {
	return verifying();
}

class $modify(VerifyBGL, GJBaseGameLayer) {
	void update(float dt) {
		auto run = activeRun(this);
		if (!run || !m_started)
			return GJBaseGameLayer::update(dt);
		if (run->finished)
			return;

		auto start = Clock::now();
		while (Clock::now() - start < frameBudget) {
			GJBaseGameLayer::update(1 / 240.f);

			if (m_player1->m_isDead) {
				run->stepSeconds += std::chrono::duration<double>(Clock::now() - start).count();
				return finishRun(*run, "died");
			}
			if (m_levelEndAnimationStarted) {
				run->stepSeconds += std::chrono::duration<double>(Clock::now() - start).count();
				return finishRun(*run, "complete");
			}
			if (run->step > std::max<int64_t>(run->lastInputFrame, run->simEndFrame) + 240 * 30) {
				run->stepSeconds += std::chrono::duration<double>(Clock::now() - start).count();
				return finishRun(*run, "timeout");
			}
		}
		run->stepSeconds += std::chrono::duration<double>(Clock::now() - start).count();
	}

	void toggleFlipped(bool flip, bool noEffects) {
		if (auto run = activeRun(this); run && !run->finished)
			run->realTrace += fmt::format("Flip {} {} {} from {:.6f}\n", run->step, flip, noEffects, m_gameState.m_levelFlipping);
		GJBaseGameLayer::toggleFlipped(flip, noEffects);
	}

	void processCommands(float dt, bool isHalfTick, bool isLastTick) {
		auto run = activeRun(this);
		if (!run || run->finished)
			return GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);

		bool held = run->holdAt(run->step + 1);
		if (held != run->realHeld) {
			GJBaseGameLayer::handleButton(held, 1, true);
			run->realHeld = held;
		}

		auto& state = run->sim->runFrame(run->holdAt(run->step + 2));
		if (state.dead && run->simDeathFrame < 0)
			run->simDeathFrame = run->step;
		if (state.pos.x >= run->sim->length && run->simEndFrame < 0)
			run->simEndFrame = run->step;

		GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
		run->pendingCompare = true;
	}

	void updateCamera(float dt) {
		GJBaseGameLayer::updateCamera(dt);

		auto run = activeRun(this);
		if (!run || !run->pendingCompare)
			return;
		run->pendingCompare = false;

		auto& sim = run->sim->latestState();
		float rx = m_player1->getPositionX(), ry = m_player1->getPositionY() - 105;
		float sx = sim.pos.x, sy = sim.pos.y - 15;
		double vel = m_player1->m_yVelocity * 60 * 0.9 * (m_player1->m_isUpsideDown ? -1 : 1);

		run->realTrace += fmt::format("Frame {} X {:.8f} Y {:.8f} Vel {:.8f} Rot {:.8f} Mode {}\n",
			run->step, rx, ry, vel, m_player1->getRotation(), realMode(m_player1));
		run->simTrace += fmt::format("Frame {} X {:.8f} Y {:.8f} Vel {:.8f} Rot {:.8f} Mode {}\n",
			run->step + 1, sx, sy, sim.velocity, sim.rotation, sim.modeString());
		// Player 2 in dual mode: `P2 <frame> X .. Y .. Vel .. Rot .. Mode ..` (sim: Level::gameStates2)
		if (m_gameState.m_isDualMode) {
			double vel2 = m_player2->m_yVelocity * 60 * 0.9 * (m_player2->m_isUpsideDown ? -1 : 1);
			run->realTrace += fmt::format("P2 {} X {:.8f} Y {:.8f} Vel {:.8f} Rot {:.8f} Mode {}\n", run->step,
				m_player2->getPositionX(), m_player2->getPositionY() - 105, vel2, m_player2->getRotation(), realMode(m_player2));
		}
		if (sim.dual) {
			auto& sim2 = run->sim->gameStates2.back();
			run->simTrace += fmt::format("P2 {} X {:.8f} Y {:.8f} Vel {:.8f} Rot {:.8f} Mode {}\n", run->step + 1,
				sim2.pos.x, sim2.pos.y - 15, sim2.velocity, sim2.rotation, sim2.modeString());
		}
		// `Flags <frame> og .. og2 .. rotating .. spin .. slope <on><was> boosted .. acc .. btn ..`: player 1's
		// state flags, in the format of gd-sim-test's Flags lines (PF_FLAGS=1); `Flags2` for player 2 in dual mode
		for (auto p : {m_player1, m_player2}) {
			if (p == m_player2 && !m_gameState.m_isDualMode)
				break;
			run->realTrace += fmt::format("{} {} og {} og2 {} rotating {} spin {:.8f} slope {}{} boosted {} acc {} btn {}\n",
				p == m_player1 ? "Flags" : "Flags2", run->step, (int)p->m_isOnGround, (int)p->m_isOnGround2,
				(int)p->m_isRotating, p->m_rotationSpeed, (int)p->m_isOnSlope, (int)p->m_wasOnSlope,
				(int)p->m_maybeIsBoosted, (int)p->m_isAccelerating, (int)p->m_jumpBuffered);
		}
		// `Acc <frame> <real m_isAccelerating> <sim accelerating>` whenever GD's flag changes
		if (m_player1->m_isAccelerating != run->realAccelerating) {
			run->realAccelerating = m_player1->m_isAccelerating;
			run->realTrace += fmt::format("Acc {} {} {}\n", run->step, run->realAccelerating, (int)sim.accelerating);
		}
		// Enter effects slide objects relative to the camera: `Cam <frame> <x> <y>`, and once per run
		// `Screen <w> <h> <PlayLayer floats 0x325c 0x3268> enter <channel map> exit <channel map>`
		if (run->step == 0) {
			auto win = CCDirector::get()->getWinSize();
			auto at = [&](int off) { return *reinterpret_cast<float*>(reinterpret_cast<char*>(this) + off); };
			auto map = [](gd::vector<int> const& v) {
				std::string s;
				for (size_t i = 0; i < v.size() && i < 4; ++i) s += fmt::format(" {}", v[i]);
				return fmt::format("{}:{}", v.size(), s);
			};
			run->realTrace += fmt::format("Screen {} {} {} {} enter {} exit {}\n", win.width, win.height,
				at(0x325c), at(0x3268), map(m_gameState.m_enterChannelMap), map(m_gameState.m_exitChannelMap));
		}
		auto layerFloat = [&](int off) { return *reinterpret_cast<float*>(reinterpret_cast<char*>(this) + off); };
		// `Cam <frame> <x> <y> vis <PlayLayer 0x50c 0x510, the camera updateVisibility uses> flip <level flip>`
		run->realTrace += fmt::format("Cam {} {:.4f} {:.4f} vis {:.4f} {:.4f} flip {:.6f} {}\n", run->step,
			m_gameState.m_cameraPosition.x, m_gameState.m_cameraPosition.y, layerFloat(0x50c), layerFloat(0x510),
			m_gameState.m_levelFlipping, layerFloat(0x3204));
		// The player's last X-snap block (PlayerObject 0x888), when its sprite isn't where it collides:
		// `SnapObj <frame> <id> real <x> <y> sprite <x> <y> enter <right> <left> <channel> margin <0x354> <0x400>`
		auto obj = *reinterpret_cast<GameObject**>(reinterpret_cast<char*>(m_player1) + 0x888);
		if (obj) {
			auto real = obj->getRealPosition();
			auto sprite = obj->getPosition();
			if (real.x != sprite.x || real.y != sprite.y) {
				auto at = [&](int off) { return reinterpret_cast<char*>(obj) + off; };
				run->realTrace += fmt::format("SnapObj {} {} real {:.4f} {:.4f} sprite {:.4f} {:.4f} enter {} {} {} margin {:.4f} {}\n",
					run->step, obj->m_objectID, real.x, real.y - 90, sprite.x, sprite.y - 90,
					*reinterpret_cast<short*>(at(0x3f8)), *reinterpret_cast<short*>(at(0x3fa)),
					*reinterpret_cast<short*>(at(0x3fc)), *reinterpret_cast<float*>(at(0x354)), (int)*at(0x400));
			}
		}

		float diff = std::max(std::abs(rx - sx), std::abs(ry - sy));
		if (run->simDeathFrame < 0 || run->step <= run->simDeathFrame)
			run->maxDiff = std::max(run->maxDiff, diff);
		if (diff > tolerance && run->divergeFrame < 0) {
			run->divergeFrame = run->step;
			run->divergeReal[0] = rx; run->divergeReal[1] = ry;
			run->divergeSim[0] = sx; run->divergeSim[1] = sy;
		}
		++run->step;
	}
};

// Logs GD's cube X-snap calls into real.txt (lines not starting with "Frame" are skipped by the tools):
// `Snap <frame> obj <x> <y> prev <x> <y> offset <stored offset> x <before> -> <after>`
class $modify(VerifyPlayerObject, PlayerObject) {
	void checkSnapJumpToObject(GameObject* object) {
		auto run = activeRun(PlayLayer::get());
		if (!run || !object || this != PlayLayer::get()->m_player1)
			return PlayerObject::checkSnapJumpToObject(object);

		// 2.2081 offsets: snap object 0x888, stored player x - object x 0x838
		auto prev = *reinterpret_cast<GameObject**>(reinterpret_cast<char*>(this) + 0x888);
		double offset = *reinterpret_cast<double*>(reinterpret_cast<char*>(this) + 0x838);
		float before = getPositionX();
		PlayerObject::checkSnapJumpToObject(object);
		run->realTrace += fmt::format("Snap {} obj {} {} prev {} {} offset {:.4f} x {:.4f} -> {:.4f}\n",
			run->step, object->getPositionX(), object->getPositionY() - 90,
			prev ? prev->getPositionX() : 0.f, prev ? prev->getPositionY() - 90 : 0.f, offset, before, getPositionX());
	}
};

class $modify(VerifyPlayLayer, PlayLayer) {
	bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
		if (!PlayLayer::init(level, useReplay, dontCreateObjects))
			return false;
		if (s_run && !s_runBound) {
			s_runBound = true;
			// Test mode (as with a start position) keeps the run from saving progress or awarding stars
			m_isTestMode = true;
		}
		return true;
	}

	void resetLevel() {
		PlayLayer::resetLevel();
		if (auto run = activeRun(this); run && !run->finished)
			run->reset();
	}

	// `Death <frame> <player 1|2> obj <id> <x> <y> rot <rotation> type <object type>` (obj -1 without an object,
	// e.g. a slope or the mode's bounds). GD calls it with its anticheat spike every step; those are skipped.
	void destroyPlayer(PlayerObject* player, GameObject* object) {
		if (auto run = activeRun(this); run && !run->finished && object != m_anticheatSpike) {
			if (object)
				run->realTrace += fmt::format("Death {} {} obj {} {:.4f} {:.4f} rot {:.4f} type {}\n", run->step,
					player == m_player2 ? 2 : 1, object->m_objectID, object->getRealPosition().x,
					object->getRealPosition().y - 90, object->getRotation(), (int)object->m_objectType);
			else
				run->realTrace += fmt::format("Death {} {} obj -1\n", run->step, player == m_player2 ? 2 : 1);
		}
		PlayLayer::destroyPlayer(player, object);
	}

	void levelComplete() {
		if (activeRun(this))
			return;
		PlayLayer::levelComplete();
	}

	void pauseGame(bool unfocused) {
		if (activeRun(this))
			return;
		PlayLayer::pauseGame(unfocused);
	}

	void startMusic() {
		if (activeRun(this))
			return;
		PlayLayer::startMusic();
	}
};

class $modify(VerifyAppDelegate, AppDelegate) {
	void applicationWillResignActive() {
		if (verifying())
			return;
		AppDelegate::applicationWillResignActive();
	}

	void applicationDidEnterBackground() {
		if (verifying())
			return;
		AppDelegate::applicationDidEnterBackground();
	}
};

class $modify(VerifyMenuLayer, MenuLayer) {
	bool init() {
		if (!MenuLayer::init())
			return false;
		if (s_jobMode) {
			muteAll();
			JobRunner::get();
		}
		return true;
	}
};

class $modify(VerifyLevelInfoLayer, LevelInfoLayer) {
	bool init(GJGameLevel* level, bool challenge) {
		if (!LevelInfoLayer::init(level, challenge))
			return false;

		Build<CCSprite>::createSpriteName("GJ_playBtn2_001.png")
			.scale(0.45)
			.intoMenuItem([this]() { pickAndVerify(levelKeyOf(m_level), m_level); })
			.id("pathfinder-verify-button")
			.parent(getChildByID("other-menu"))
			.matchPos(getChildByIDRecursive("list-button"))
			.move(0, 90);
		return true;
	}
};

class $modify(VerifyEditLevelLayer, EditLevelLayer) {
	bool init(GJGameLevel* level) {
		if (!EditLevelLayer::init(level))
			return false;

		Build<CCSprite>::createSpriteName("GJ_playBtn2_001.png")
			.scale(0.45)
			.intoMenuItem([this]() { pickAndVerify(levelKeyOf(m_level), m_level); })
			.id("pathfinder-verify-button")
			.intoNewParent(CCMenu::create())
			.parent(this)
			.id("pathfinder-verify-menu")
			.matchPos(getChildByIDRecursive("delete-button"))
			.move(-45, -100);
		return true;
	}
};
