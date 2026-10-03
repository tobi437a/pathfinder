#include "Hybrid.hpp"

#include <Geode/binding/EffectGameObject.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelSettingsObject.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <algorithm>
#include <cfloat>
#include <climits>
#include <unordered_set>

#include "Engine.hpp"

// Windows headers define `small`, which Player uses as a member
#undef small

using namespace geode::prelude;

namespace gdpf::hybrid {

namespace {
    /// Triggers that only change how the level looks or sounds, never where the player can go
    std::unordered_set<int> const kVisualTriggers = {
        22, 23, 24, 25, 26, 27, 28, 55, 56, 57, 58, 59, // enter effects (the sim handles their sprite shifts)
        29, 30, 104, 105, 221, 717, 718, 743, 744, 899, 900, 915, // colours
        31, // start position
        32, 33, // enable / disable trail
        1006, 1007, 1520, 1585, 1612, 1613, 1818, 1819, // pulse, alpha, shake, animate, hide/show player, bg effect
        1913, 1914, 1916, 2015, 2016, 2062, 2901, 2925, // camera
        1934, 3602, 3603, 3605, // song and sfx
        2903, 2904, 2905, 2907, 2909, 2910, 2911, 2912, 2913, 2914, 2915, 2916, 2917, 2919, 2920, 2921, 2922,
        2923, 2924, // gradient and shaders
        3606, 3612, // background and middleground speed
    };

    bool ignorableType(GameObjectType type) {
        switch (type) {
            case GameObjectType::Decoration:
            case GameObjectType::SecretCoin:
            case GameObjectType::UserCoin:
            case GameObjectType::EnterEffectObject:
            case GameObjectType::Special:
            case GameObjectType::CollisionObject:
                return true;
            default:
                return false;
        }
    }

    /// Why the sim would get this object wrong, or nullptr if it models it
    char const* unsupported(GameObject* o) {
        int id = o->m_objectID;
        if (Object::supported(id)) {
            if (o->m_isPassable) return "passable";
            if (o->m_isNoTouch) return "no touch";
            if (o->m_isNonStickX || o->m_isNonStickY || o->m_isIceBlock || o->m_isGripSlope) return "2.2 block flags";
            if (o->m_hasExtendedCollision) return "extended collision";
            return nullptr;
        }
        auto type = o->m_objectType;
        if (type == GameObjectType::Modifier)
            return kVisualTriggers.contains(id) ? nullptr : "trigger";
        if (ignorableType(type))
            return nullptr;
        return "object";
    }

    int speedIndex(float speed) {
        static constexpr float kSpeeds[] = {0.7f, 0.9f, 1.1f, 1.3f, 1.6f};
        for (int i = 0; i < 5; i++) {
            if (std::abs(speed - kSpeeds[i]) < 0.001f) return i;
        }
        return -1;
    }

    /// Sim coordinates are the level string's; GD's y is 90 higher (its ground is at 90)
    constexpr float kYOffset = 90.f;

