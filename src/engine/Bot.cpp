#include "Bot.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string_view>

#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/GameStatsManager.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <chrono>

#include "Engine.hpp"
#include "Experiments.hpp"
#include "InputFile.hpp"
#include "LeanLevel.hpp"
#include "Snapshot.hpp"
#include "Solver.hpp"
#include "ui/ProgressPopup.hpp"

#undef small

using namespace geode::prelude;

namespace gdpf {

namespace {
    // How far the segment machinery is allowed to widen and back up when a segment cannot
    // be solved. Each failure doubles the segment and rewinds the resumed partial path by
    // that much, so the cap decides how far back a bad approach can be re-solved from.
    int s_segmentScaleMax = 4;
    int s_showSearch = -1;

    double nowSeconds() {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }

    constexpr int kVerifyMaxTicks = 240 * 60 * 20;
    constexpr double kBudgetMin = 0.004;
    constexpr double kBudgetMax = 0.015;
    constexpr double kTargetFrame = 1.0 / 60.0;

    double s_budget = 0.011;

    void adaptBudget(double frameDt) {
        if (frameDt > kTargetFrame + 0.003) s_budget -= 0.001;
        else if (frameDt < kTargetFrame + 0.0005) s_budget += 0.0005;
        s_budget = std::clamp(s_budget, kBudgetMin, kBudgetMax);
    }
}

void SafeModeState::capture(PlayLayer* pl) {
    if (!pl || !pl->m_level) return;
    auto gsm = GameStatsManager::sharedState();
    statJumps = gsm->getStat("1");
    statAttempts = gsm->getStat("2");
    auto lvl = pl->m_level;
    levelAttempts = lvl->m_attempts.value();
    levelJumps = lvl->m_jumps.value();
    levelClicks = lvl->m_clicks.value();
    normalPercent = lvl->m_normalPercent.value();
    practicePercent = lvl->m_practicePercent;
    orbCompletion = lvl->m_orbCompletion.value();
    newNormalPercent2 = lvl->m_newNormalPercent2.value();
    bestTime = lvl->m_bestTime;
    captured = true;
}

void SafeModeState::restore(PlayLayer* pl) {
    if (!captured || !pl || !pl->m_level) return;
    auto gsm = GameStatsManager::sharedState();
    gsm->setStat("1", statJumps);
    gsm->setStat("2", statAttempts);
    auto lvl = pl->m_level;
    lvl->m_attempts = levelAttempts;
    lvl->m_jumps = levelJumps;
    lvl->m_clicks = levelClicks;
    lvl->m_normalPercent = normalPercent;
    lvl->m_practicePercent = practicePercent;
    lvl->m_orbCompletion = orbCompletion;
    lvl->m_newNormalPercent2 = newNormalPercent2;
    lvl->m_bestTime = bestTime;
}

std::string makeLevelKey(GJGameLevel* level) {
    if (!level) return "none";
    std::string str = level->m_levelString;
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : str) { h ^= c; h *= 1099511628211ull; }
    int id = level->m_levelID.value();
    std::string name = level->m_levelName;
    if (id == 0) {
        for (unsigned char c : name) { h ^= c; h *= 1099511628211ull; }
    }
    return fmt::format("{}-{:016x}", id, h);
}

std::string levelDisplayName(GJGameLevel* level) {
    if (!level) return "?";
    std::string name = level->m_levelName;
    return fmt::format("{} (id {})", name, level->m_levelID.value());
}

Bot& Bot::get() {
    static Bot instance;
    return instance;
}

bool Bot::setGuideFile(std::string const& path, float radius) {
    auto text = file::readString(std::filesystem::path(path));
    if (text.isErr()) {
        log::error("[bot] guide file {} could not be read: {}", path, text.unwrapErr());
        return false;
    }
    auto pts = std::make_shared<std::vector<Solver::GuidePoint>>();
    std::string const& s = text.unwrap();
    size_t pos = 0;
    while (pos < s.size()) {
        size_t e = s.find('\n', pos);
        std::string_view line(s.data() + pos, (e == std::string::npos ? s.size() : e) - pos);
        pos = e == std::string::npos ? s.size() : e + 1;
        if (line.empty() || line[0] == '#') continue;
        float x = 0.f, y = 0.f;
        int tick = 0;
        if (std::sscanf(std::string(line).c_str(), "%d %f %f", &tick, &x, &y) == 3) pts->push_back({x, y});
    }
    if (pts->empty()) {
        log::error("[bot] guide file {} holds no trajectory samples", path);
        return false;
    }
    m_guide = pts;
    m_guideRadius = radius > 0.f ? radius : 120.f;
    log::info("[bot] guide loaded: {} samples from {} (radius {:.0f})", pts->size(), path, m_guideRadius);
    return true;
}

void Bot::requestSolve(GJGameLevel* level, bool playAfter, bool allowCache) {
    m_allowCache = allowCache;
    m_pending = Request::Solve;
    m_pendingLevel = level;
    m_pendingSince = nowSeconds();
    m_playWhenSolved = playAfter;
    m_solveTimeLimit = (double) Mod::get()->getSettingValue<int64_t>("time-limit");
    log::info("[bot] solve requested for {} (play after: {})", levelDisplayName(level), playAfter);
}

void Bot::requestPlay(GJGameLevel* level, InputList inputs) {
    m_pending = Request::PlayInputs;
    m_pendingLevel = level;
    m_pendingSince = nowSeconds();
    m_inputs = std::move(inputs);
    log::info("[bot] playback requested for {} ({} events)", levelDisplayName(level), m_inputs.size());
}

void Bot::requestVerify(GJGameLevel* level, InputList inputs, int runs) {
    m_playAfterVerify = false;
    m_pending = Request::VerifyInputs;
    m_pendingLevel = level;
    m_pendingSince = nowSeconds();
    m_inputs = std::move(inputs);
    m_runsRemaining = std::max(1, runs);
    log::info("[bot] verification requested for {} ({} events, {} runs)", levelDisplayName(level), m_inputs.size(), m_runsRemaining);
}

void Bot::requestExperiment(GJGameLevel* level, std::unique_ptr<Experiment> experiment) {
    m_pending = Request::RunExperiment;
    m_pendingLevel = level;
    m_pendingSince = nowSeconds();
    m_experiment = std::move(experiment);
    log::info("[bot] experiment requested for {}", levelDisplayName(level));
}

void Bot::clearPending() {
    m_pending = Request::None;
    m_pendingLevel = nullptr;
}

bool Bot::hasPendingFor(GJGameLevel* level) const {
    if (m_pending == Request::None || !level) return false;
    if (nowSeconds() - m_pendingSince > 120.0) return false;
    if (m_pendingLevel == level) return true;
    int id = level->m_levelID.value();
    return id != 0 && m_pendingLevel && m_pendingLevel->m_levelID.value() == id;
}

bool Bot::inputBlocked() const {
    return active() && !engine::state().injecting;
}

void Bot::onPlayLayerInit(PlayLayer* pl) {
    if (!pl) return;
    if (hasPendingFor(pl->m_level)) {
        log::info("[bot] PlayLayer created for pending request ({})", (int) m_pending);
    }
}

void Bot::setForeignGameplayHooks(bool enabled) {
    static constexpr std::string_view kGameplay[] = {
        "GJBaseGameLayer::", "PlayLayer::", "PlayerObject::", "GameObject::", "EffectGameObject::",
        "EnhancedGameObject::", "RingObject::", "OBB2D::", "HardStreak::", "GJEffectManager::",
        "CheckpointObject::", "PlayerCheckpoint::", "GJGameState::", "GameToolbox::",
    };
    static constexpr std::string_view kNeverTouch[] = {
        "GJBaseGameLayer::getModifiedDelta", "GJBaseGameLayer::update",
    };
    std::map<std::string, int> changed;
    for (auto mod : Loader::get()->getAllMods()) {
        if (!mod || mod == Mod::get()) continue;
        for (auto hook : mod->getHooks()) {
            if (!hook) continue;
            auto name = hook->getDisplayName();
            bool gameplay = false;
            for (auto prefix : kGameplay) if (name.rfind(prefix, 0) == 0) { gameplay = true; break; }
            if (!gameplay) continue;
            bool protectedHook = false;
            for (auto keep : kNeverTouch) if (name.rfind(keep, 0) == 0) { protectedHook = true; break; }
            if (protectedHook) continue;
            if (enabled) {
                if (!m_disabledForeignHooks.count(hook)) continue;
                if (hook->enable()) { m_disabledForeignHooks.erase(hook); changed[mod->getID()]++; }
            } else {
                if (!hook->isEnabled()) continue;
                if (hook->disable()) { m_disabledForeignHooks.insert(hook); changed[mod->getID()]++; }
            }
        }
    }
    for (auto const& [id, n] : changed)
        log::info("[bot] {} {} hooks of {}", enabled ? "re-enabled" : "disabled", n, id);
}

