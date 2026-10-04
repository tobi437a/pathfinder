#include <Pad.hpp>
#include <Player.hpp>

Pad::Pad(Vec2D size, std::unordered_map<int, std::string>&& fields) : EffectObject(size, std::move(fields)) {
	switch (atoi(fields[1].c_str())) {
		case 35:
			type = PadType::Yellow;
			break;
		case 67:
			type = PadType::Blue;
			break;
		case 140:
			type = PadType::Pink;
			break;
		case 1332:
			type = PadType::Red;
			break;
		default:
			type = PadType::Yellow;
			break;
	}

	bool flipY = atoi(fields[5].c_str()) == 1;
	if (flipY) {
		rotation += 180;
	}

	// A blue pad flips the player to upside down unless it faces down (GameObject::isFacingDown)
	int rot = (int)stod_def(fields[6]);
	bool facingDown;
	if (rot % 90 != 0)
		facingDown = ((rot >= 91 && rot <= 269) || (rot >= -269 && rot <= -91)) != flipY;
	else
		facingDown = (std::abs(rot) == 180) != flipY;
	flipsToUpsideDown = !facingDown;
}

const velocity_map<PadType, VehicleType, bool> pad_velocities = {
	//											   slow speed   1x speed    2x speed   3x speed
	{{PadType::Yellow, VehicleType::Cube, false}, {864,         864,         864,         864}},
	{{PadType::Yellow, VehicleType::Cube, true},  {691.2,       691.2,       691.2,       691.2}},

	{{PadType::Yellow, VehicleType::Ship, false}, {864,         864,         864,         864}},
	{{PadType::Yellow, VehicleType::Ship, true},  {691.2,       691.2,       691.2,       691.2}},

	{{PadType::Yellow, VehicleType::Ball, false}, {518.4000206, 518.4000206, 518.4000206, 518.4000206}},
	{{PadType::Yellow, VehicleType::Ball, true},  {414.7200165, 414.7200165, 414.7200165, 414.7200165}},

	// GD launches a ufo like a cube; the ufo's velocity cap (432) only applies on the next step
	{{PadType::Yellow, VehicleType::Ufo, false},  {864,         864,         864,         864}},
	{{PadType::Yellow, VehicleType::Ufo, true},   {458.784,     691.2,       691.2,       691.2}},


	// GD: propellPlayer(0.8) (x16, x0.8 mini, x0.6 ball) then flipGravity's x-0.5, whatever the speed
	{{PadType::Blue, VehicleType::Cube, false},   {-345.6,      -345.6,      -345.6,      -345.6}},
	{{PadType::Blue, VehicleType::Cube, true},    {-276.48,     -276.48,     -276.48,     -276.48}},

	{{PadType::Blue, VehicleType::Ship, false},   {-345.6,      -345.6,      -345.6,      -345.6}},
	{{PadType::Blue, VehicleType::Ship, true},    {-276.48,     -276.48,     -276.48,     -276.48}},

	{{PadType::Blue, VehicleType::Ball, false},   {-207.360008, -207.360008, -207.360008, -207.360008}},
	{{PadType::Blue, VehicleType::Ball, true},    {-165.888007, -165.888007, -165.888007, -165.888007}},

	{{PadType::Blue, VehicleType::Ufo, false},    {-345.6,      -345.6,      -345.6,      -345.6}},
	{{PadType::Blue, VehicleType::Ufo, true},     {-276.48,     -276.48,     -276.48,     -276.48}},


	{{PadType::Pink, VehicleType::Cube, false},   {561.6,       561.6,       561.6,       561.6}},
	{{PadType::Pink, VehicleType::Cube, true},    {449.28,      449.28,      449.28,      449.28}},

	{{PadType::Pink, VehicleType::Ship, false},   {302.4,       302.4,       302.4,       302.4}},
	{{PadType::Pink, VehicleType::Ship, true},    {241.92,      241.92,      241.92,      241.92}},

	{{PadType::Pink, VehicleType::Ball, false},   {362.880014,  362.880014,  362.880014,  362.880014}},
	{{PadType::Pink, VehicleType::Ball, true},    {290.304012,  290.304012,  290.304012,  290.304012}},

	{{PadType::Pink, VehicleType::Ufo, false},    {345.6,       345.6,       345.6,       345.6}},
	{{PadType::Pink, VehicleType::Ufo, true},     {276.48,      276.48,      276.48,      276.48}},


	{{PadType::Red, VehicleType::Cube, false},    {1080,        1080,        1080,        1080}},
	{{PadType::Red, VehicleType::Cube, true},     {864,         864,         864,         864}},

	{{PadType::Red, VehicleType::Ship, false},    {544.32,      544.32,      544.32,      544.32}},
	{{PadType::Red, VehicleType::Ship, true},     {656.64,      656.64,      656.64,      656.64}},

	{{PadType::Red, VehicleType::Ball, false},    {648.00002575,648.00002575,648.00002575,648.00002575}},
	{{PadType::Red, VehicleType::Ball, true},     {518.4000206, 518.4000206, 518.4000206, 518.4000206}},

	{{PadType::Red, VehicleType::Ufo, false},     {518.4,       518.4,       518.4,       518.4}},
	{{PadType::Red, VehicleType::Ufo, true},      {677.376,     677.376,     677.376,     677.376}},
};

void Pad::collide(Player& p) const {
	if (type == PadType::Blue && p.upsideDown == flipsToUpsideDown)
		return;

	// GD has already moved a player standing on a block by a step of its old gravity (into that block; only
	// the block snap, which a pad prevents, would undo it), so it is launched from there (and a blue pad's
	// player may miss the new floor by that much)
	auto const& prev = p.prevPlayer();
	if (p.upsideDown == prev.upsideDown && prev.grounded && prev.velocity == 0 && prev.gravBottom(prev) > prev.gravFloor() && !p.walkedOff) {
		// (unless a slope earlier in this step's collisions has put it back on its surface: launched from there)
		if (!p.slope.on)
			p.moveY(roundVel(prev.grav(prev.acceleration) * p.dt, prev.upsideDown));
		p.walkedOff = true;
	}

	// propellPlayer restarts the rotation (before a blue pad's flipGravity)
	p.stopRotation();
	if (p.vehicle.type == VehicleType::Ball)
		p.runBallRotation();
	else
		p.runNormalRotation();

	if (type == PadType::Blue) {
		p.flipGravity(flipsToUpsideDown);
		p.flipPartner();

		if (p.vehicle.type == VehicleType::Wave)
			p.velocity = -p.velocity;
	}

	if (p.vehicle.type != VehicleType::Wave)
		p.velocity = pad_velocities.get(type, p.vehicle.type, p.small, std::min(3, p.speed));

	p.grounded = false;
	p.gravityPortal = false;
	p.hitPad = true;
	p.onGround2 = false;
	p.accelerating = false;
	// PlayerObject::propellPlayer: launched, and off any slope
	p.boosted = true;
	p.slope.on = p.slope.was = false;

	EffectObject::collide(p);

}