    int64_t posKey(float x, float y) {
        return ((int64_t) std::lround(x * 4.f) << 32) ^ (int64_t) (uint32_t) std::lround(y * 4.f);
    }
}

float Horizon::next(float x) const {
    auto it = std::lower_bound(xs.begin(), xs.end(), x);
    return it == xs.end() ? INFINITY : *it;
}

Horizon scanLevel(PlayLayer* pl) {
    Horizon h;
    if (!pl || !pl->m_objects) {
        h.usable = false;
        h.reason = "no level";
        return h;
    }
    if (pl->m_levelSettings && pl->m_levelSettings->m_platformerMode) {
        h.usable = false;
        h.reason = "platformer";
        return h;
    }
    std::unordered_map<std::string, int> counts;
    for (auto o : CCArrayExt<GameObject*>(pl->m_objects)) {
        if (!o) continue;
        if (auto why = unsupported(o)) {
            h.xs.push_back(o->getRealPosition().x);
            counts[fmt::format("{} {}", why, o->m_objectID)]++;
        }
    }
    std::sort(h.xs.begin(), h.xs.end());
    std::string summary;
    for (auto const& [what, n] : counts) summary += fmt::format(" {}x{}", what, n);
    log::info("[hybrid] {} objects the sim doesn't model, first at x {:.0f}:{}", h.xs.size(),
        h.xs.empty() ? INFINITY : h.xs.front(), summary.empty() ? " none" : summary);
    return h;
}

bool simStartSupported(PlayLayer* pl, std::string& why) {
    auto s = pl ? pl->m_levelSettings : nullptr;
    if (!s) { why = "no level settings"; return false; }
    if (s->m_platformerMode) { why = "platformer"; return false; }
    if (s->m_startDual) { why = "starts in dual mode"; return false; }
    if (s->m_startMode > 4) { why = "starts as robot, spider or swing"; return false; }
    if (s->m_startsWithStartPos) { why = "starts at a start position"; return false; }
    return true;
}

std::optional<Player> capture(PlayLayer* pl, Level& lvl, int tick, bool button, std::string& why) {
    auto p = pl ? pl->m_player1 : nullptr;
    if (!p) { why = "no player"; return std::nullopt; }
    auto& gs = pl->m_gameState;
    if (gs.m_isDualMode) { why = "dual"; return std::nullopt; }
    if (p->m_isRobot || p->m_isSpider || p->m_isSwing) { why = "robot, spider or swing"; return std::nullopt; }
    if (p->m_isDead || p->m_isLocked || p->m_controlsDisabled) { why = "player dead or locked"; return std::nullopt; }
    if (p->m_isOnSlope) { why = "on a slope"; return std::nullopt; }
    if (p->m_isDashing) { why = "dashing"; return std::nullopt; }
    if (engine::playerReversed(p)) { why = "reversed"; return std::nullopt; }
    if (p->m_gravityMod != 1.f) { why = "gravity changed by a trigger"; return std::nullopt; }
    if (gs.m_timeWarp != 1.f) { why = "time warp"; return std::nullopt; }
    if (gs.m_levelFlipping != 0.f && gs.m_levelFlipping != 1.f) { why = "mid mirror flip"; return std::nullopt; }
    int speed = speedIndex(p->m_playerSpeed);
    if (speed < 0) { why = fmt::format("unknown speed {}", p->m_playerSpeed); return std::nullopt; }
    bool small = p->m_vehicleSize != 1.f;
    if (small && std::abs(p->m_vehicleSize - 0.6f) > 0.001f) { why = "odd size"; return std::nullopt; }

    VehicleType type = p->m_isShip ? VehicleType::Ship : p->m_isBall ? VehicleType::Ball
        : p->m_isBird ? VehicleType::Ufo : p->m_isDart ? VehicleType::Wave : VehicleType::Cube;

    Player s;
    s.level = &lvl;
    s.vehicle = Vehicle::from(type);
    s.upsideDown = p->m_isUpsideDown;
    s.small = small;
    s.speed = speed;
    s.pos = {p->getPositionX(), p->getPositionY() - kYOffset};
    if (type == VehicleType::Wave)
        s.size = small ? Vec2D(6, 6) : Vec2D(10, 10);
    else
        s.size = Vec2D(30, 30) * (small ? 0.6 : 1);
    s.rotation = p->getRotation();
    s.velocity = p->m_yVelocity * 54.0 * (s.upsideDown ? -1 : 1);
    s.acceleration = 0;
    s.grounded = p->m_isOnGround;
    s.coyoteFrames = INT_MAX;
    s.button = s.input = button;
    s.buffer = button && p->m_stateRingJump;
    s.vehicleBuffer = false;
    s.frame = tick + 1;
    s.dt = engine::kStepDt;
    s.timeElapsed = tick * (double) engine::kStepDt;

    if (type == VehicleType::Cube) {
        s.floor = 0;
        s.ceiling = s.vehicle.bounds;
    } else {
        s.floor = pl->getMinPortalY() - kYOffset;
        s.ceiling = pl->getMaxPortalY() - kYOffset;
    }

    s.flip.mirrored = gs.m_levelFlipping == 1.f;
    s.flip.from = s.flip.to = s.flip.value = gs.m_levelFlipping;
    s.flip.start = 0;
    // After a completed flip to mirrored GD measures sprite shifts from the mirrored side (see Player::flip)
    s.flip.fromMirrored = *reinterpret_cast<float*>(reinterpret_cast<char*>(pl) + 0x3204) == -1.f;

    // The last X-snap block (PlayerObject 0x888) and the player's x - its x back then (0x838)
    if (type == VehicleType::Cube) {
        if (auto snap = *reinterpret_cast<GameObject**>(reinterpret_cast<char*>(p) + 0x888); snap && tick > 0) {
            auto real = snap->getRealPosition();
            double offset = *reinterpret_cast<double*>(reinterpret_cast<char*>(p) + 0x838);
            s.snapData.object = Entity{{real.x, real.y - kYOffset}, {30, 30}, 0};
            s.snapData.playerFrame = tick;
            s.snapData.playerX = (float) (snap->getPositionX() + offset);
            s.snapData.spriteDx = 0;
        }
    }

    // Orbs, pads, portals and breakable blocks GD has used up: same object id isn't stored in the sim, so
    // match them by position
    std::unordered_map<int64_t, bool> used;
    for (auto o : CCArrayExt<GameObject*>(pl->m_objects)) {
        if (!o || !Object::supported(o->m_objectID)) continue;
        auto e = typeinfo_cast<EffectGameObject*>(o);
        bool isUsed = (e && e->m_activatedByPlayer1) || o->m_isDisabled || o->m_isDisabled2;
        auto real = o->getRealPosition();
        auto& slot = used[posKey(real.x, real.y - kYOffset)];
        slot = slot || isUsed;
    }
    for (auto& section : lvl.sections) {
        for (auto& o : section) {
            if (auto it = used.find(posKey(o->pos.x, o->pos.y)); it != used.end() && it->second)
                s.usedEffects.insert(o->id);
        }
    }
    return s;
}

InputList toEngine(std::vector<SimInput> const& inputs) {
    InputList out;
    out.reserve(inputs.size());
    for (auto const& i : inputs) out.push_back({i.frame - 1, 1, i.down, false});
    return out;
}

std::vector<SimInput> toSim(InputList const& inputs) {
    std::vector<SimInput> out;
    for (auto const& i : inputs) {
        if (i.button == 1 && !i.player2) out.push_back({i.tick + 1, i.down});
    }
    return out;
}

Result<std::filesystem::path> saveMacro(InputList const& inputs, std::string const& levelName) {
    auto dir = Mod::get()->getSaveDir() / "macros";
    if (auto eclipse = Loader::get()->getLoadedMod("eclipse.eclipse-menu"))
        dir = eclipse->getSaveDir() / "replays";
    std::string name = levelName;
    for (auto& c : name) {
        if (std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos || (unsigned char) c < 32) c = '_';
    }
    auto path = dir / fmt::format("{} - pathfinder.gdr2", name);
    GEODE_UNWRAP(file::createDirectoryAll(dir));
    GEODE_UNWRAP(file::writeBinary(path, exportMacro(toSim(inputs))));
    return Ok(path);
}

}