Bot::StartState Bot::startStateOf(PlayLayer* pl) {
    StartState st;
    if (!pl) return st;
    auto p = pl->m_player1;
    if (!p) return st;
    st.ok = true;
    st.y = p->getPositionY();
    st.speed = p->m_playerSpeed;
    st.size = p->m_vehicleSize;
    st.flipped = p->m_isUpsideDown;
    st.mode = (p->m_isBall ? 1 : 0) | (p->m_isShip ? 2 : 0) | (p->m_isBird ? 4 : 0)
            | (p->m_isDart ? 8 : 0) | (p->m_isRobot ? 16 : 0) | (p->m_isSpider ? 32 : 0)
            | (p->m_isSwing ? 64 : 0);
    return st;
}

void Bot::onLevelStarted(PlayLayer* pl) {
    if (!pl || active()) return;
    if (!hasPendingFor(pl->m_level)) return;
    Request req = m_pending;
    clearPending();
    bool continuing = m_continueRequest;
    m_continueRequest = false;
    if (continuing && m_leanPhase) {
        m_levelKey = m_realKey;
        setForeignGameplayHooks(false);
    } else {
        m_levelKey = makeLevelKey(pl->m_level);
    }
    log::info("[bot] level started: {} key={} platformer={} 2p={}{}", levelDisplayName(pl->m_level), m_levelKey,
        pl->m_levelSettings ? pl->m_levelSettings->m_platformerMode : false,
        pl->m_levelSettings ? pl->m_levelSettings->m_twoPlayerMode : false,
        continuing ? (m_leanPhase ? " (lean copy)" : " (continuing)") : "");
    if (!continuing) {
        m_solveAttempt = 0;
        m_leanPhase = false;
        m_leanLevel = nullptr;
        m_realLevel = pl->m_level;
        m_realKey = m_levelKey;
    }
    switch (req) {
        case Request::Resume: {
            pl->pauseGame(false);
            if (!m_resumeMessage.empty()) showBotAlert("Pathfind", m_resumeMessage);
            m_resumeMessage.clear();
            if (onFinished) { auto cb = std::move(onFinished); onFinished = nullptr; cb(*this); }
            break;
        }
        case Request::Solve: {
            if (continuing && m_leanPhase) {
                auto lean = startStateOf(pl);
                if (!lean.sameAs(m_realStart)) {
                    log::error("[bot] the lean copy does not start where the real level does"
                               " (y {:.1f} vs {:.1f}, mode {:#x} vs {:#x}, speed {:.2f} vs {:.2f},"
                               " size {:.2f} vs {:.2f}, grav {} vs {}) - searching the real level",
                        lean.y, m_realStart.y, lean.mode, m_realStart.mode,
                        lean.speed, m_realStart.speed, lean.size, m_realStart.size,
                        lean.flipped ? 1 : 0, m_realStart.flipped ? 1 : 0);
                    m_leanLevel = nullptr;
                    m_leanPhase = false;
                    m_leanOverride = 0;
                    m_levelKey = m_realKey;
                    switchToLevel(m_realLevel, Request::Solve);
                    break;
                }
                startSolve(pl);
                break;
            }
            bool useCache = m_allowCache && Mod::get()->getSettingValue<bool>("use-cache");
            if (useCache) {
                auto path = Mod::get()->getSaveDir() / "paths" / (m_levelKey + ".json");
                if (std::filesystem::exists(path)) {
                    auto pf = PathFile::load(path);
                    if (pf.isOk() && !pf.unwrap().inputs.empty()) {
                        log::info("[bot] using cached path {} ({} events)", path.string(), pf.unwrap().inputs.size());
                        m_inputs = pf.unwrap().inputs;
                        startPlayback(pl);
                        break;
                    }
                }
            }
            if (startLeanSearch(pl, Request::Solve)) break;
            startSolve(pl);
            break;
        }
        case Request::PlayInputs: startPlayback(pl); break;
        case Request::VerifyInputs: startVerification(pl, m_runsRemaining); break;
        case Request::RunExperiment:
            beginSession(pl, Mode::Experiment);
            if (m_experiment) m_experiment->start(pl);
            break;
        default: break;
    }
}

void Bot::beginSession(PlayLayer* pl, Mode mode) {
    setForeignGameplayHooks(false);
    m_layer = pl;
    m_mode = mode;
    m_runResults.clear();
    m_current = RunResult{};
    m_cursor = 0;
    m_timeAccumulator = 0.0;
    m_waitTicks = 0;
    m_safe.capture(pl);
    engine::beginTakeover(pl);
    engine::setFastRestore(Mod::get()->getSettingValue<bool>("fast-restore"));
    engine::onAfterReset(pl);
    log::info("[bot] session begin: {} (fast-restore {})", modeName(mode), engine::fastRestore());
    static bool logged = false;
    if (!logged) {
        logged = true;
        for (auto fn : {"GJBaseGameLayer::getModifiedDelta", "GJBaseGameLayer::update"}) {
            std::string line;
            for (auto mod : Loader::get()->getAllMods()) {
                if (!mod) continue;
                for (auto hook : mod->getHooks()) {
                    if (!hook || hook->getDisplayName() != fn) continue;
                    line += fmt::format(" {}({}{})", mod->getID(), hook->getPriority(), hook->isEnabled() ? "" : ",off");
                }
            }
            log::info("[bot] hooks on {}:{}", fn, line);
        }
    }
}

void Bot::endSession(bool requestDone) {
    if (m_mode == Mode::Idle) return;
    stopSim();
    auto pl = m_layer;
    Mode was = m_mode;
    m_mode = Mode::Idle;
    if (pl) {
        pl->setVisible(true);
        engine::endTakeover(pl);
        m_safe.restore(pl);
    }
    engine::setDynamicObjects({});
    m_solver.reset();
    m_resumePending = false;
    // Everything below holds snapshots
    m_segments.clear();
    m_segmentPrefix.clear();
    m_resumeInputs.clear();
    m_resumeTicks = 0;
    m_deepestPath.clear();
    m_bestPrefix.clear();
    m_bestPrefixTick = 0;
    if (double back = releaseFreeMemory(); back > 1.0)
        log::info("[bot] released {:.0f} MB at session end, process now at {:.0f} MB", back, processMemoryUsedMB());
    if (m_experiment) {
        m_experimentSummary = m_experiment->summary();
        m_experimentPassed = m_experiment->passed();
        m_experiment.reset();
    }
    m_layer = nullptr;
    log::info("[bot] session end (was {}){}", modeName(was), requestDone ? "" : ", request continues on another level");
    if (requestDone && onFinished) {
        auto cb = std::move(onFinished);
        onFinished = nullptr;
        cb(*this);
    }
}

bool Bot::startLeanSearch(PlayLayer* pl, Request next) {
    bool wantLean = m_leanOverride >= 0 ? m_leanOverride != 0
                                        : Mod::get()->getSettingValue<bool>("lean-search");
    if (!wantLean) return false;
    if (!pl->m_objects || pl->m_objects->count() < 2000) return false;
    if (pl->m_levelSettings && pl->m_levelSettings->m_platformerMode) return false;
    auto info = buildLeanLevel(pl);
    if (!info.level || info.objectsAfter == 0) return false;
    if (info.objectsAfter * 10 > info.objectsBefore * 9) {
        log::info("[lean] not worth it ({} -> {} objects)", info.objectsBefore, info.objectsAfter);
        return false;
    }
    m_leanLevel = info.level;
    m_realLevel = pl->m_level;
    m_leanPhase = true;
    m_realStart = startStateOf(pl);
    log::info("[bot] continuing on a lean copy of {} ({} -> {} objects)", levelDisplayName(pl->m_level), info.objectsBefore, info.objectsAfter);
    switchToLevel(m_leanLevel, next);
    return true;
}

void Bot::switchToLevel(GJGameLevel* level, Request next) {
    setForeignGameplayHooks(true);
    endSession(false);
    m_pending = next;
    m_pendingLevel = level;
    m_pendingSince = nowSeconds();
    m_continueRequest = true;
    Ref<GJGameLevel> keep(level);
    Loader::get()->queueInMainThread([keep] {
        auto scene = PlayLayer::scene(keep, false, false);
        CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.5f, scene));
    });
}

