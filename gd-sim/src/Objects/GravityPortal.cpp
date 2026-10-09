#include <Portals.hpp>
#include <Player.hpp>

GravityPortal::GravityPortal(Vec2D size, std::unordered_map<int, std::string>&& fields) : EffectObject(size, std::move(fields)), upsideDown(std::stoi(fields[1]) == 11) {}


void GravityPortal::collide(Player& p) const {
	EffectObject::collide(p);

	if (upsideDown != p.upsideDown) {
		// Gravity portals halve the velocity (that of GD's step of gravity for a player standing on a slope, unless
		// the slope has stopped it again)
		if (p.velocity == 0 && !p.slope.on)
			p.velocity = p.slopeFallVelocity;
		p.velocity = -p.velocity / 2;
		p.flipGravity(upsideDown);
		// (PlayerObject::flipGravity clears m_isOnGround: a slope (or a slope's side) that grounded the player earlier
		// in this step's collisions is undone; standing on a block is last step's, which postCollision handles)
		if (p.slope.on || p.slopeSideLanded)
			p.grounded = false;
		p.gravityPortal = true;
		p.flipPartner();
	}
}
