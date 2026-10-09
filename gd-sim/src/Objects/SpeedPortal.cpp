#include <Portals.hpp>
#include <Player.hpp>

SpeedPortal::SpeedPortal(Vec2D size, std::unordered_map<int, std::string>&& fields) : EffectObject(size, std::move(fields)) {
	switch (atoi(fields[1].c_str())) {
		case 200:
			speed = 0;
			break;
		case 201:
			speed = 1;
			break;
		case 202:
			speed = 2;
			break;
		case 203:
			speed = 3;
			break;
		case 1334:
			speed = 4;
			break;
	}
}


bool SpeedPortal::touching(Player const& p) const {
	// GD handles a speed portal as a trigger (playerTouchedTrigger), which in dual mode fires once in all: not again
	// for the other player (unlike pads, orbs and the other portals, once per player)
	if (p.partner && p.dual && p.partner->usedEffects.contains(id))
		return false;
	return EffectObject::touching(p);
}

void SpeedPortal::collide(Player& p) const {
	EffectObject::collide(p);

	// GD sets the new speed at the start of its next step (updateTimeMod): the rest of this step's collisions
	// (slopes, orbs, X-snaps) still see the old one (Player::postCollision applies it)
	p.pendingSpeed = speed;
	p.tookSpeedPortal = true;
	// (updateTimeMod also restarts a ball's roll, at the start of GD's next step: Player::postCollision)
}
