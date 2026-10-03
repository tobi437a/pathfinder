#include <Slope.hpp>
#include <Player.hpp>
#include <Level.hpp>
#include <cmath>
#include <algorithm>

/*
	Ported from GD 2.2081: PlayerObject::preSlopeCollision, collidedWithSlopeInternal (and
	GameObject::determineSlopeDirection, slopeYPos). Leaving a slope (the launch off its top) is in
	Player::leaveSlope, the jump boost in Player::slopeJump. GD works in its own coordinates (sim y + 90)
	with float rects; so does this, so it rounds the same way. Not modelled: moving slopes, platformer,
	sideways (rotated gameplay), a player moving left, dart slide (2.2).
*/

GDRect GDRect::of(Entity const& e, float w, float h) {
	float sw = e.size.x * w, sh = e.size.y * h;
	return {e.pos.x - sw * 0.5f, (e.pos.y + 90.f) - sh * 0.5f, sw, sh};
}

Slope::Slope(Vec2D size, std::unordered_map<int, std::string>&& fields) : Block(size, std::move(fields)) {
	prio = 0;

	// GameObject::determineSlopeDirection, from GD's rotation (clockwise degrees) and flips
	bool flipX = atoi(fields[4].c_str()) == 1;
	bool flipY = atoi(fields[5].c_str()) == 1;
	float rot = (float)((int)stod_def(fields[6].c_str()) % 360);
	bool half = std::abs(rot) == 180;
	bool cw = rot == 90 || rot == -270;
	bool ccw = rot == -90 || rot == 270;

	auto set = [&](int dir) {
		direction = dir;
		uphill = dir == 0 || dir == 3 || dir == 6 || dir == 7;
	};
	if (!flipX && !flipY) {
		if (rot == 0) set(0);
		else if (half) set(3);
		else if (cw) set(4);
		else if (ccw) set(5);
	} else if (!flipX) {
		if (rot == 0) set(1);
		else if (half) set(2);
		else if (ccw) set(6);
		else if (cw) set(7);
	} else if (!flipY) {
		if (half) set(1);
		else if (rot == 0) set(2);
		else if (cw) set(6);
		else if (ccw) set(7);
	} else {
		if (half) set(0);
		else if (rot == 0) set(3);
		else if (ccw) set(4);
		else if (cw) set(5);
	}
}

bool Slope::ceiling() const {
	return direction == 1 || direction == 3 || direction == 5 || direction == 6;
}

bool Slope::wallLeft() const {
	return direction == 2 || direction == 3 || direction == 4 || direction == 6;
}

double Slope::surfaceY(float x) const {
	GDRect r = GDRect::of(*this);
	double ratio = r.h / r.w;
	double y;
	if (r.minX() < x) {
		double d = ((double)x - r.maxX()) * ratio;
		y = uphill ? d + r.maxY() : r.minY() - d;
	} else {
		double d = ((double)r.minX() - x) * ratio;
		y = uphill ? r.minY() - d : d + r.maxY();
	}
	if (hazard)
		y += ceiling() ? -4 : 4;
	return y;
}

bool Slope::touching(Player const& p) const {
	return GDRect::of(*this, 2, 2).intersects(GDRect::of(p));
}

namespace {

bool isFly(Player const& p) {
	auto t = p.vehicle.type;
	return t == VehicleType::Ship || t == VehicleType::Ufo || t == VehicleType::Wave;
}

/// GD's m_jumpBuffered during this frame's collisions: the button as of the last frame (GD handles a
/// press a step earlier than the sim, see Player::postCollision); a ball's jump uses the click up
bool isHeld(Player const& p) {
	auto const& prev = p.prevPlayer();
	return p.vehicle.type == VehicleType::Ball ? prev.input : prev.button;
}

/// Within 0.1 s of a gravity flip or a mode change (GD's m_lastFlipTime, m_gameModeChangedTime)
bool recentlyChanged(Player const& p) {
	Player const* a = &p;
	for (int i = 1; i < 24; ++i) {
		auto const& b = p.level->getState(p.frame - i);
		if (a->upsideDown != b.upsideDown || a->vehicle.type != b.vehicle.type)
			return true;
		a = &b;
	}
	return false;
}

void addPotential(Player& p, Slope const* s) {
	if (std::find(p.potentialSlopes.begin(), p.potentialSlopes.end(), s) == p.potentialSlopes.end())
		p.potentialSlopes.push_back(s);
}

}

