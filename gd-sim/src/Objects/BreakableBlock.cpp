#include <Block.hpp>
#include <Player.hpp>

bool BreakableBlock::touching(Player const& p) const {
	if (Block::touching(p)) {
		return !p.usedEffects.contains(id);
	}

	return false;
}

void BreakableBlock::collide(Player& p) const {
	// To handle snapping accordingly, hand it off to Block
	Block::collide(p);

	if (p.dead)
		p.usedEffects.insert(id);

	// Reset fields that block sets
	p.dead = false;
}

bool BreakableBlock::stopsHead(Player const& p) const {
	return p.vehicle.type != VehicleType::Ufo;
}
