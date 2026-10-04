#include <sstream>
#include <iomanip>
#include <Level.hpp>
#include <Orb.hpp>
#include <algorithm>
#include <cstdlib>

// Otherwise gameStates copies every Player (instead of moving) whenever it grows
static_assert(std::is_nothrow_move_constructible_v<Player>);

void Level::initLevelSettings(std::string const& lvlSettings, Player& player) {
	std::unordered_map<std::string, std::string> obj;

	std::stringstream ss2(lvlSettings);
	std::string k, v;
	while (std::getline(ss2, k, ',')) {
		std::getline(ss2, v, ',');
		obj[k] = v;
	}

	// Helper to make a default value for nonexistant keys
	auto get_or = [&obj](std::string const& key, std::string const& def) {
		if (auto it = obj.find(key); it != obj.end())
			return it->second.c_str();
		return def.c_str();
	};

	player.speed = atoi(get_or("kA4", "0"));

	// Robtop stores 1x speed as 0 and slow speed as 1. Very silly
	if (player.speed == 0)
		player.speed = 1;
	else if (player.speed == 1)
		player.speed = 0;

	if ((player.small = atoi(get_or("kA3", "0"))))
		player.size = player.size * 0.6;

	player.upsideDown = atoi(get_or("kA11", "0"));
	player.vehicle = Vehicle::from(static_cast<VehicleType>(atoi(get_or("kA2", "0"))));

	player.floor = 0;
	player.ceiling = player.vehicle.bounds;
	player.dual = atoi(get_or("kA8", "0"));
}

Level::Level(std::string const& lvlString) {
	// Split by ';' to parse level string
	std::stringstream ss(lvlString);
	std::string objstr;
	bool first = true;

	// First player state
	auto player = Player();

	while (std::getline(ss, objstr, ';')) {
		// First entry is level settings object
		if (first) {
			initLevelSettings(objstr, player);
			// (two-player mode from the level's own settings, not a start position's)
			std::stringstream settings(objstr);
			std::string k, v;
			while (std::getline(settings, k, ',') && std::getline(settings, v, ','))
				if (k == "kA10")
					twoPlayer = atoi(v.c_str());
			first = false;
			continue;
		}

		std::unordered_map<int, std::string> obj;

		std::stringstream ss2(objstr);
		std::string k, v;
		while (std::getline(ss2, k, ',')) {
			std::getline(ss2, v, ',');
			if (atoi(k.c_str()) > 0)
				obj[atoi(k.c_str())] = v;
		}

		// PlayLayer::createObjectsFromSetupFinished puts the end portal 340 past the last object (any object), and
		// checkForEnd completes the level once player 1 reaches its spawn point, 300 before that
		if (obj.count(2))
			length = std::max(length, (float)stod_def(obj[2], 0) + 40);

		if (obj[1] == "31") {
			initLevelSettings(objstr, player);
			player.pos.x = stod_def(obj[2], 0);
			player.pos.y = stod_def(obj[3], 0);
		}

		if (auto ob_o = Object::create(std::move(obj))) {
			auto ob = ob_o.value();

			// Unique ID
			ob->id = objectCount++;

			// Sections are divided by x position in increments of 100
			size_t sectionPos = std::max(.0f, ob->pos.x / sectionSize);
			if (sectionPos >= sections.size())
				sections.resize(sectionPos + 1);
			sections[sectionPos].push_back(ob);

		}
	}

	player.level = this;
	Player p2 = player;
	p2.isP2 = true;
	if (player.dual) {
		// A level that starts in dual mode: player 2 as at a dual portal (DualPortal::collide), with the dual
		// ground centred on GD's y 0 (no portal), which puts its floor on the ground
		p2.flipGravity(!player.upsideDown);
		player.dualPortalY = p2.dualPortalY = -90;
		player.partner = &p2;
		p2.partner = &player;
		player.updateGround(0);
		player.partner = p2.partner = nullptr;
	}
	gameStates.push_back(player);
	gameStates2.push_back(p2);
	if (player.dual) {
		// ...which GD steps from the first step on: off the ground, with a step of gravity before its first move
		Player& q = gameStates2.back();
		q.grounded = false;
		q.startDual();
	}
}

