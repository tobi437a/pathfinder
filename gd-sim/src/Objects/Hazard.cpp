#include <Hazard.hpp>
#include <Player.hpp>
#include <Slope.hpp>
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
	// GJBaseGameLayer::playerCircleCollision (a level without m_fixRadiusCollision, as 1.8 ones are): the centre in
	// the player's rect (edges included), or a corner of that rect closer than the radius; floats in GD's coordinates
	GDRect r = GDRect::of(p);
	float cx = pos.x, cy = pos.y + 90.f;
	if (r.minX() <= cx && cx <= r.maxX() && r.minY() <= cy && cy <= r.maxY())
		return true;
	float radius = size.x;
	for (float x : {r.maxX(), r.minX()}) {
		for (float y : {r.maxY(), r.minY()}) {
			float dx = x - cx, dy = y - cy;
			if (std::sqrt(dx * dx + dy * dy) < radius)
				return true;
		}
	}
	return false;
}

void Hazard::collide(Player& p) const {
	p.dead = true;
}
