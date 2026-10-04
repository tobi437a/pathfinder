#include <Portals.hpp>
#include <Player.hpp>

GravityPortal::GravityPortal(Vec2D size, std::unordered_map<int, std::string>&& fields) : EffectObject(size, std::move(fields)), upsideDown(std::stoi(fields[1]) == 11) {}


void GravityPortal::collide(Player& p) const {
	EffectObject::collide(p);

	if (upsideDown != p.upsideDown) {
		// Gravity portals halve the velocity
		p.velocity = -p.velocity / 2;
		p.flipGravity(upsideDown);
		// (PlayerObject::flipGravity clears m_isOnGround: a slope that grounded the player earlier in this step's
		// collisions is undone; standing on a block is last step's, which postCollision handles)
		if (p.slope.on)
			p.grounded = false;
		p.gravityPortal = true;
		p.flipPartner();
	}
}
