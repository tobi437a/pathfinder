#include <Block.hpp>
#include <Slope.hpp>
#include <Level.hpp>
#include <Player.hpp>
#include <cmath>
#include <array>
#include <algorithm>
#include <iostream>

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
		// GD aims at the new block's sprite plus the player's offset from the old block's sprite back then
		// (PlayerObject::checkSnapJumpToObject); the pattern above uses hitbox positions
		float fromX = snapData.playerFrame <= p.level->baseFrame ? snapData.playerX
			: p.level->getState(snapData.playerFrame, p.isP2).nextPlayer()->pos.x;
		p.pos.x = std::clamp(
			fromX + diff.x + p.spriteShift(b.pos.x) - snapData.spriteDx,
			p.pos.x - threshold,
			p.pos.x + threshold
		);
	}
}

void Block::snapX(Player& p) const {
	if (p.vehicle.type != VehicleType::Cube)
		return;
	if (!p.prevPlayer().grounded) {
		if (p.snapData.playerFrame > 0 && p.snapData.playerFrame + 1 < p.frame && p.snapData.fromSolid) {
			float before = p.pos.x;
			trySnap(*this, p);
			// Like the verifier's Snap lines in real.txt (sim frame = real frame + 1)
			if (p.level->debug)
				std::cout << "Snap " << p.frame - 1 << " obj " << pos.x << " " << pos.y << " prev "
						  << p.snapData.object.pos.x << " " << p.snapData.object.pos.y << " x " << before
						  << " -> " << p.pos.x << std::endl;
		}
	}

	p.snapData.playerFrame = p.level->currentFrame();
	p.snapData.object = *this;
	p.snapData.fromSolid = !dynamic_cast<BreakableBlock const*>(this);
	p.snapData.spriteDx = p.spriteShift(pos.x);
}

void Block::collide(Player& p) const {
	collideBlock(p, false);
}

