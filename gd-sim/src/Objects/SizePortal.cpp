#include <Portals.hpp>
#include <Player.hpp>

SizePortal::SizePortal(Vec2D size, std::unordered_map<int, std::string>&& fields) : EffectObject(size, std::move(fields)), small(std::stoi(fields[1]) == 101) {}

/// Easiest portal!
void SizePortal::collide(Player& p) const {
	EffectObject::collide(p);

	if (p.small != small)
		p.resizePending = true;
	bool changes = p.small != small;
	p.small = small;
	// togglePlayerScale restarts a rolling ball's rotation at its new size
	if (changes && p.vehicle.type == VehicleType::Ball && p.isRotating) {
		p.stopRotation();
		p.runBallRotation();
	}
}
