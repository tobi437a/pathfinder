#include <Portals.hpp>
#include <Player.hpp>
#include <Level.hpp>

DualPortal::DualPortal(Vec2D size, std::unordered_map<int, std::string>&& fields) : EffectObject(size, std::move(fields)), on(std::stoi(fields[1]) == 286) {}

/// A player standing on a block (not a slope) has fallen a step of gravity into it when GD checks a portal (the block
/// puts it back afterwards, see Pad::collide); the player that copies it (copyAttributes) copies that
static void copyWalkOff(Player const& p, Player& copy) {
	auto const& prev = p.prevPlayer();
	if (prev.grounded && prev.velocity == 0 && p.velocity == 0 && p.upsideDown == prev.upsideDown
		&& prev.gravBottom(prev) > prev.gravFloor() && !prev.slope.on) {
		// (a slope or pad touched before the portal has taken that step already: copied with the position)
		if (!p.walkedOff)
			copy.moveY(roundVel(prev.grav(prev.acceleration) * p.dt, prev.upsideDown));
		copy.velocity = roundVel(prev.acceleration * p.dt, prev.upsideDown);
	} else if (p.walkedOff && prev.slope.on && !p.slope.on && p.velocity == 0) {
		// Likewise on a slope the portal comes before in the collisions: the walk-off step (Player::preCollision)
		// has moved it already, and GD's velocity is that step's gravity
		copy.velocity = roundVel(prev.acceleration * p.dt, prev.upsideDown);
	}
}

void DualPortal::collide(Player& p) const {
	EffectObject::collide(p);
	if (!p.partner || p.dual == on)
		return;

	if (on) {
		// toggleDualMode: player 2 copies player 1 (copyAttributes) with the other gravity and the opposite y
		// velocity (the same relative to its gravity), off the ground. It starts moving the next frame
		// (Player::startDual).
		p.dual = true;
		p.dualPortalY = (float)pos.y;
		// (the ground GD recentres once dual mode ends is this portal's, m_gameState+0x400)
		p.modePortalY = p.dualPortalY;
		if (p.partner)
			p.partner->modePortalY = p.dualPortalY;
		Player& q = *p.partner;
		q.resetObject();
		q.copyAttributes(p);
		copyWalkOff(p, q);
		q.flipGravity(!p.upsideDown);
		// (which runs updatePlayerScale: player 2's hitbox has the new size even if player 1's is still pending)
		q.applyResize();
		q.grounded = false;
		q.onGround2 = false;
		// In two-player mode player 2 lets go of player 1's button (m_jumpBuffered, m_stateRingJump cleared)
		if (p.level->twoPlayer) {
			q.button = q.input = q.buffer = false;
			q.clickUsed = true;
		}
		// Both players get the dual ground (updateDualGround)
		p.updateGround((float)pos.y);
		return;
	}

	// Solo portal: dual mode ends. If player 2 took it, player 1 takes its place (copyAttributes).
	p.dual = false;
	Player& p1 = p.isP2 ? *p.partner : p;
	if (p.isP2) {
		bool groundedBefore = p1.grounded;
		float rotationBefore = p1.rotation;
		bool switches = p1.vehicle.type != p.vehicle.type || p.vehicle.type == VehicleType::Ball;
		p1.copyAttributes(p);
		copyWalkOff(p, p1);
		p1.copiedFromP2 = true;
		// (m_isOnGround stays player 1's: one in the air can't jump on its next step, even if player 2 had landed)
		if (!groundedBefore)
			p1.grounded = false;
		// GD turns player 1 after this (updateRotation, once both stepped), with the spin the copy's mode switch gave it
		// (from 0 if the switch reset the rotation); the sim's player 1 has turned already
		if (switches) {
			if (p1.rotation == rotationBefore && p1.beforeTurn.frame == p1.frame)
				p1.rotation = p1.beforeTurn.rotation;
			p1.turn();
			// (a ship, ufo or wave on player 1's own slope, m_isOnSlope, turns towards it: updateRotation)
			if (p1.vehicle.turn && p1.slope.on)
				p1.slopeRotation();
		}
		// ...and a ship, ufo or wave towards its travel from where player 2 turned last (the copied m_shipRotation), in
		// Player::startDual: from the rotation before the sim's turn
		if (p1.vehicle.turn && !switches)
			p1.rotation = p1.beforeFlyTurn;
	}
	p1.dual = false;
	p1.updateGround(p1.modePortalY);
}