void Block::collideBlock(Player& p, bool bare) const {
	// The maximum amount the player can dip below block while still being snapped up. GD
	// (collidedWithObjectInternal) allows 10 (6 for ship/ufo), plus however far the player fell this
	// frame, plus on a slope the extra half height its angle gives the player.
	double slopeClip = p.slope.was ? p.slope.clipExtra : 0;
	double landClip = (p.vehicle.type == VehicleType::Ufo || p.vehicle.type == VehicleType::Ship) ? 6 : 10;
	landClip += slopeClip + std::max(0.0, -(double)p.grav(p.pos.y - p.prevPlayer().pos.y));

	// A blue orb clicked this frame flips the player. GD clicks it a step later, so in this step it still
	// lands on a block on the old side (not grounded: the orb launches it), and none on the new side. With
	// the button already held, GD takes the orb in this step's collisions, before the blocks, which then
	// act in the new gravity. A blue pad pushes the player towards the new floor, which GD lands it on in
	// the same step if it reaches it (see Pad::collide for a player flipped off a block).
	auto const& prev = p.prevPlayer();
	if (p.upsideDown != prev.upsideDown && !p.gravityPortal && !p.hitPad && !prev.button && !p.partnerFlipped) {
		double oldClip = landClip - std::max(0.0, -(double)p.grav(p.pos.y - prev.pos.y))
			+ std::max(0.0, -(double)prev.grav(p.pos.y - prev.pos.y));
		double oldBottom = prev.gravBottom(p);
		if (prev.gravTop(*this) - oldBottom <= oldClip) {
			// (still standing on it in the old gravity, see Player::onBlock)
			p.onBlock = true;
			if (!p.innerHitbox().intersects(*this) && prev.velocity <= 0 && oldBottom < prev.gravTop(*this)) {
				p.pos.y = prev.grav(prev.gravTop(*this)) + prev.grav(p.size.y / 2);
				// (X-snap in the gravity it landed in)
				if (!bare) {
					p.upsideDown = prev.upsideDown;
					snapX(p);
					p.upsideDown = !prev.upsideDown;
				}
			}
		}
		return;
	}

	double bottom = p.gravBottom(p);

	// A slope near the player whose surface covers this block's top (bottom) means the player isn't
	// landing on (hitting its head on) the block (GD walks m_potentialSlopeMap). Unless the player is on a
	// slope or this is a slope's side, only slopes over the block's centre count; a ceiling slope only
	// covers a bottom and a floor slope a top, unless it's the player's slope.
	bool coverTop = false, coverBottom = false;
	if (p.slope.on || p.slope.was || !p.potentialSlopes.empty()) {
		bool any = p.slope.on || p.slope.was || bare;
		GDRect block = GDRect::of(*this), player = GDRect::of(p);
		for (Slope const* s : p.potentialSlopes) {
			if (!any && s != p.slope.current && !(s->getLeft() < pos.x && pos.x < s->getRight()))
				continue;
			GDRect sr = GDRect::of(*s, 1.2f, 1.1f);
			if (!sr.intersects(block) || !sr.intersects(player))
				continue;
			double sy = s->surfaceY(s->uphill != s->ceiling() ? block.minX() : block.maxX());
			bool mine = s == p.slope.current;
			if (block.maxY() <= sy + 2 && (!s->ceiling() || mine))
				coverTop = true;
			if (sy - 2 <= block.minY() && (s->ceiling() || mine))
				coverBottom = true;
			if (coverTop && coverBottom)
				break;
		}
	}
	bool coverFeet = p.upsideDown ? coverBottom : coverTop;
	bool coverHead = p.upsideDown ? coverTop : coverBottom;

	// An orb clicked this frame (orbs run before blocks here) still lands the player on the block: GD
	// handles the click a step later. Not one taken with the button held as it's touched, which GD
	// activates in this step's collisions. Pads and gravity portals don't: GD only lands when moving
	// towards the block (yVelocity <= 0), or when it was on a slope last frame.
	bool padHitBefore = (!p.gravityPortal && !p.hitPad && !p.prevPlayer().grounded && p.prevPlayer().velocity <= 0 && p.velocity > 0 && !(p.clickedOrb && p.prevPlayer().button));

	// A grounded cube flipped by a gravity portal has GD's walk-off velocity times -0.5 (positive) by now,
	// so it doesn't land this frame (see Player::postCollision). A ball or ufo lands right away.
	bool flippedOffGround = p.gravityPortal && p.upsideDown != p.prevPlayer().upsideDown && p.prevPlayer().grounded
		&& p.prevPlayer().velocity == 0 && p.vehicle.type == VehicleType::Cube;

	// Standing on this block whatever the velocity (an orb clicked on it), see Player::onBlock
	if (p.gravTop(*this) - bottom <= landClip)
		p.onBlock = true;

	// collidedWithObjectInternal: a player rising (y velocity > 0, not off a slope) with its feet within the
	// landing distance of the block's top neither lands nor gets killed by its inner hitbox (GD leaves that
	// test out once the player is in landing range)
	bool risingOnto = !p.upsideDown && p.gravTop(*this) - bottom <= landClip && p.velocity > 0 && !padHitBefore && !p.slope.was;

	if (p.innerHitbox().intersects(*this) && !risingOnto) {
		// Hitting block head-on. Within 0.1 s of a gravity flip GD instead puts the player on the block's far
		// side from its (new) floor and grounds it there (hitGround: velocity 0, on ground if slow enough), if the
		// block is on its head side: its bottom at least the landing clip above the player's (before or after
		// this frame's move)
		double clip = (p.vehicle.type == VehicleType::Ufo || p.vehicle.type == VehicleType::Ship) ? 6 : 10;
		double rise = p.grav(p.pos.y - p.prevPlayer().pos.y);
		bool headSide = p.gravBottom(*this) >= std::min(bottom + clip, bottom + clip - rise);
		if (headSide && p.lastFlipFrame > 0 && stepsWithin(p.frame - p.lastFlipFrame, 0.1) && p.vehicle.type != VehicleType::Wave) {
			p.pos.y = p.grav(p.gravBottom(*this)) - p.grav(p.size.y / 2);
			if (p.velocity <= 270)
				p.grounded = true;
			p.velocity = 0;
			p.headHit = true;
		} else
			p.dead = true;
	} else if (p.vehicle.type != VehicleType::Wave && p.gravTop(*this) - bottom <= landClip && (padHitBefore || p.velocity <= 0 || p.slope.was) && !flippedOffGround && !coverFeet) {
		p.pos.y = p.grav(p.gravTop(*this)) + p.grav(p.size.y / 2);
		p.applyResize();

		// After an orb click, the next frame will cause the player to slightly dip into the block. hitGround stops
		// the player, but only grounds it (m_isOnGround) at y velocity <= 5 relative to gravity: a player rising
		// faster off a slope isn't (it can't jump from there)
		if (!padHitBefore && p.velocity > 5 * 54)
			p.velocity = 0;
		else if (!padHitBefore)
			p.grounded = true;

		if (!bare)
			snapX(p);
	} else {
		// Ship and ufo can hit the ceiling of a block without dying
		// Same tolerance as landing: 10 (6 for ship/ufo) plus how far the player rose this frame
		if (p.vehicle.type == VehicleType::Ship || p.vehicle.type == VehicleType::Ufo || p.vehicle.type == VehicleType::Ball) {
			double headClip = (p.vehicle.type == VehicleType::Ball ? 10 : 6) + slopeClip + std::max(0.0, (double)p.grav(p.pos.y - p.prevPlayer().pos.y));
			// GD checks velocity >= 0: 0 counts when another block's head hit or a landing zeroed it this
			// frame, or for a grounded ship, ufo or ball that a gravity portal flipped in place (GD resolved the block before the portal), but not for a player
			// walking off (GD has already applied that step's gravity)
			bool flippedOnGround = p.gravityPortal && p.vehicle.type != VehicleType::Cube && p.prevPlayer().grounded;
			// A landing this frame counts whatever the sim's velocity is: GD zeroes it when landing.
			// So does a snap onto the mode floor this frame.
			// A flying player whose velocity just reached 0 (a ship slowing its fall) counts too.
			bool rising = p.velocity > 0 || p.grounded || p.floorSnapped || p.slope.was || (p.velocity == 0 && (p.prevPlayer().velocity > 0 || flippedOnGround || !p.prevPlayer().grounded));
			if (p.gravTop(p) - p.gravBottom(*this) <= headClip && rising && !coverHead) {
				if (!stopsHead(p)) {
					p.dead = true;
				} else {
					p.pos.y = p.grav(p.gravBottom(*this)) - p.grav(p.size.y / 2);
					p.velocity = 0;
					p.headHit = true;
					p.applyResize();
				}
			}
		}
	}
}