void Bot::cancel() {
    if (!active()) return;
    auto pl = m_layer;
    stopSim();
    if (m_solver) m_solver->cancel();
    m_lastFailure = "Cancelled";
    if (pl && fastStepping()) engine::resetToStart(pl);
    if (m_leanPhase && m_realLevel) {
        m_leanPhase = false;
        m_resumeMessage.clear();
        switchToLevel(m_realLevel, Request::Resume);
        return;
    }
    endSession();
    if (pl) pl->pauseGame(false);
}

void Bot::onQuit(PlayLayer* pl) {
    setForeignGameplayHooks(true);
    if (activeOn(pl)) {
        if (m_solver) m_solver->cancel();
        m_leanPhase = false;
        endSession();
    }
}

void Bot::onAfterReset(PlayLayer* pl) {
    if (!activeOn(pl)) return;
    engine::onAfterReset(pl);
    m_safe.restore(pl);
    if (m_mode == Mode::Playing || m_mode == Mode::Verifying) {
        m_cursor = 0;
        m_timeAccumulator = 0.0;
        m_current = RunResult{};
        m_waitTicks = 0;
    }
}

bool Bot::onDeath(PlayLayer* pl, PlayerObject* player, GameObject* object) {
    if (!activeOn(pl)) return false;
    auto& st = engine::state();
    st.diedThisStep = true;
    st.deathObjectID = object ? object->m_objectID : 0;
    st.deathObject = object;
    if (player) {
        auto const& pr = engine::peekRect(player);
        st.deathPlayerRect[0] = pr.getMinX(); st.deathPlayerRect[1] = pr.getMaxX();
        st.deathPlayerRect[2] = pr.getMinY(); st.deathPlayerRect[3] = pr.getMaxY();
        st.deathVehicleSize = player->m_vehicleSize;
        int f = 0;
        f |= player->m_isOnGround ? 1 : 0;
        f |= player->m_isUpsideDown ? 2 : 0;
        f |= player->m_isShip ? 4 : 0;
        f |= player->m_isBall ? 8 : 0;
        f |= player->m_isBird ? 16 : 0;
        f |= player->m_isDart ? 32 : 0;
        f |= player->m_isRobot ? 64 : 0;
        f |= player->m_isSpider ? 128 : 0;
        f |= player->m_isSwing ? 256 : 0;
        st.deathModeFlags = f;
    }
    if (m_mode == Mode::Solving || m_mode == Mode::Verifying || m_mode == Mode::Experiment) {
        return true;
    }
    m_current.died = true;
    m_current.endTick = st.tick;
    m_current.deathObjectID = st.deathObjectID;
    return false;
}

bool Bot::onReachedEnd(PlayLayer* pl) {
    if (!activeOn(pl)) return false;
    auto& st = engine::state();
    st.reachedEndThisStep = true;
    m_current.completed = true;
    m_current.endTick = st.tick;
    if (m_mode == Mode::Solving || m_mode == Mode::Verifying || m_mode == Mode::Experiment) return true;
    return false;
}

void Bot::onLevelComplete(PlayLayer* pl) {
    if (!activeOn(pl)) return;
    if (m_mode == Mode::Playing) {
        m_current.completed = true;
        finishRun(pl, m_current);
    }
}

void Bot::finishRun(PlayLayer* pl, RunResult result) {
    result.furthestX = std::max(result.furthestX, engine::playerX(pl));
    result.furthestPercent = std::max(result.furthestPercent, engine::percent(pl));
    m_runResults.push_back(result);
    std::string killer;
    if (result.died) {
        if (auto obj = engine::state().deathObject) {
            auto const& r = engine::peekRect(obj);
            std::string groups;
            if (obj->m_groups) for (int i = 0; i < std::min<int>(obj->m_groupCount, 10); i++) groups += fmt::format("{}{}", i ? "." : "", (*obj->m_groups)[i]);
            killer = fmt::format(" killer: obj {} uid {} type {} at ({:.1f},{:.1f}) start ({:.1f},{:.1f}) rect [{:.1f}..{:.1f} x {:.1f}..{:.1f}] groups [{}] player at ({:.2f},{:.2f})",
                obj->m_objectID, obj->m_uniqueID, (int) obj->m_objectType, obj->getPositionX(), obj->getPositionY(),
                obj->m_startPosition.x, obj->m_startPosition.y, r.getMinX(), r.getMaxX(), r.getMinY(), r.getMaxY(), groups,
                engine::playerX(pl), engine::playerY(pl));
        } else if (result.deathObjectID == 0) {
            killer = " game callers:" + engine::state().deathCallers;
        }
        auto const& d = engine::state();
        if (d.deathPlayerRect[0] != 0.f || d.deathPlayerRect[1] != 0.f) {
            killer += fmt::format(" | player box [{:.2f}..{:.2f} x {:.2f}..{:.2f}] ({:.1f} wide, {:.1f} tall) size {:.2f} mode {:#x}",
                d.deathPlayerRect[0], d.deathPlayerRect[1], d.deathPlayerRect[2], d.deathPlayerRect[3],
                d.deathPlayerRect[1] - d.deathPlayerRect[0], d.deathPlayerRect[3] - d.deathPlayerRect[2],
                d.deathVehicleSize, d.deathModeFlags);
        }
    }
    log::info("[bot] run {} finished: completed={} died={} endTick={} x={:.2f} percent={:.2f} hash={:016x} deathObj={}{}",
        m_runResults.size(), result.completed, result.died, result.endTick, result.furthestX,
        result.furthestPercent, result.trajectoryHash, result.deathObjectID, killer);
    if (m_mode == Mode::Verifying && --m_runsRemaining > 0) {
        engine::restoreStart(pl);
        return;
    }
    if (m_mode == Mode::Verifying && m_playAfterVerify) {
        m_playAfterVerify = false;
        if (result.completed) {
            log::info("[bot] path verified from a fresh start ({} ticks){}", result.endTick, m_playWhenSolved ? ", playing it back" : "");
            savePath(pl, result.endTick);
            if (Mod::get()->getSettingValue<bool>("export-macro") && pl->m_level) {
                auto saved = hybrid::saveMacro(m_inputs, pl->m_level->m_levelName);
                if (saved.isOk()) {
                    m_lastMacro = saved.unwrap();
                    log::info("[bot] macro saved to {}", saved.unwrap());
                    Notification::create(fmt::format("Macro saved: {}", utils::string::pathToString(saved.unwrap().filename())),
                        NotificationIcon::Success, 3.f)->show();
                } else {
                    log::warn("[bot] could not save the macro: {}", saved.unwrapErr());
                }
            }
            if (m_playWhenSolved) {
                m_mode = Mode::Playing;
                m_runResults.clear();
                engine::restoreStart(pl);
                return;
            }
            endSession();
            return;
        }
        log::warn("[bot] found path did not survive a fresh replay (ended at tick {} / {:.2f}%)", result.endTick, result.furthestPercent);
        savePath(pl, result.endTick, false);
        if (m_solveAttempt < 3) {
            log::warn("[bot] searching again (attempt {})", m_solveAttempt + 1);
            if (m_leanLevel) {
                log::warn("[bot] the lean copy does not match this level; searching the real level instead");
                m_leanLevel = nullptr;
                m_leanPhase = false;
                m_resumeNext = true;
            }
            m_mode = Mode::Solving;
            m_runResults.clear();
            launchSolver(pl);
            return;
        }
        m_lastFailure = fmt::format("Path desynced at {:.1f}%", result.furthestPercent);
        endSession();
        reportFailure(pl, m_lastFailure);
        return;
    }
    if (m_mode == Mode::Playing && result.died) {
        endSession();
        Notification::create(fmt::format("Playback desynced at {:.1f}%", result.furthestPercent), NotificationIcon::Error)->show();
        return;
    }
    endSession();
}

void Bot::savePath(PlayLayer* pl, int ticks, bool verified) {
    PathFile pf;
    pf.levelKey = m_levelKey;
    pf.levelName = pl && pl->m_level ? std::string(pl->m_level->m_levelName) : "";
    pf.modVersion = Mod::get()->getVersion().toVString();
    pf.ticks = ticks;
    pf.inputs = m_inputs;
    auto path = Mod::get()->getSaveDir() / "paths" / (m_levelKey + (verified ? ".json" : ".unverified.json"));
    auto res = pf.save(path);
    if (res.isErr()) log::warn("[bot] could not save path: {}", res.unwrapErr());
    else log::info("[bot] path saved to {}", path.string());
}

