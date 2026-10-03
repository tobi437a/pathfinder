#include <Portals.hpp>
#include <Player.hpp>

MirrorPortal::MirrorPortal(Vec2D size, std::unordered_map<int, std::string>&& fields) : EffectObject(size, std::move(fields)), mirrored(std::stoi(fields[1]) == 45) {}

/// GJBaseGameLayer::toggleFlipped
void MirrorPortal::collide(Player& p) const {
	EffectObject::collide(p);

	if (p.flip.mirrored == mirrored)
		return;
	p.flip.mirrored = mirrored;

	// A finished flip restarts from just off its end; one still running turns around where it is
	float current = p.flipValueAt(p.frame - 1);
	p.flip.from = current == 0 || current == 1 ? (mirrored ? 0.00001f : 0.99999f) : current;
	p.flip.to = mirrored ? 1 : 0;
	p.flip.start = p.frame;
}
