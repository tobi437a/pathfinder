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
		// (Player::startDual), not holding the button.
		p.dual = true;
		p.dualPortalY = (float)pos.y;
		Player& q = *p.partner;
		q.copyAttributes(p);
		q.button = q.input = false;
		q.flipGravity(!p.upsideDown);
		q.velocity = p.velocity;
		q.grounded = false;
		q.onGround2 = false;
		// Both players get the dual ground (updateDualGround)
		p.updateGround((float)pos.y);
		return;
	}

	// Solo portal: dual mode ends. If player 2 took it, player 1 takes its place (copyAttributes).
	p.dual = false;
	Player& p1 = p.isP2 ? *p.partner : p;
	if (p.isP2)
		p1.copyAttributes(p);
	p1.dual = false;
	p1.updateGround(p1.modePortalY);
}
