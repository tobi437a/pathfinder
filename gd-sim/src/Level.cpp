#include <sstream>
#include <iomanip>
#include <Level.hpp>
#include <Orb.hpp>
#include <algorithm>

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

			if (ob->pos.x > length)
				length = ob->pos.x + 100;
		}
	}

	player.level = this;
	gameStates.push_back(player);
}

Player& Level::runFrame(bool pressed, float dt) {
	Player p = gameStates.back();

	// Can't play if you're dead
	if (p.dead)
		return gameStates.back();

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
			}
		}
	}

	if (clickOrbs.size() > 1) {
		auto firstTouched = [&](Orb const* orb) {
			int f = orb->Object::touching(p) ? p.frame : p.frame - 1;
			while (f - 1 > baseFrame && orb->Object::touching(getState(f - 1)))
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
	}

	// Blocks are processed in descending order
	for (int i = blocks.size() - 1; i >= 0; --i) {
		if (p.dead) break;
		auto& b = *blocks[i];
		if (b->touching(p)) {
			++numCollisions;
			b->collide(p);
		}
	}

	for (auto hp : hazards) {
		auto& h = *hp;
		if (p.dead) break;
		if (h->touching(p)) {
			++numCollisions;
			h->collide(p);
			
		}
	}

	if (!p.dead)
		p.postCollision();
	else
		p.vehicle.clamp(p); // GD keeps the player within the mode's bounds before object collisions kill it

	if (debug) {
		std::cout << "Frame " << gameStates.size() << std::fixed << std::setprecision(8)
				  << " X " << p.pos.x << " Y " << p.pos.y - 15 << " Vel " << p.velocity
				  << " Accel " << p.acceleration << " Rot " << p.rotation << " Coll " << numCollisions
				  << " Mode " << p.modeString()
 				  << std::endl;

		if (p.button != gameStates.back().button) {
			std::cout << "Input X " << p.pos.x << " Y " << p.pos.y - 15 << std::endl;
		}
	}

	gameStates.push_back(std::move(p));
	return gameStates.back();
}


void Level::rollback(int frame) {
	gameStates.resize(std::max(frame - baseFrame, 1));
}

void Level::resume(Player state) {
	state.level = this;
	baseFrame = state.frame - 1;
	gameStates.clear();
	gameStates.push_back(std::move(state));
}

int Level::currentFrame() const {
	return gameStates.size() + baseFrame;
}

Player const& Level::getState(int frame) const {
	int index = frame - 1 - baseFrame;
	if (index <= 0)
		return gameStates[0];
	if (index >= (int)gameStates.size())
		return gameStates.back();
	return gameStates[index];
}

Player& Level::latestState() {
	return gameStates.back();
}
