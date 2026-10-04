#include <Hazard.hpp>
#include <Player.hpp>
#include <cmath>

Hazard::Hazard(Vec2D size, std::unordered_map<int, std::string>&& fields) : Object(size, std::move(fields)) {
	// Hazards are processed last, after everything else
	prio = 2;

	// GameObject::customSetup moves some small pits' hitboxes sideways (m_customBoxOffset), which getBoxOffset
	// turns with the object's transform: rotation (clockwise degrees) and flips (GD flips with a negative scale)
	int objId = std::stoi(fields[1]);
	float ox = 0;
	if (objId == 243 || objId == 422 || objId == 447 || objId == 1720 || objId == 1726 || objId == 1729)
		ox = -5;
	else if (objId == 244 || objId == 1721)
		ox = 5;
	if (ox != 0) {
		float scale = stod_def(fields[32], 1);
		float sx = (stod_def(fields[4]) != 0 ? -1.f : 1.f) * scale * stod_def(fields[128], 1);
		float rad = -stod_def(fields[6]) * 3.14159265358979f / 180.f;
		pos.x += std::cos(rad) * sx * ox;
		pos.y += std::sin(rad) * sx * ox;
	}
}

bool Sawblade::touching(Player const& p) const {
	// Hitbox detection for circle

	Vec2D corners[4] = {
		Vec2D(p.getLeft(), p.getBottom()),
		Vec2D(p.getRight(), p.getBottom()),
		Vec2D(p.getRight(), p.getTop()),
		Vec2D(p.getLeft(), p.getTop())
	};

	float radius = size.x;

	for (auto& c : corners) {
		auto diff = c - pos;
		if (std::sqrt(diff.x * diff.x + diff.y * diff.y) <= radius)
			return true;
		c = diff;
	}

	// Check if sawblade is physically inside the hitbox
	if (std::signbit(corners[2].x) != std::signbit(corners[0].x) && std::signbit(corners[2].y) != std::signbit(corners[0].y)) {
		return true;
	}

	return false;
}

void Hazard::collide(Player& p) const {
	p.dead = true;
}
