#include <Block.hpp>
#include <Slope.hpp>
#include <Level.hpp>
#include <Player.hpp>
#include <cmath>
#include <array>
#include <algorithm>

Block::Block(Vec2D s, std::unordered_map<int, std::string>&& fields) : Object(s, std::move(fields)) {
	// Blocks have a prio of 1, so they are processed later than most other objects.
	prio = 1;

	// No rotation allowed
	if ((int)fabs(rotation) % 180 != 0)
		size = {size.y, size.x};
	rotation = 0;

	// Edge case for this specific block.
	if (fields[1] == "468" && size.y == 5) {
		size.y -= 3.5;
	}
}


/**
 * "Snapping" here refers to what happens when the cube player is
 * holding a button on given block staircases. In order to prevent
 * the player from falling off a block staircase, the X position
 * is manually adjusted by a set amount. The amount depends on
 * the type of staircase, speed, and cube size.
 */
enum class SnapType {
	None,
	BigStair,
	LittleStair,
	DownStair
};


float snapThreshold(Vec2D const& diff, Player const& p) {
	std::array<Vec2D, 3> stairs;
	float threshold;

	switch (p.speed) {
	case 0:
		stairs = { Vec2D(120, -30), Vec2D(90, 30), Vec2D(60, 60) };
		threshold = 1;
		break;
	case 1:
		stairs = { Vec2D(150, -30), Vec2D(p.small ? 90 : 120, 30), Vec2D(90, 60) };
		threshold = 1;
		break;
	case 2:
		stairs = { Vec2D(195, -30), Vec2D(p.small ? 90 : 150, 30), Vec2D(120, 60) };
		threshold = 2;
		break;
	case 3:
		stairs = { Vec2D(225, -30), Vec2D(p.small ? 90 : 180, 30), Vec2D(135, 60) };
		threshold = 2;
		break;
	default:
		stairs = { Vec2D(150, -30), Vec2D(120, 30), Vec2D(90, 60) };
		threshold = 1;
		break;
	}

	for (auto& stair : stairs) {
		if (std::abs(diff.x - stair.x) <= threshold && std::abs(diff.y - stair.y) <= threshold)
			return threshold;
	}

	return 0;
}

void trySnap(Block const& b, Player& p) {
	auto snapData = p.snapData;
	auto diff = b.pos - snapData.object.pos;
	diff.y = p.grav(diff.y);

	if (float threshold = snapThreshold(diff, p); threshold > 0) {
		p.pos.x = std::clamp(
			p.level->getState(snapData.playerFrame).nextPlayer()->pos.x + diff.x,
			p.pos.x - threshold,
			p.pos.x + threshold
		);
	}
}