void Bot::startPlayback(PlayLayer* pl) {
    beginSession(pl, Mode::Playing);
    engine::setDynamicObjects(engine::collectSnapshotObjects(pl));
    engine::restoreStart(pl);
}

void Bot::startVerification(PlayLayer* pl, int runs) {
    m_runsRemaining = std::max(1, runs);
    m_refTrajectory.clear();
    m_trajMismatches = 0;
    m_reportedJumpClock = false;
    beginSession(pl, Mode::Verifying);
    engine::setDynamicObjects(engine::collectSnapshotObjects(pl));
    engine::restoreStart(pl);
}

void Bot::startSolve(PlayLayer* pl) {
    beginSession(pl, Mode::Solving);
    m_lastFailure.clear();
    m_lastMacro.clear();
    m_hybrid = Mod::get()->getSettingValue<std::string>("search-mode") != "Engine only";
    m_simPhase = SimPhase::Off;
    m_simBlockedUntilX = -INFINITY;
    m_simFailures = m_simRounds = m_simTicks = 0;
    m_simProgress = 0;
    m_simCheck.reset();
    if (m_hybrid) {
        m_horizon = hybrid::scanLevel(pl);
        m_levelString = pl->m_level ? ZipUtils::decompressString(pl->m_level->m_levelString, true, 0) : "";
        if (!m_horizon.usable || m_levelString.empty()) {
            log::info("[hybrid] engine only: {}", m_levelString.empty() ? "no level string" : m_horizon.reason);
            m_hybrid = false;
        } else {
            m_simCheck = std::make_unique<Level>(m_levelString);
        }
    }
    m_resumeStale = false;
    m_bestPrefix.clear();
    m_bestPrefixTick = 0;
    {
        auto dir = Mod::get()->getSaveDir() / "dumps";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        Solver::setDumpPrefix((dir / m_levelKey).string());
    }
    launchSolver(pl);
}

void Bot::launchSolver(PlayLayer* pl) {
    if (m_solveAttempt == 0) {
        m_solveStarted = nowSeconds();
        m_solveDeadline = m_solveStarted + m_solveTimeLimit;
        m_bestPercent = 0.f;
        m_nodesTotal = 0;
        m_routesTotal = 0;
        m_segmentAttempts = 0;
        m_deepestTick = 0;
        m_deepestPath.clear();
    }
    m_solveAttempt++;
    m_segments.clear();
    m_segmentPrefix.clear();
    m_segmentScale = 1;
    resetVariants((m_solveAttempt - 1) % kSteeringVariants);
    m_segmentTicks = m_segmentTicksArg > 0 ? m_segmentTicksArg
                                          : (int) Mod::get()->getSettingValue<int64_t>("segment-seconds") * 240;
    log::info("[bot] search {} ({} s left, segments of {} ticks), process at {:.0f} MB, {:.0f} MB free",
        m_solveAttempt, (int) (m_solveDeadline - nowSeconds()), m_segmentTicks, processMemoryUsedMB(), memoryFreeMB());
    m_resumeInputs.clear();
    m_resumeTicks = 0;
    if (m_resumePath.empty() && Mod::get()->getSettingValue<bool>("resume-saved")) m_resumePath = "1";
    m_resumePending = (m_solveAttempt == 1 || m_resumeNext) && m_segmentTicks > 0 && !m_resumePath.empty();
    m_resumeNext = false;
    if (!m_resumePending) continueSearch(pl);
    pl->setVisible(s_showSearch >= 0 ? s_showSearch != 0
                                     : Mod::get()->getSettingValue<bool>("show-search"));
    if (ProgressPopup::current()) return;
    if (auto popup = ProgressPopup::create()) {
        popup->m_scene = pl->getParent() ? static_cast<CCLayer*>(pl->getParent()) : nullptr;
        popup->show();
    }
}

void Bot::resumeFromPartial(PlayLayer* pl) {
    auto path = m_resumePath == "1" ? Mod::get()->getSaveDir() / "paths" / (m_levelKey + ".partial.json")
                                    : std::filesystem::path(m_resumePath);
    auto pf = PathFile::load(path);
    if (pf.isErr()) {
        log::warn("[bot] resume: no usable partial path at {}: {}", path.string(), pf.unwrapErr());
    } else if (pf.unwrap().ticks <= 0 || pf.unwrap().inputs.empty()) {
        log::warn("[bot] resume: partial path at {} is empty", path.string());
    } else {
        auto partial = pf.unwrap();
        int target = partial.ticks;
        int step = m_segmentTicks > 0 ? m_segmentTicks : 1920;
        bool backedOff = false;
        for (int tries = 0; tries < 12 && target > 0; tries++) {
            int reached = 0;
            if (rootFromPartial(pl, partial.inputs, target, path.string().c_str(), &reached)) {
                m_resumeInputs = partial.inputs;
                m_resumeTicks = target;
                if (backedOff) m_resumeStale = true;
                break;
            }
            int from = reached > 0 && reached < target ? reached : target;
            int next = (from / step - 1) * step;
            if (next <= 0 || next >= target) { m_resumeStale = true; break; }
            log::info("[bot] resume: backing off to tick {} and retrying", next);
            target = next;
            backedOff = true;
        }
    }
    continueSearch(pl);
}

bool Bot::rootFromPartial(PlayLayer* pl, InputList const& inputs, int ticks, char const* why, int* reachedOut) {
    auto& st = engine::state();
    engine::releaseAll(pl);
    engine::setDynamicObjects(engine::collectSnapshotObjects(pl));
    engine::restoreStart(pl);
    size_t cursor = 0;
    bool died = false, ended = false;
    int every = engine::heapCheckEvery();
    int nextCheck = every > 0 ? every : ticks + 1;
    if (every > 0) engine::checkHeap("replay start", st.tick);
    while (st.tick < ticks) {
        engine::applyDueInputs(pl, inputs, cursor);
        engine::step(pl);
        if (st.diedThisStep) { died = true; break; }
        if (st.reachedEndThisStep) { ended = true; break; }
        if (st.tick >= nextCheck) {
            nextCheck = st.tick + every;
            engine::checkHeap("replay", st.tick);
        }
    }
    if (reachedOut) *reachedOut = st.tick;
    if (died || ended) {
        log::warn("[bot] resume: partial path {} at tick {} of {} at ({:.1f},{:.1f}) {:.2f}%{}{}",
            died ? "died" : "ended", st.tick, ticks, engine::playerX(pl), engine::playerY(pl), engine::percent(pl),
            died ? fmt::format(" killed by obj {}", st.deathObjectID) : std::string(),
            m_leanPhase ? " (on the lean copy, not the real level)" : "");
        engine::releaseAll(pl);
        engine::restoreStart(pl);
        return false;
    }
    engine::applyDueInputs(pl, inputs, cursor);
    auto snap = engine::takeSnapshot(pl);
    if (!snap) {
        engine::releaseAll(pl);
        engine::restoreStart(pl);
        return false;
    }
    SolvedSegment seg;
    Solver::Candidate c;
    for (auto const& ev : inputs) if (ev.tick <= ticks) c.path.push_back(ev);
    c.boundaryTick = ticks;
    c.state = snap;
    c.x = engine::playerX(pl);
    c.y = engine::playerY(pl);
    if (ticks > m_bestPrefixTick) {
        m_bestPrefixTick = ticks;
        m_bestPrefix = c.path;
    }
    seg.candidates.push_back(std::move(c));
    seg.rootTick = ticks;
    m_segments.push_back(std::move(seg));
    m_bestPercent = std::max(m_bestPercent, engine::percent(pl));
    log::info("[bot] resumed from partial path {}: {} events to tick {} at ({:.0f},{:.0f}), {:.2f}%",
        why, m_segments.back().candidates.front().path.size(), ticks, engine::playerX(pl), engine::playerY(pl), engine::percent(pl));
    return true;
}

