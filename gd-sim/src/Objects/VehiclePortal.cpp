#include <Portals.hpp>
#include <Player.hpp>
#include <climits>

VehiclePortal::VehiclePortal(Vec2D size, std::unordered_map<int, std::string>&& fields) : EffectObject(size, std::move(fields)) {
	switch (atoi(fields[1].c_str())) {
		case 12:
			type = VehicleType::Cube;
			break;
		case 13:
			type = VehicleType::Ship;
			break;
		case 47:
			type = VehicleType::Ball;
			break;
		case 111:
			type = VehicleType::Ufo;
			break;
		case 660:
			type = VehicleType::Wave;
			break;
		default:
			type = VehicleType::Cube;
			break;
		}
}

void VehiclePortal::collide(Player& player) const {
	EffectObject::collide(player);

	if (player.vehicle.type != type) {
		// GD checks pads and portals against the player's (cached) rect from before them, so the new size only
		// shows for the blocks after a switch into or out of ship, ufo or wave (see scaleRefreshed), or else at
		// the end of the frame (the wave's own size is set next frame, see wave())
		if (type != VehicleType::Wave)
			player.resizePending = true;

		// GD's wave y velocity is the x speed (m_playerSpeed * m_speedMultiplier, rounded; not doubled when
		// mini), up or down; the sim's moves the wave instead. The new vehicle's enter halves it (toggleDartMode).
		if (player.vehicle.type == VehicleType::Wave) {
			double speed = player_speeds[player.speed] / 60;
			double whole = (int)speed;
			speed = std::round((speed - whole) * 1000.0) / 1000.0 + whole;
			player.velocity = (player.velocity > 0 ? 1 : -1) * speed * 54;
		}

		VehicleType from = player.vehicle.type;
		// Coyote frames only count from leaving the ground as this vehicle
		player.coyoteFrames = INT_MAX;
		auto flies = [](VehicleType t) { return t == VehicleType::Ship || t == VehicleType::Ufo || t == VehicleType::Wave; };
		if (flies(from) || flies(type))
			player.scaleRefreshed = true;
		player.vehicle = Vehicle::from(type);
		// toggleFlyMode, toggleBirdMode, toggleDartMode (on or off) and toggleRollMode (off) set the rotation
		// to 0; all of them stop it
		player.stopRotation();
		if (flies(from) || flies(type) || from == VehicleType::Ball)
			player.rotation = 0;
		if (flies(from) || flies(type))
			player.onGround2 = false;
		// ...and switching the ship, ufo or wave off (resetPlayerIcon) starts a cube's spin (a ball's roll too,
		// but toggleRollMode stops that again)
		if (flies(from) && type == VehicleType::Cube)
			player.runNormalRotation();
		// enter scales velocity; find by how much, for a walk-off step added later (Player::postCollision)
		double velocity = player.velocity;
		player.velocity = 1;
		player.vehicle.enter(player, from);
		player.portalVelocityScale *= player.velocity;
		player.velocity = velocity * player.velocity;
	}

	// GD's on-ground flag outlives the switch: a player resting on the old mode's floor stays grounded
	// this frame (and can jump), even if the new mode's floor is lower
	if (player.prevPlayer().grounded && player.velocity == 0 && player.gravBottom(player) <= player.gravFloor())
		player.grounded = true;

	// GJBaseGameLayer::animateInDualGroundNew: the bottom is the portal's y minus half the height, rounded
	// down to 30 in GD's coordinates (90 higher), and at least GD's ground at 90
	float bottom = std::floor(((float)pos.y + 90.f - (float)player.vehicle.bounds * 0.5f) / 30.f) * 30.f;
	player.floor = std::max(0., (double)bottom - 90.);
	player.ceiling = player.floor + player.vehicle.bounds;
}