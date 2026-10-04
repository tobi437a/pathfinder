#include <Portals.hpp>
#include <Player.hpp>

DualPortal::DualPortal(Vec2D size, std::unordered_map<int, std::string>&& fields) : EffectObject(size, std::move(fields)), on(std::stoi(fields[1]) == 286) {}

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
		Player& q = *p.partner;
		q.resetObject();
		q.copyAttributes(p);
		// A player standing on a block (not a slope) has fallen a step of gravity into it when GD checks the portal (the block
		// puts it back afterwards, see Pad::collide); player 2 copies that
		auto const& prev = p.prevPlayer();
		if (prev.grounded && prev.velocity == 0 && p.velocity == 0 && p.upsideDown == prev.upsideDown
			&& prev.gravBottom(prev) > prev.gravFloor() && !p.walkedOff && !prev.slope.on) {
			q.moveY(roundVel(prev.grav(prev.acceleration) * p.dt, prev.upsideDown));
			q.velocity = roundVel(prev.acceleration * p.dt, prev.upsideDown);
		}
		q.flipGravity(!p.upsideDown);
		q.grounded = false;
		q.onGround2 = false;
		// Both players get the dual ground (updateDualGround)
		p.updateGround((float)pos.y);
		return;
	}

	// Solo portal: dual mode ends. If player 2 took it, player 1 takes its place (copyAttributes).
	p.dual = false;
	Player& p1 = p.isP2 ? *p.partner : p;
	if (p.isP2) {
		p1.copyAttributes(p);
		p1.copiedFromP2 = true;
	}
	p1.dual = false;
	p1.updateGround(p1.modePortalY);
}