void Bot::launchSegment(PlayLayer* pl) {
    m_segmentAttempts++;
    Solver::Segment seg;
    int rootTick = 0;
    if (!m_segments.empty()) {
        auto& last = m_segments.back();
        auto const& c = last.candidates[last.next];
        seg.root = c.state;
        seg.prefix = last.prefix;
        seg.prefix.insert(seg.prefix.end(), c.path.begin(), c.path.end());
        rootTick = c.boundaryTick;
    }
    m_segmentPrefix = seg.prefix;
    if (m_segCandidates > 0) seg.candidates = m_segCandidates;
    if (m_segExtra >= 0.0) seg.extraTime = m_segExtra;
    if (m_segMargin >= 0) seg.margin = m_segMargin;
    if (m_guide) {
        seg.guide = m_guide;
        seg.guideRadius = m_guideRadius;
        if (!m_segments.empty()) seg.rootGuideIdx = m_segments.back().candidates[m_segments.back().next].guideIdx;
    }
    int maxTicks = (int) (pl->m_levelLength / 0.9f) + 3600;
    if (rootTick > maxTicks) {
        log::warn("[bot] segment root at tick {} is past the max {} for a {:.0f} unit level", rootTick, maxTicks, pl->m_levelLength);
        finishSearchFailure(pl, false);
        return;
    }
    seg.goalTick = m_segmentTicks > 0 ? rootTick + m_segmentTicks * m_segmentScale : -1;
    double remaining = m_solveDeadline - nowSeconds();
    double limit;
    if (m_segmentTicks > 0) {
        float x = m_segments.empty() ? 0.f : m_segments.back().candidates[m_segments.back().next].x;
        double ticksLeft = std::max(1.0, (double) (pl->m_levelLength - x) / 1.3);
        double segmentsLeft = std::max(1.0, ticksLeft / (m_segmentTicks * m_segmentScale));
        limit = std::max(45.0, remaining / segmentsLeft * 2.0);
    } else {
        limit = m_variantIdx == 0 ? remaining * 0.5 : remaining;
    }
    if (m_segmentCap > 0.0) limit = std::min(limit, m_segmentCap * (double) std::max(1, m_segmentScale));
    limit = std::min(limit, remaining / (double) std::max(1, m_variantCount - m_variantIdx));
    limit = std::min(limit, remaining);
    m_solver = std::make_unique<Solver>(pl, seg);
    m_solver->setVariant(m_segmentVariant);
    m_solver->setTimeLimit(std::max(10.0, limit));
    float rootX = m_segments.empty() ? 0.f : m_segments.back().candidates[m_segments.back().next].x;
    float rootY = m_segments.empty() ? 0.f : m_segments.back().candidates[m_segments.back().next].y;
    float prevRootX = -1e9f;
    if (m_segments.size() >= 2) {
        auto const& prev = m_segments[m_segments.size() - 2];
        prevRootX = prev.candidates[prev.next].x;
    }
    if (std::fabs(rootX - prevRootX) <= 5.f && prevRootX > -1e8f) {
        auto p1 = pl->m_player1;
        log::warn("[bot] not moving: boundary at tick {} x {:.0f}, previous x {:.0f} (y {:.0f}{}{}{}{})",
            rootTick, rootX, prevRootX, rootY,
            p1 && p1->m_isDead ? ", dead" : "", p1 && p1->m_isLocked ? ", locked" : "",
            p1 && p1->m_controlsDisabled ? ", no controls" : "", pl->m_isPracticeMode ? ", practice" : "");
        segmentFailed(pl);
        return;
    }
    // The speed the player is carrying into the segment decides how much x a jump covers,
    // so a segment entered at the wrong speed can be geometrically impossible.
    auto* sp = pl->m_player1;
    float spd = sp ? sp->m_playerSpeed : 0.f;
    log::info("[bot] segment {} from tick {} at ({:.0f},{:.0f}) to {} (variant {}, {:.0f}s of {:.0f}s left, {} inputs before it, end at x {:.0f}, speed {:.2f} = {:.3f} x/tick, size {:.2f}, grav {:.1f})",
        m_segments.size() + 1, rootTick, rootX, rootY, seg.goalTick, m_segmentVariant, std::max(10.0, limit), remaining,
        seg.prefix.size(), pl->m_endXPosition, spd, engine::perTickForSpeed(spd),
        sp ? sp->m_vehicleSize : 0.f, sp ? sp->m_gravityMod : 0.f);
}

void Bot::resetVariants(int first) {
    for (int i = 0; i < kSteeringVariants; i++) {
        m_variantOrder[i] = (first + i) % kSteeringVariants;
        m_variantBest[i] = -1.f;
    }
    m_variantCount = kSteeringVariants;
    m_variantIdx = 0;
    m_segmentVariant = m_variantOrder[0];
}

void Bot::orderVariants() {
    int order[kSteeringVariants];
    for (int i = 0; i < kSteeringVariants; i++) order[i] = i;
    std::stable_sort(order, order + kSteeringVariants, [&](int a, int b) { return m_variantBest[a] > m_variantBest[b]; });
    float best = m_variantBest[order[0]];
    int count = 0;
    for (int i = 0; i < kSteeringVariants; i++) {
        int v = order[i];
        if (m_variantBest[v] >= 0.f && best - m_variantBest[v] > kVariantSlack) continue;
        m_variantOrder[count++] = v;
    }
    if (count == 0) { resetVariants(0); return; }
    m_variantCount = count;
    m_variantIdx = 0;
    m_segmentVariant = m_variantOrder[0];
    std::string shown;
    for (int i = 0; i < count; i++) shown += fmt::format("{}{} ({:.0f})", i ? ", " : "", m_variantOrder[i], m_variantBest[m_variantOrder[i]]);
    log::info("[bot] variant order: {}{}", shown, count < kSteeringVariants ? fmt::format(", dropped {}", kSteeringVariants - count) : "");
}

void Bot::segmentFailed(PlayLayer* pl) {
    if (m_solveDeadline - nowSeconds() < 15.0) { finishSearchFailure(pl, false); return; }
    if (m_candFirst && !m_segments.empty()) {
        auto& last = m_segments.back();
        if (last.next + 1 < last.candidates.size()) {
            last.next++;
            resetVariants(0);
            log::info("[bot] segment failed, trying end state {} of {} of the previous segment",
                last.next + 1, last.candidates.size());
            launchSegment(pl);
            return;
        }
    }
    if (m_variantIdx + 1 < m_variantCount) {
        m_variantIdx++;
        m_segmentVariant = m_variantOrder[m_variantIdx];
        log::info("[bot] segment failed, trying steering variant {}", m_segmentVariant);
        launchSegment(pl);
        return;
    }
    if (!m_segments.empty()) {
        auto& last = m_segments.back();
        if (last.next + 1 < last.candidates.size()) {
            last.next++;
            resetVariants(0);
            log::info("[bot] segment failed, trying end state {} of the previous segment", last.next + 1);
            launchSegment(pl);
            return;
        }
        if (m_segmentScale < s_segmentScaleMax) {
            int rootTick = last.rootTick;
            m_segments.pop_back();
            int back = rootTick - m_segmentTicks * m_segmentScale;
            if (m_segments.empty() && m_resumeTicks > 0 && back > 0
                && rootFromPartial(pl, m_resumeInputs, back, "(earlier boundary of the resumed partial path)")) {
                m_segmentScale *= 2;
                orderVariants();
                log::info("[bot] backing up {} ticks on the partial path, segments of {} ticks", rootTick - back, m_segmentTicks * m_segmentScale);
                launchSegment(pl);
                return;
            }
            m_segmentScale *= 2;
            orderVariants();
            log::info("[bot] all end states failed, merging with previous segment ({} ticks)", m_segmentTicks * m_segmentScale);
            launchSegment(pl);
            return;
        }
    } else if (m_segmentTicks > 0 && m_segmentScale < s_segmentScaleMax) {
        m_segmentScale *= 2;
        orderVariants();
        log::info("[bot] first segment failed, retrying with {} ticks", m_segmentTicks * m_segmentScale);
        launchSegment(pl);
        return;
    }
    finishSearchFailure(pl, false);
}

void Bot::setSegmentScaleMax(int v) { s_segmentScaleMax = std::max(1, v); }
void Bot::setShowSearch(int v) { s_showSearch = v; }

void Bot::savePartial(PlayLayer* pl) {
    if (m_bestPrefix.empty() || m_bestPrefixTick <= 0) return;
    auto path = Mod::get()->getSaveDir() / "paths" / (m_levelKey + ".partial.json");
    PathFile pf;
    pf.levelKey = m_levelKey;
    pf.levelName = pl && pl->m_level ? std::string(pl->m_level->m_levelName) : "";
    pf.modVersion = Mod::get()->getVersion().toVString();
    pf.ticks = m_bestPrefixTick;
    pf.inputs = m_bestPrefix;
    auto existing = PathFile::load(path);
    if (existing.isOk() && !m_resumeStale && existing.unwrap().ticks >= pf.ticks) return;
    auto res = pf.save(path);
    log::info("[bot] partial path saved ({} events, tick {}, {:.2f}%){}",
        pf.inputs.size(), pf.ticks, m_bestPercent, res.isErr() ? " failed: " + res.unwrapErr() : "");
}

