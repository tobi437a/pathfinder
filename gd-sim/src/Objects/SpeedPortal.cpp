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


void SpeedPortal::collide(Player& p) const {
	EffectObject::collide(p);

	p.speed = speed;
	// updateTimeMod restarts a ball's roll at the new speed, at the start of GD's next step: this step still
	// turns with the old spin (pendingSpin)
	if (p.vehicle.type == VehicleType::Ball) {
		Player::Spin before {p.rotationSpeed, p.rotateSpeed, p.isRotating, p.ballRotating, p.ballRotating2};
		p.stopRotation();
		p.runBallRotation();
		p.pendingSpin = Player::Spin {p.rotationSpeed, p.rotateSpeed, p.isRotating, p.ballRotating, p.ballRotating2};
		p.rotationSpeed = before.speed;
		p.rotateSpeed = before.rotateSpeed;
		p.isRotating = before.isRotating;
		p.ballRotating = before.ballRotating;
		p.ballRotating2 = before.ballRotating2;
	}
}