Player& Level::runFrame(bool pressed, float dt) {
	Player p = gameStates.back();

	// Can't play if you're dead
	if (p.dead)
		return gameStates.back();

	// GJBaseGameLayer::update steps player 1 (update, collisions), then player 2 if dual mode was on before
	// player 1's step; both get the button. Each sees the other as `partner` (the dual ground, portals).
	Player q = gameStates2.back();
	bool wasDual = p.dual;
	int speed1 = p.speed, speed2 = q.speed;
	p.partner = &q;
	q.partner = &p;
	for (Player* pl : {&p, &q}) {
		if (pl->flipPartnerNext) {
			pl->flipPartnerNext = false;
			pl->flipPartner();
		}
	}
	int collisions = stepPlayer(p, pressed, dt);
	if (wasDual && p.dual && !p.dead) {
		// (not once player 1 died: GD stops the step there)
		stepPlayer(q, twoPlayer ? q.button : pressed, dt);
		if (q.dead)
			p.dead = true;
		// Player 2 left dual mode through a solo portal: player 1 took its place (DualPortal::collide) with
		// player 2's velocity as GD's collisions saw it, which GD's next step adds gravity to (like a new player 2)
		if (p.copiedFromP2) {
			p.copiedFromP2 = false;
			p.startDual();
		}
	} else if (p.dual) {
		q.startDual();
	}
	if (p.dual) {
		// A speed portal sets GD's speed for the next step, which updateTimeMod gives both players at its start
		int speed = wasDual && q.speed != speed2 ? q.speed : p.speed != speed1 ? p.speed : -1;
		for (Player* pl : {&p, &q}) {
			if (speed >= 0 && pl->speed != speed) {
				pl->speed = speed;
				if (pl->vehicle.type == VehicleType::Ball) {
					pl->stopRotation();
					pl->runBallRotation();
				}
			}
		}
	}
	if (!p.dual) {
		// Player 2 idles; keep a copy of player 1 so its lane has states for every frame
		q.copyAttributes(p);
	}
	p.partner = q.partner = nullptr;

	if (debug) {
		std::cout << "Frame " << gameStates.size() << std::fixed << std::setprecision(8)
				  << " X " << p.pos.x << " Y " << p.pos.y - 15 << " Vel " << p.velocity
				  << " Accel " << p.acceleration << " Rot " << p.rotation << " Coll " << collisions
				  << " Mode " << p.modeString()
 				  << std::endl;
		if (p.dual)
			std::cout << "P2 " << gameStates.size() << " X " << q.pos.x << " Y " << q.pos.y - 15 << " Vel " << q.velocity
					  << " Rot " << q.rotation << " Mode " << q.modeString() << " floor " << p.floor << " ceiling " << p.ceiling
					  << std::endl;

		// `Flags <frame> ...`: internal state for debugging (set PF_FLAGS=1)
		static bool const flags = std::getenv("PF_FLAGS") != nullptr;
		if (flags) {
			for (Player const* pl : {&p, &q}) {
				if (pl == &q && !p.dual)
					break;
				std::cout << (pl == &p ? "Flags " : "Flags2 ") << gameStates.size() << " og " << pl->grounded
						  << " og2 " << pl->onGround2 << " rotating " << pl->isRotating << " spin " << pl->rotationSpeed
						  << " slope " << pl->slope.on << pl->slope.was << " boosted " << pl->boosted
						  << " acc " << pl->accelerating << " btn " << pl->button << std::endl;
			}
		}

		if (p.button != gameStates.back().button) {
			std::cout << "Input X " << p.pos.x << " Y " << p.pos.y - 15 << std::endl;
		}
	}

	gameStates.push_back(std::move(p));
	gameStates2.push_back(std::move(q));
	return gameStates.back();
}

/// `SimDeath <frame> <player> at <x> <y> size <w> <h> rot <r>`: the object that killed the player (set PF_FLAGS=1)
static void logDeath(Player const& p, Object const& o) {
	static bool const on = std::getenv("PF_FLAGS") != nullptr;
	if (on)
		std::cout << "SimDeath " << p.frame - 1 << " " << (p.isP2 ? 2 : 1) << " at " << o.pos.x << " " << o.pos.y
				  << " size " << o.size.x << " " << o.size.y << " rot " << -o.rotation << std::endl;
}