void Block::collide(Player& p) const {
	// The maximum amount the player can dip below block while still being snapped up. GD
	// (collidedWithObjectInternal) allows 10 (6 for ship/ufo), plus however far the player fell this
	// frame.
	double landClip = (p.vehicle.type == VehicleType::Ufo || p.vehicle.type == VehicleType::Ship) ? 6 : 10;
	landClip += std::max(0.0, -(double)p.grav(p.pos.y - p.prevPlayer().pos.y));

	// When hitting blue orbs, there is a single frame where block collisions arent done. A blue pad pushes
	// the player towards the new floor, which GD lands it on in the same step, except when the player
	// stood on a block: GD's step of gravity (undone only by the floor snap) moved it away first.
	auto const& prev = p.prevPlayer();
	bool padFromBlock = prev.grounded && prev.gravBottom(prev) > prev.gravFloor();
	if (p.upsideDown != prev.upsideDown && !p.gravityPortal && !(p.hitPad && !padFromBlock))
		return;

	/*
		Going from slope to block means you have to check the bottom of the player
		adjusted for the angle of the slope
	*/
	double bottom = p.gravBottom(p);
	if (p.slopeData.slope) {
		if (p.slopeData.slope->angle() > 0) {
			bottom = bottom + sin(p.slopeData.slope->angle()) * p.size.y / 2;
			landClip = 7;

			// Prevent block from catching on slope when it shouldn't
			// TODO this should probably go somewhere else...
			if (p.gravTop(*this) - bottom < 2) {
				return;
			}
		}
	}

	for (auto& entity : p.potentialSlopes) {
		auto block_comp = entity->orientation < 2 ? getTop() : getBottom();
		auto slope_comp = entity->orientation < 2 ? entity->getBottom() : entity->getTop();

		if (block_comp - slope_comp < 2) {
			return;
		}
	}

	// An orb clicked this frame (orbs run before blocks here) still lands the player on the block.
	// Pads and gravity portals don't: GD only lands when moving towards the block (yVelocity <= 0).
	bool padHitBefore = (!p.gravityPortal && !p.hitPad && !p.prevPlayer().grounded && p.prevPlayer().velocity <= 0 && p.velocity > 0);

	// A grounded cube flipped by a gravity portal has GD's walk-off velocity times -0.5 (positive) by now,
	// so it doesn't land this frame (see Player::postCollision). A ball or ufo lands right away.
	bool flippedOffGround = p.gravityPortal && p.upsideDown != p.prevPlayer().upsideDown && p.prevPlayer().grounded
		&& p.prevPlayer().velocity == 0 && p.vehicle.type == VehicleType::Cube;

	if (p.innerHitbox().intersects(*this)) {
		// Hitting block head-on
		p.dead = true;
	} else if (p.vehicle.type != VehicleType::Wave && p.gravTop(*this) - bottom <= landClip && (padHitBefore || p.velocity <= 0) && !flippedOffGround) {
		p.pos.y = p.grav(p.gravTop(*this)) + p.grav(p.size.y / 2);
		p.applyResize();

		// After an orb click, the next frame will cause the player to slightly dip into the block
		if (!padHitBefore)
			p.grounded = true;

		// If on a downhill slope and hits a block, the player is no longer on that slope.
		if (p.slopeData.slope && p.slopeData.slope->angle() < 0) {
			p.slopeData.slope = {};
		}

		// X-snapping
		if (p.vehicle.type == VehicleType::Cube) {
			if (!p.prevPlayer().grounded) {
				if (p.snapData.playerFrame > 0 && p.snapData.playerFrame + 1 < p.frame)
					trySnap(*this, p);
			}

			p.snapData.playerFrame = p.level->currentFrame();
			p.snapData.object = *this;
		}
	} else {
		// Ship and ufo can hit the ceiling of a block without dying
		// Same tolerance as landing: 10 (6 for ship/ufo) plus how far the player rose this frame
		if (p.vehicle.type == VehicleType::Ship || p.vehicle.type == VehicleType::Ufo || p.vehicle.type == VehicleType::Ball) {
			double headClip = (p.vehicle.type == VehicleType::Ball ? 10 : 6) + std::max(0.0, (double)p.grav(p.pos.y - p.prevPlayer().pos.y));
			// GD checks velocity >= 0: 0 counts when another block's head hit or a landing zeroed it this
			// frame, or for a grounded ship that a gravity portal flipped in place, but not for a player
			// walking off (GD has already applied that step's gravity)
			bool shipFlipped = p.gravityPortal && p.vehicle.type == VehicleType::Ship && p.prevPlayer().grounded;
			bool rising = p.velocity > 0 || (p.velocity == 0 && (p.prevPlayer().velocity > 0 || p.grounded || shipFlipped));
			if (p.gravTop(p) - p.gravBottom(*this) <= headClip && rising) {
				if (!stopsHead(p)) {
					p.dead = true;
				} else {
					p.pos.y = p.grav(p.gravBottom(*this)) - p.grav(p.size.y / 2);
					p.velocity = 0;
					p.applyResize();
				}
			}
		}
	}
}