void Bot::finishSearchFailure(PlayLayer* pl, bool cancelled) {
    m_lastFailure = cancelled ? "Cancelled" : fmt::format("No path found. Reached {:.1f}%", m_bestPercent);
    if (!cancelled) savePartial(pl);
    if (!cancelled && !m_deepestPath.empty()) {
        auto path = Mod::get()->getSaveDir() / "paths" / (m_levelKey + ".deepest.json");
        PathFile pf;
        pf.levelKey = m_levelKey;
        pf.levelName = pl && pl->m_level ? std::string(pl->m_level->m_levelName) : "";
        pf.modVersion = Mod::get()->getVersion().toVString();
        pf.ticks = m_deepestTick;
        pf.inputs = m_deepestPath;
        auto res = pf.save(path);
        log::info("[bot] deepest lane saved ({} events, to tick {}): {}{}", pf.inputs.size(), pf.ticks, path.string(),
            res.isErr() ? " failed: " + res.unwrapErr() : "");
    }
    m_solver.reset();
    if (m_leanPhase && m_realLevel) {
        m_leanPhase = false;
        m_resumeMessage = cancelled ? "" : m_lastFailure;
        log::warn("[bot] {}", m_lastFailure);
        switchToLevel(m_realLevel, Request::Resume);
        return;
    }
    endSession();
    if (!cancelled) reportFailure(pl, m_lastFailure);
}

float Bot::progressPercent() const {
    float p = m_bestPercent;
    if (m_solver) p = std::max(p, m_solver->furthestPercent());
    if (m_simPhase == SimPhase::Searching) p = std::max(p, (float) m_simProgress.load());
    return p;
}

char const* Bot::searchPhase() const {
    switch (m_simPhase) {
        case SimPhase::Searching: return "simulator";
        case SimPhase::Checking: return "checking";
        default: return m_hybrid ? "engine" : "engine only";
    }
}

double Bot::solveSeconds() const { return m_solveStarted > 0.0 ? nowSeconds() - m_solveStarted : 0.0; }

uint64_t Bot::progressRoutes() const {
    return m_routesTotal + (m_solver ? m_solver->stats().routesEnded : 0);
}

void Bot::reportFailure(PlayLayer* pl, std::string const& text) {
    log::warn("[bot] {}", text);
    if (!pl) return;
    engine::resetToStart(pl);
    pl->pauseGame(false);
    showBotAlert("Pathfind", text);
}

void Bot::onSolveFinished(PlayLayer* pl, bool found) {
    auto& s = *m_solver;
    log::info("[bot] segment search finished: found={} cancelled={} furthest={:.2f}% candidates={} nodes={} steps={} snapshots={} restores={} merged={} time={:.1f}s reason='{}'",
        found, s.cancelled(), s.furthestPercent(), s.candidates().size(), s.stats().nodesExpanded, s.stats().stepsSimulated,
        s.stats().snapshots, s.stats().restores, s.stats().merged, s.stats().seconds, s.failureReason());
    m_bestPercent = std::max(m_bestPercent, s.furthestPercent());
    m_nodesTotal += s.stats().nodesExpanded;
    m_routesTotal += s.stats().routesEnded;
    if (!found && s.deepestTick() > m_deepestTick) {
        m_deepestTick = s.deepestTick();
        m_deepestPath = s.deepestPath();
    }
    if (s.cancelled()) { finishSearchFailure(pl, true); return; }
    if (found && !s.candidates().empty()) {
        auto const& c = s.candidates().front();
        if (c.reachedEnd) {
            m_inputs = m_segmentPrefix;
            m_inputs.insert(m_inputs.end(), c.path.begin(), c.path.end());
            log::info("[bot] path ({} segments, {} events): {}", m_segments.size() + 1, m_inputs.size(), describeInputs(m_inputs, 60));
            m_solver.reset();
            finishFound(pl);
            return;
        }
        SolvedSegment seg;
        seg.candidates = s.candidates();
        if (m_guide) {
            std::stable_sort(seg.candidates.begin(), seg.candidates.end(),
                [](Solver::Candidate const& a, Solver::Candidate const& b) { return a.guideIdx > b.guideIdx; });
            for (size_t i = 0; i < seg.candidates.size(); i++)
                log::info("[bot] boundary candidate {}: tick {} at ({:.0f},{:.0f}) guide index {}", i + 1,
                    seg.candidates[i].boundaryTick, seg.candidates[i].x, seg.candidates[i].y, seg.candidates[i].guideIdx);
        } else if (m_planOrder && seg.candidates.size() > 1) {
            int ybias = m_candY;
            bool byX = m_candX;
            std::stable_sort(seg.candidates.begin(), seg.candidates.end(),
                [ybias, byX](Solver::Candidate const& a, Solver::Candidate const& b) {
                    if (a.planDepth != b.planDepth) return a.planDepth > b.planDepth;
                    if (byX && std::fabs(a.x - b.x) > 1.f) return a.x > b.x;
                    if (a.planPull != b.planPull) return a.planPull < b.planPull;
                    if (ybias < 0) return a.y < b.y;
                    if (ybias > 0) return a.y > b.y;
                    return false;
                });
            for (size_t i = 0; i < seg.candidates.size(); i++)
                log::info("[bot] boundary candidate {}: tick {} at ({:.0f},{:.0f}) planDepth {} pull {}", i + 1,
                    seg.candidates[i].boundaryTick, seg.candidates[i].x, seg.candidates[i].y,
                    seg.candidates[i].planDepth, seg.candidates[i].planPull);
        }
        seg.prefix = m_segmentPrefix;
        seg.rootTick = seg.candidates.front().boundaryTick;
        if (seg.rootTick > m_bestPrefixTick) {
            m_bestPrefixTick = seg.rootTick;
            m_bestPrefix = m_segmentPrefix;
            auto const& c = seg.candidates.front();
            m_bestPrefix.insert(m_bestPrefix.end(), c.path.begin(), c.path.end());
            savePartial(pl);
        }
        m_segments.push_back(std::move(seg));
        m_segmentScale = 1;
        resetVariants(0);
        m_solver.reset();
        continueSearch(pl);
        return;
    }
    m_variantBest[m_segmentVariant] = std::max(m_variantBest[m_segmentVariant], s.furthestX());
    m_solver.reset();
    segmentFailed(pl);
}