int Level::stepPlayer(Player& p, bool pressed, float dt) {
	p.dt = dt;
	p.preCollision(pressed);

	// Objects from previous, current, and next section are all collision tested
	size_t sectionIdx = std::min(std::max(0, (int)(p.pos.x / sectionSize)), (int)sections.size() - 1);
	auto prevSection = &sections[sectionIdx == 0 ? 0 : sectionIdx - 1];
	auto currSection = &sections[sectionIdx];
	auto nextSection = &sections[sectionIdx + 1 >= sections.size() - 1 ? sections.size() - 1 : sectionIdx + 1];

	// If at start or end of level, previous/next section is invalid so don't use it
	std::vector<ObjectContainer>* sections[3] = { prevSection, nullptr, nullptr };
	if (&currSection != &prevSection)
		sections[1] = currSection;
	if (&nextSection != &currSection)
		sections[2] = nextSection;

	// Blocks are hazards processed separately
	blocks.clear();
	hazards.clear();

	size_t numCollisions = 0;

	// A click is handled by PlayerObject::pushButton, after this step's collisions, which tries the rings in
	// m_touchingRings (touched this step or the last) in the order they were first touched: a ring stays
	// in it while the player keeps touching it (resetTouchedRings).
	bool clicked = p.button && !p.prevPlayer().button;
	std::vector<Orb const*> clickOrbs;

	for (auto section : sections) {
		if (section == nullptr) continue;
		for (auto& o : *section) {
			if (p.dead) break;
			if (o->prio == 1)
				blocks.push_back(&o);
			else if (o->prio == 2)
				hazards.push_back(&o);
			else if (o->touching(p)) {
				++numCollisions;
				if (auto orb = clicked ? dynamic_cast<Orb const*>(o.operator->()) : nullptr)
					clickOrbs.push_back(orb);
				else
					o->collide(p);
				if (p.dead)
					logDeath(p, *o.operator->());
			}
		}
	}

	if (clickOrbs.size() > 1) {
		auto firstTouched = [&](Orb const* orb) {
			int f = orb->Object::touching(p) ? p.frame : p.frame - 1;
			while (f - 1 > baseFrame && orb->Object::touching(getState(f - 1, p.isP2)))
				--f;
			return f;
		};
		std::vector<std::pair<int, Orb const*>> order;
		for (auto orb : clickOrbs)
			order.push_back({firstTouched(orb), orb});
		std::stable_sort(order.begin(), order.end(), [](auto& a, auto& b) { return a.first < b.first; });
		for (size_t i = 0; i < order.size(); ++i)
			clickOrbs[i] = order[i].second;
	}
	for (auto orb : clickOrbs) {
		if (p.dead) break;
		if (orb->touching(p))
			orb->collide(p);
		if (p.dead)
			logDeath(p, *orb);
	}

	if (p.scaleRefreshed)
		p.applyResize();

	// Blocks are processed in descending order
	for (int i = blocks.size() - 1; i >= 0; --i) {
		if (p.dead) break;
		auto& b = *blocks[i];
		if (b->touching(p)) {
			++numCollisions;
			b->collide(p);
			if (p.dead)
				logDeath(p, *b.operator->());
		}
	}

	for (auto hp : hazards) {
		auto& h = *hp;
		if (p.dead) break;
		if (h->touching(p)) {
			++numCollisions;
			h->collide(p);
			if (p.dead)
				logDeath(p, *h.operator->());
		}
	}

	if (!p.dead)
		p.postCollision();
	else
		p.vehicle.clamp(p); // GD keeps the player within the mode's bounds before object collisions kill it

	return numCollisions;
}


void Level::rollback(int frame) {
	gameStates.resize(std::max(frame - baseFrame, 1));
	gameStates2.resize(gameStates.size());
}

void Level::resume(Player state) {
	state.level = this;
	baseFrame = state.frame - 1;
	gameStates.clear();
	gameStates2.clear();
	gameStates.push_back(state);
	state.isP2 = true;
	gameStates2.push_back(std::move(state));
}

int Level::currentFrame() const {
	return gameStates.size() + baseFrame;
}

Player const& Level::getState(int frame, bool p2) const {
	auto const& states = p2 ? gameStates2 : gameStates;
	int index = frame - 1 - baseFrame;
	if (index <= 0)
		return states[0];
	if (index >= (int)states.size())
		return states.back();
	return states[index];
}

Player& Level::latestState() {
	return gameStates.back();
}