bool Slope::preCollide(Player& p, GDRect const& S, GDRect const& P) const {
	if (id == p.slope.collidingId)
		return false;

	// A side counts as a block (collidedWithObjectInternal with that rect; no X-snap off it)
	auto side = [&](GDRect const& r) {
		if (!P.intersects(r))
			return;
		Block b = *this;
		b.pos = {r.x + r.w / 2, (r.y + r.h / 2) - 90.f};
		b.size = {r.w, r.h};
		b.collideBlock(p, true);
	};

	float px = p.pos.x, py = p.pos.y + 90.f;
	bool tried = false;
	// Left of a slope whose vertical side is on the left: that side, one unit wide. (GD only checks the
	// right side for a player moving left.)
	if (wallLeft() && px < S.minX()) {
		tried = true;
		side({S.x, S.y, 1, S.h});
	}
	// Centre beyond the flat side (within one unit of it): that edge, one unit thick
	GDRect flat = ceiling() ? GDRect{S.x, (S.y + S.h) - 1, S.w, 1} : GDRect{S.x, S.y, S.w, 1};
	if (ceiling() ? flat.h + flat.y <= py : py <= flat.h + flat.y) {
		tried = true;
		side(flat);
	}

	if (!tried) {
		p.slope.keepPotential = true;
		if (!p.slope.on) {
			addPotential(p, this);
			auto risingFloor = [](Slope const* s) { return s->uphill && !s->ceiling(); };
			auto cur = p.slope.potential;
			if (!cur || (!risingFloor(cur) && !risingFloor(this) && pos.x < cur->pos.x) || (risingFloor(cur) && cur->pos.x < pos.x))
				p.slope.potential = this;
		}
	}
	return tried || !S.intersects(P);
}

