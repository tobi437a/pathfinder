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
	bool deadBefore = p.dead;
	Block::collide(p);

	// (a player a block killed before keeps colliding with the step's blocks, see Level::stepPlayer)
	if (p.dead && !deadBefore)
		p.usedEffects.insert(id);

	// Reset fields that block sets
	p.dead = deadBefore;
}

bool BreakableBlock::stopsHead(Player const& p) const {
	return p.vehicle.type != VehicleType::Ufo;
}