void Bot::onLayerUpdate(PlayLayer* pl, float dt) {
    if (!activeOn(pl)) return;
    auto& st = engine::state();
    if (fastStepping()) adaptBudget(dt);
    switch (m_mode) {
        case Mode::Playing: {
            m_timeAccumulator += dt;
            int steps = (int) std::floor(m_timeAccumulator * 240.0 + 1e-6);
            if (steps > 8) steps = 8;
            m_timeAccumulator -= steps / 240.0;
            if (m_timeAccumulator < 0) m_timeAccumulator = 0;
            for (int i = 0; i < steps; i++) {
                engine::applyDueInputs(pl, m_inputs, m_cursor);
                engine::hashState(pl, m_current.trajectoryHash);
                engine::step(pl);
                m_current.furthestX = std::max(m_current.furthestX, engine::playerX(pl));
                m_current.furthestPercent = std::max(m_current.furthestPercent, engine::percent(pl));
                if (st.diedThisStep) { finishRun(pl, m_current); return; }
                if (st.reachedEndThisStep) return;
            }
            break;
        }
        case Mode::Verifying: {
            double start = nowSeconds();
            while (nowSeconds() - start < s_budget) {
                engine::applyDueInputs(pl, m_inputs, m_cursor);
                engine::hashState(pl, m_current.trajectoryHash);
                if (auto p1 = pl->m_player1) {
                    uint32_t f = 0;
                    if (p1->m_isOnGround) f |= 1u << 0;
                    if (p1->m_isOnGround2) f |= 1u << 1;
                    if (p1->m_isDead) f |= 1u << 2;
                    if (st.holding[0]) f |= 1u << 3;
                    for (auto const& kv : p1->m_holdingButtons)
                        if (kv.first == 1 && kv.second) f |= 1u << 4;
                    if (p1->m_jumpBuffered) f |= 1u << 5;
                    if (p1->m_isLocked) f |= 1u << 6;
                    if (p1->m_controlsDisabled) f |= 1u << 7;
                    if (p1->m_isOnSlope) f |= 1u << 8;
                    if (p1->m_touchedRing) f |= 1u << 9;
                    if (p1->m_isDashing) f |= 1u << 10;
                    TrajPoint tp{p1->getPositionX(), p1->getPositionY(), p1->m_yVelocity, f, p1->m_lastJumpTime};
                    if (m_runResults.empty()) m_refTrajectory.push_back(tp);
                    else if (st.tick < (int) m_refTrajectory.size()) {
                        auto const& r = m_refTrajectory[st.tick];
                        if (r.x != tp.x || r.y != tp.y || r.vy != tp.vy || r.flags != tp.flags) {
                            if (m_trajMismatches++ < 3)
                                log::warn("[bot] run {} differs from run 1 at tick {}: ({:.4f},{:.4f} vy {:.5f} f {:#x}) vs ({:.4f},{:.4f} vy {:.5f} f {:#x})",
                                    m_runResults.size() + 1, st.tick, tp.x, tp.y, tp.vy, tp.flags, r.x, r.y, r.vy, r.flags);
                        } else if (r.lastJump != tp.lastJump && !m_reportedJumpClock) {
                            m_reportedJumpClock = true;
                            log::warn("[bot] run {} lastJumpTime differs from run 1 at tick {}: {:.5f} vs {:.5f}",
                                m_runResults.size() + 1, st.tick, tp.lastJump, r.lastJump);
                        }
                    }
                }
                engine::step(pl);
                m_current.furthestX = std::max(m_current.furthestX, engine::playerX(pl));
                m_current.furthestPercent = std::max(m_current.furthestPercent, engine::percent(pl));
                if (st.diedThisStep) {
                    m_current.died = true;
                    m_current.endTick = st.tick;
                    m_current.deathObjectID = st.deathObjectID;
                    finishRun(pl, m_current);
                    return;
                }
                if (st.reachedEndThisStep) {
                    m_current.completed = true;
                    m_current.endTick = st.tick;
                    finishRun(pl, m_current);
                    return;
                }
                if (st.tick > kVerifyMaxTicks) {
                    m_current.endTick = st.tick;
                    finishRun(pl, m_current);
                    return;
                }
            }
            break;
        }
        case Mode::Solving: {
            if (m_simPhase != SimPhase::Off) {
                simSlice(pl);
                break;
            }
            if (!m_solver && m_resumePending) {
                m_resumePending = false;
                resumeFromPartial(pl);
                if (m_mode != Mode::Solving) return;
            }
            // Resuming may have handed the search to the sim instead of a Solver
            if (m_simPhase != SimPhase::Off) break;
            if (!m_solver) { endSession(); return; }
            m_solver->runSlice(s_budget);
            if (m_solver->finished()) onSolveFinished(pl, m_solver->found());
            break;
        }
        case Mode::Experiment: {
            if (!m_experiment) { endSession(); return; }
            if (m_experiment->slice(pl, s_budget)) {
                log::info("[bot] experiment finished: {}", m_experiment->summary());
                endSession();
            }
            break;
        }
        default: break;
    }
}

void Bot::onFrame(float) {
    if (active() && PlayLayer::get() != m_layer) {
        log::warn("[bot] PlayLayer vanished, ending session");
        stopSim();
        m_layer = nullptr;
        m_mode = Mode::Idle;
        m_solver.reset();
        engine::setDynamicObjects({});
        engine::endTakeover(nullptr);
        if (onFinished) { auto cb = std::move(onFinished); onFinished = nullptr; cb(*this); }
    }
}

// ---- Hybrid search (see SimPhase in Bot.hpp) ----

namespace {
    /// Hand the sim a stretch only if it can run at least this far before an object it doesn't model
    constexpr float kMinSimRun = 300.f;
    /// ...and stop it this far before that object, so the engine has room to line up for it
    constexpr float kHorizonMargin = 60.f;
    /// Engine steps a copied state must match the sim for before the sim gets it
    constexpr int kCaptureCheckTicks = 12;
    constexpr float kCaptureTolerance = 0.05f;
    /// Checked sim paths are cut into segments this long, so the engine can back up one at a time
    constexpr int kSimChunkTicks = 240;
    /// When a sim path dies in GD, the engine takes over at least this long before the death
    constexpr int kSimBackoffTicks = 120;
    /// A round that keeps less than this counts as failed; after this many failures in a row the
    /// sim isn't used again this session
    constexpr int kMinSimGainTicks = 240;
    constexpr int kMaxSimFailures = 4;
    constexpr double kSimStallSeconds = 10.0;
}

void Bot::finishFound(PlayLayer* pl) {
    log::info("[hybrid] path found: {} sim rounds, {} ticks from the sim", m_simRounds, m_simTicks);
    m_playAfterVerify = true;
    m_runsRemaining = 1;
    if (m_leanPhase && m_realLevel) {
        m_leanPhase = false;
        switchToLevel(m_realLevel, Request::VerifyInputs);
        return;
    }
    pl->setVisible(true);
    m_mode = Mode::Verifying;
    m_runResults.clear();
    engine::restoreStart(pl);
}

void Bot::continueSearch(PlayLayer* pl) {
    if (m_hybrid && startSimRound(pl)) return;
    launchSegment(pl);
}

void Bot::stopSim() {
    m_simStop = true;
    if (m_simFuture.valid()) {
        try { m_simFuture.get(); } catch (...) {}
    }
    m_simFuture = {};
    m_simPhase = SimPhase::Off;
    m_round = SimRound{};
}

bool Bot::captureForSim(PlayLayer* pl, Solver::Candidate const& c, InputList const& prefix, Player& out) {
    auto& st = engine::state();
    engine::releaseAll(pl);
    if (!c.state || !engine::restoreSnapshot(pl, *c.state)) return false;

    // Best: the sim plays the whole path so far itself. Where it models the level that ends exactly where
    // GD is, with all of the sim's own state (snaps, coyote frames, ...) intact.
    {
        Level replay(m_levelString);
        auto inputs = hybrid::toSim(prefix);
        bool held = false;
        size_t next = 0;
        int target = c.boundaryTick + 1;
        while (replay.currentFrame() < target && !replay.latestState().dead) {
            int frame = replay.currentFrame() + 1;
            while (next < inputs.size() && inputs[next].frame <= frame) held = inputs[next++].down;
            replay.runFrame(held);
        }
        auto const& sp = replay.latestState();
        float dx = std::abs(engine::playerX(pl) - sp.pos.x);
        float dy = std::abs(engine::playerY(pl) - 90.f - sp.pos.y);
        if (!sp.dead && replay.currentFrame() == target && sp.button == st.holding[0]
            && dx <= kCaptureTolerance && dy <= kCaptureTolerance) {
            out = sp;
            // A snap from before the hand-over can't look its frame up any more (see Level::resume)
            if (out.snapData.playerFrame > 0)
                out.snapData.playerX = replay.getState(out.snapData.playerFrame).nextPlayer()->pos.x;
            return true;
        }
        log::info("[hybrid] sim replay to tick {} is off: GD ({:.3f},{:.3f}) sim ({:.3f},{:.3f}){}; copying GD's state instead",
            c.boundaryTick, engine::playerX(pl), engine::playerY(pl) - 90.f, sp.pos.x, sp.pos.y, sp.dead ? " dead" : "");
    }

    std::string why;
    auto captured = hybrid::capture(pl, *m_simCheck, st.tick, st.holding[0], why);
    bool ok = captured.has_value();
    if (!ok) {
        log::info("[hybrid] can't hand tick {} (x {:.0f}) to the sim: {}", c.boundaryTick, c.x, why);
    } else {
        // Run both a few steps with the button as it is: the copy only counts if the sim follows GD
        m_simCheck->resume(*captured);
        bool held = st.holding[0];
        for (int i = 0; i < kCaptureCheckTicks && ok; i++) {
            engine::step(pl);
            auto const& sp = m_simCheck->runFrame(held);
            float dx = std::abs(engine::playerX(pl) - sp.pos.x);
            float dy = std::abs(engine::playerY(pl) - 90.f - sp.pos.y);
            if (st.diedThisStep || sp.dead || dx > kCaptureTolerance || dy > kCaptureTolerance) {
                log::info("[hybrid] sim copy at tick {} drifts {} steps in: GD ({:.3f},{:.3f}){} sim ({:.3f},{:.3f}){} mode {}",
                    c.boundaryTick, i + 1, engine::playerX(pl), engine::playerY(pl) - 90.f, st.diedThisStep ? " dead" : "",
                    sp.pos.x, sp.pos.y, sp.dead ? " dead" : "", sp.modeString());
                ok = false;
            }
        }
    }
    engine::releaseAll(pl);
    engine::restoreSnapshot(pl, *c.state);
    if (ok) out = *captured;
    return ok;
}