void Slope::collide(Player& p) const {
	GDRect S = GDRect::of(*this), P = GDRect::of(p);
	if (preCollide(p, S, P))
		return;

	auto& st = p.slope;
	bool up = p.upsideDown;
	int sign = up ? -1 : 1;
	auto type = p.vehicle.type;
	bool fly = isFly(p), ball = type == VehicleType::Ball, ufo = type == VehicleType::Ufo;
	bool held = isHeld(p);
	bool isCeil = ceiling();
	// Falling to the right, relative to gravity
	bool down = !uphill != up;

	// Coming off a slope that launches the player, a downhill slope of the same kind doesn't catch it;
	// nor does a less steep one
	if (st.was && 0 < sign * st.velocity && down && st.ceiling == isCeil && st.ceiling == up)
		return;
	float yVel = S.h / (float)((double)S.w / (player_speeds[p.speed] / 60));
	if (st.was && 0 < sign * st.velocity && (double)yVel < st.yVelocity)
		return;

	// How far above a downhill slope the player still gets pulled onto it
	float tol = down ? (st.was ? 4 : 1) : 0;

	float px = p.pos.x, py = p.pos.y + 90.f;
	float half = P.h * 0.5f / cosf((float)st.rotation);
	float edge = !up ? (py - half) - tol : half + py + tol;
	if (!st.was) {
		if (!GDRect{S.x, S.y + 1.f, S.w, S.h - 2.f}.intersects(P))
			return;
	} else if (!up) {
		if (S.y + S.h < edge)
			return;
	} else if (edge < S.y) {
		return;
	}

	float angle = atanf(S.h / S.w);
	// Half the player's height measured vertically through the slope
	float halfV = (P.h / cosf(angle)) * 0.5f;
	// Going from a floor slope to a ceiling slope (or back)
	bool switching = st.was && st.collidingId != id && st.ceiling != isCeil;
	float lift = switching ? (p.small ? 0.6f : 1.f) * 20.f : 0;
	float sy = (float)surfaceY(px);

	// Where the player's centre goes: on the surface, but within the slope's height
	float target;
	bool topClamp, bottomClamp;
	if (!isCeil) {
		float high = (P.h * 0.5f + S.maxY()) - lift;
		float on = (sy + halfV) - lift;
		float t = high <= on ? high : on;
		topClamp = high <= on;
		target = t <= S.minY() ? S.minY() : t;
		bottomClamp = t <= S.minY();
	} else {
		float low = (S.minY() - P.h * 0.5f) + lift;
		float on = (sy - halfV) + lift;
		target = on <= low ? low : on;
		topClamp = on <= low;
		bottomClamp = S.maxY() <= target;
		if (bottomClamp)
			target = S.maxY();
	}

	bool head = false, clampDown = false, raise = false, noLand = false, hit;
	if (isCeil == up) {
		// The slope is under the player (relative to gravity)
		bool snapDown = false;
		if (!switching && down && !st.on) {
			if (!p.boosted)
				snapDown = !(type == VehicleType::Ship && held);
			else if (type == VehicleType::Ship)
				snapDown = !held;
			else
				snapDown = ufo || type == VehicleType::Wave;
		}
		if (!up)
			hit = py < target || (snapDown && py < target + tol && !(ufo && 0 < p.gdVelocity()));
		else
			hit = target < py || (snapDown && target - tol < py && !(ufo && p.gdVelocity() < 0));
		// A flying player holding on an uphill slope rides it (at least 2 up) without landing
		raise = noLand = held && fly && !down;
	} else {
		// The slope is over the player's head
		head = true;
		if (!down)
			tol = fly && held ? (st.was ? 2 : 1) : tol;
		else
			tol = 0;
		bool loose = !(switching || (fly && down) || st.on);
		if (!up)
			hit = target < py || (loose && target - tol < py);
		else
			hit = py < target || (loose && py < target + tol);
		noLand = !held && (fly || ball) && down && st.was;
		clampDown = !held && fly && down;
	}
	if (!hit)
		return;

	if (head && !fly && !ball) {
		// A cube (robot, spider) under a slope
		if (recentlyChanged(p)) {
			p.pos.y = target - 90.f;
			double v = p.gdVelocity();
			p.setGdVelocity(!up ? std::min(v, -2.0) : std::max(v, 2.0));
			p.grounded = false;
			return;
		}
		float near = !up ? py - 2.f : py + 2.f;
		if (!up ? near <= target : target <= near)
			p.grounded = false;
		else
			p.dead = true;
		return;
	}
	if (hazard || switching || type == VehicleType::Wave) {
		p.dead = true;
		return;
	}
	// Put at the same height as last frame (at the slope's end): the slope lets go
	if (st.was && st.target == (double)target)
		return;

	st.current = this;
	st.potential = this;
	addPotential(p, this);
	st.angle = angle;
	st.clipExtra = (double)(halfV - P.h * 0.5f);
	st.ceiling = isCeil;
	st.target = (double)target;
	st.collidingId = id;
	st.downhill = down;
	st.topClamped = topClamp;
	st.bottomClamped = bottomClamp;
	st.rotation = (double)((float)(sign * (down ? 1 : -1)) * angle * 57.29578f * 0.017453292f);

	p.pos.y = target - 90.f;
	p.applyResize();
	st.on = true;
	if (!st.was)
		st.startTime = p.timeElapsed;
	st.head = head;

	if (!noLand) {
		if (!head) {
			// hitGround: stops the player, grounded unless it came in rising faster than 5; it keeps rising
			// faster than 5
			double before = p.gdVelocity();
			float v0 = (float)before;
			p.velocity = 0;
			if (sign * before <= 5)
				p.grounded = true;
			if (!up ? 5 < v0 : v0 < -5)
				p.setGdVelocity(v0);
		} else {
			double v = p.gdVelocity();
			p.setGdVelocity(!up ? (0 <= v ? 0 : v) : (v <= 0 ? 0 : v));
			p.grounded = false;
		}
	}
	if (clampDown) {
		double v = p.gdVelocity();
		if (!up ? -2 < v : v < 2)
			p.setGdVelocity(!up ? -2 : 2);
	}
	if (raise) {
		double v = p.gdVelocity();
		if (!up ? v < 2 : -2 < v)
			p.setGdVelocity(!up ? 2 : -2);
	}

	// The launch speed off its top (see Player::leaveSlope)
	st.yVelocity = (double)yVel;
	float k = 0.8f / angle <= 1.1f ? 0.8f / angle : 1.1f;
	float v = k * yVel * 1.4f;
	if (down)
		v = v * -1.f;
	st.velocity = (float)sign * v;
	if (fly || ball)
		st.velocity = (float)sign * v * 0.75f;

	// updateSlopeRotation: a cube turns towards the slope (and again in Player::updateRotation)
	if (p.vehicle.type == VehicleType::Cube)
		p.easeRotation(p.closestRotation((float)(st.rotation * 57.295780181884766)));
}