bool Bot::startSimRound(PlayLayer* pl) {
    if (m_simFailures >= kMaxSimFailures) return false;
    double remaining = m_solveDeadline - nowSeconds();
    if (remaining < 20.0) return false;

    SimSearch search;
    search.levelString = m_levelString;
    SimRound round;
    if (m_segments.empty()) {
        std::string why;
        if (!hybrid::simStartSupported(pl, why)) {
            log::info("[hybrid] the sim can't start this level: {}", why);
            return false;
        }
        round.rootX = -INFINITY;
    } else {
        auto const& last = m_segments.back();
        auto const& c = last.candidates[last.next];
        if (c.x < m_simBlockedUntilX) return false;
        if (m_horizon.next(c.x) - c.x < kMinSimRun) return false;
        round.prefix = last.prefix;
        round.prefix.insert(round.prefix.end(), c.path.begin(), c.path.end());
        Player start;
        if (!captureForSim(pl, c, round.prefix, start)) return false;
        search.start = start;
        round.root = c.state;
        round.rootTick = c.boundaryTick;
        round.rootX = c.x;
    }
    float horizon = m_horizon.next(round.rootX);
    if (horizon - std::max(round.rootX, 0.f) < kMinSimRun) return false;
    search.targetX = horizon - kHorizonMargin;
    search.timeLimit = std::min<double>((double) Mod::get()->getSettingValue<int64_t>("sim-round-seconds"), remaining - 15.0);
    search.stallLimit = kSimStallSeconds;

    m_simRounds++;
    log::info("[hybrid] sim round {} from tick {} (x {:.0f}) to x {:.0f}{}", m_simRounds, round.rootTick,
        std::max(round.rootX, 0.f), search.targetX, std::isinf(horizon) ? " (level end)" : "");
    m_round = std::move(round);
    m_simStop = false;
    m_simProgress = progressPercent();
    m_simPhase = SimPhase::Searching;
    m_simFuture = std::async(std::launch::async, [this, search = std::move(search)] {
        return pathfindSim(search, m_simStop, [this](double p) {
            if (p > m_simProgress) m_simProgress = p;
        });
    });
    return true;
}

void Bot::simSlice(PlayLayer* pl) {
    auto& st = engine::state();
    auto& r = m_round;
    if (m_simPhase == SimPhase::Searching) {
        if (m_simFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        SimResult result;
        try {
            result = m_simFuture.get();
        } catch (std::exception const& e) {
            log::error("[hybrid] sim search failed: {}", e.what());
            m_simFuture = {};
            m_simPhase = SimPhase::Off;
            m_simFailures = kMaxSimFailures;
            launchSegment(pl);
            return;
        }
        m_simFuture = {};
        log::info("[hybrid] sim reached tick {} (x {:.0f}){}", result.endFrame - 1, result.endX,
            result.reachedEnd ? ", the level end" : result.reachedTarget ? ", its target" : "");
        if (result.endFrame - result.startFrame < 2) {
            m_simFailures++;
            m_simBlockedUntilX = std::max(m_simBlockedUntilX, result.endX + 30.f);
            m_simPhase = SimPhase::Off;
            launchSegment(pl);
            return;
        }
        r.inputs = hybrid::toEngine(result.inputs);
        r.endTick = result.endFrame - 1;
        r.endX = result.endX;
        r.reachedTarget = result.reachedTarget;
        r.cursor = 0;
        r.lastBoundary = r.rootTick;
        engine::releaseAll(pl);
        if (r.root) {
            engine::restoreSnapshot(pl, *r.root);
        } else {
            engine::restoreStart(pl);
            engine::setDynamicObjects(engine::collectSnapshotObjects(pl));
        }
        m_simPhase = SimPhase::Checking;
        return;
    }

    // Checking: play the sim's path in GD
    double start = nowSeconds();
    while (nowSeconds() - start < s_budget) {
        engine::applyDueInputs(pl, r.inputs, r.cursor);
        if (st.tick >= r.endTick) return finishSimCheck(pl, false, false);
        if (st.tick - r.lastBoundary >= kSimChunkTicks) {
            Solver::Candidate c;
            for (auto const& ev : r.inputs) {
                if (ev.tick > r.lastBoundary && ev.tick <= st.tick) c.path.push_back(ev);
            }
            c.boundaryTick = st.tick;
            c.state = engine::takeSnapshot(pl);
            c.x = engine::playerX(pl);
            c.y = engine::playerY(pl);
            if (c.state) {
                r.boundaries.push_back(std::move(c));
                r.lastBoundary = st.tick;
            }
        }
        engine::step(pl);
        if (st.diedThisStep) return finishSimCheck(pl, true, false);
        if (st.reachedEndThisStep) return finishSimCheck(pl, false, true);
    }
}

void Bot::finishSimCheck(PlayLayer* pl, bool died, bool ended) {
    auto& st = engine::state();
    auto& r = m_round;
    m_simPhase = SimPhase::Off;
    m_bestPercent = std::max(m_bestPercent, engine::percent(pl));

    if (ended) {
        m_simTicks += st.tick - r.rootTick;
        m_inputs = r.prefix;
        for (auto const& ev : r.inputs) {
            if (ev.tick <= st.tick) m_inputs.push_back(ev);
        }
        log::info("[hybrid] the sim's path reaches the end in GD (tick {})", st.tick);
        m_round = SimRound{};
        finishFound(pl);
        return;
    }

    if (died) {
        float deathX = engine::playerX(pl);
        log::info("[hybrid] the sim's path dies in GD at tick {} (x {:.0f}, object {}); the engine takes over",
            st.tick, deathX, st.deathObjectID);
        int keepUntil = st.tick - kSimBackoffTicks;
        while (!r.boundaries.empty() && r.boundaries.back().boundaryTick > keepUntil) r.boundaries.pop_back();
        m_simBlockedUntilX = std::max(m_simBlockedUntilX, deathX + 30.f);
    } else {
        // Alive where the sim stopped: keep all of it
        Solver::Candidate c;
        for (auto const& ev : r.inputs) {
            if (ev.tick > r.lastBoundary && ev.tick <= st.tick) c.path.push_back(ev);
        }
        c.boundaryTick = st.tick;
        c.state = engine::takeSnapshot(pl);
        c.x = engine::playerX(pl);
        c.y = engine::playerY(pl);
        if (c.state && c.boundaryTick > r.lastBoundary) r.boundaries.push_back(std::move(c));
        // Stalled before its target: don't hand the same spot straight back
        if (!r.reachedTarget) m_simBlockedUntilX = std::max(m_simBlockedUntilX, r.endX + 30.f);
    }

    int gained = r.boundaries.empty() ? 0 : r.boundaries.back().boundaryTick - r.rootTick;
    m_simTicks += gained;
    m_simFailures = gained < kMinSimGainTicks ? m_simFailures + 1 : 0;
    if (m_simFailures >= kMaxSimFailures)
        log::info("[hybrid] {} sim rounds in a row gained little; engine only from here", m_simFailures);

    InputList prefix = r.prefix;
    for (auto& c : r.boundaries) {
        SolvedSegment seg;
        seg.prefix = prefix;
        seg.rootTick = c.boundaryTick;
        prefix.insert(prefix.end(), c.path.begin(), c.path.end());
        if (c.boundaryTick > m_bestPrefixTick) {
            m_bestPrefixTick = c.boundaryTick;
            m_bestPrefix = prefix;
        }
        seg.candidates.push_back(std::move(c));
        m_segments.push_back(std::move(seg));
    }
    if (!r.boundaries.empty()) savePartial(pl);
    log::info("[hybrid] kept {} ticks of the sim's path ({} segments); engine from tick {}", gained,
        r.boundaries.size(), m_segments.empty() ? 0 : m_segments.back().rootTick);
    m_round = SimRound{};
    m_segmentScale = 1;
    resetVariants(0);
    launchSegment(pl);
}

}

bool pathfinderBotActive() {
    return gdpf::Bot::get().active();
}
