#include <Vehicle.hpp>
#include <Player.hpp>
#include <Object.hpp>
#include <algorithm>
#include <Slope.hpp>
#include <cfloat>
#include <cmath>

/*
	For ship and ufo, there are two sets of acceleration values depending on the current
	velocity. If the current velocity is higher than one of these thresholds
	(indexed by speed) then a lighter acceleration would be used.
*/
constexpr double velocity_thresholds[] = {
	101.541492,
	103.485494592,
	103.377492,
	103.809492,
	103.809492
};

/// PlayerObject::updateJump's fall: m_yVelocity -= float(dt * gravity * mult) / size each step (size 0.85 for a
/// mini ship or ufo), with GD's dt 0.25f * 0.9f at 240 tps. As an acceleration in sim units (54x, per second).
double gdFall(Player const& p, float gravity, float mult, float size) {
	float dt = (float)(p.dt * 60) * 0.9f;
	return -(double)(dt * gravity * mult / size) * 54.0 / p.dt;
}

/// When the cube lands, it's rotation is snapped to whichever 90 degree angle is closest
float normalizeRotation(Player const& p, float angle) {
    float playerRotation = (int)p.rotation % 360;

    float diff = std::fmod(playerRotation - angle, 90.0f);
    if (diff < 0)
        diff += 90.0f;

    if (std::abs(playerRotation - angle) < std::abs(diff))
        return angle;
    else
        return playerRotation - diff;
}

/// Ship, UFO, Wave all have the same basic mechanism for rotation
void rotateFly(Player& p, float mult) {
	auto diff = p.pos - p.prevPlayer().pos;

	// Unknown why this happens
	if (p.dt * 72 <= std::pow(diff.x, 2) + std::pow(diff.y, 2)) {
		p.rotation = slerp(p.rotation * 0.017453292f, atan2(diff.y, diff.x), (p.dt * 60) * mult) * 57.29578f;
	}
}

Vehicle cube() {
	Vehicle v;
	 v.type = VehicleType::Cube;

	v.enter = +[](Player& p) {
		// GD halves y velocity when the ship, ufo or wave mode is toggled off (or on)
		if (p.prevPlayer().vehicle.type != VehicleType::Ball)
			p.velocity = p.velocity / 2;

		if (p.prevPlayer().vehicle.type == VehicleType::Ship && p.input)
			p.buffer = true;
	};

	v.clamp = +[](Player& p) {
		if (p.velocity < -810)
			p.velocity = -810;

		if (p.gravTop(p.innerHitbox()) >= p.gravCeiling())
			p.dead = true;
	};

	v.update = +[](Player& p) {
		// m_gravity per speed (updateTimeMod)
		static float const gravity[] = {0.940199f, 0.958199f, 0.957199f, 0.961199f, 0.961199f};
		p.acceleration = gdFall(p, gravity[p.speed], 1);

		//TODO add rotation
		p.rotation = 0;

		bool jump = false;

		if (p.grounded) {
			if (p.input) {
				jump = true;
			} else {
				p.setVelocity(0, true);
			}
			p.buffer = false;
		}

		if (p.upsideDown && p.input && p.coyoteFrames < 10) {
			jump = true;
			p.buffer = false;
		}

		if (jump) {
			static double jumpHeights[] = {
				573.481728,
				603.7217172,
				616.681728,
				606.421728,
				606.421728
			};

			p.setVelocity(jumpHeights[p.speed], p.prevPlayer().input);
			p.grounded = false;
			p.boosted = true;
			// Jumping off a slope adds a quarter of its launch
			p.slopeJump(0.25f);
		}
	};

	v.bounds = FLT_MAX;

	return v;
}

double shipAcceleration(Player const& p) {
	if (p.input) {
		if (p.velocity <= p.grav(velocity_thresholds[p.speed]))
			return p.small ? 1643.5872 : 1397.0491;
		return p.small ? 1314.86976 : 1117.64328;
	}
	if (p.velocity >= p.grav(velocity_thresholds[p.speed]))
		return p.small ? -1577.85408 : -1341.1719;
	return p.small ? -1051.8984 : -894.11464;
}

double ufoAcceleration(Player const& p) {
	float size = p.small ? 0.85f : 1;
	if (p.velocity > p.grav(velocity_thresholds[p.speed]))
		return gdFall(p, 0.958199f, 0.6f, size);
	return gdFall(p, 0.958199f, 0.4f, size);
}

Vehicle ship() {
	Vehicle v;
	v.type = VehicleType::Ship;

	v.enter = +[](Player& p) {
		if (p.prevPlayer().vehicle.type == VehicleType::Ufo || p.prevPlayer().vehicle.type == VehicleType::Wave)
			p.velocity = p.velocity / 4.0;
		else 
			p.velocity = p.velocity / 2.0;
	};

	v.clamp = +[](Player& p) {
		// Can't buffer clicks on ship
		p.buffer = false;

		// Max velocity
		p.velocity = std::clamp(p.velocity, 
			p.small ? -406.566 : -345.6,
			p.small ? 508.248 : 432.0
		);

		//TODO deal with slope velocity more consistently

		// Slopes complicate things like "maximum velocity"
		/*if (p.input)
			p.velocity = std::min(p.velocity, p.small ? 508.248 : 432.0);
		else
			p.velocity = std::max(p.velocity, p.small ? -406.566 : -345.6);*/


		// Put back under the ceiling before collisions (Player::preCollision, like GD); something that moves
		// it over the ceiling later in the frame (a size portal) only counts next frame
		if (p.ceilingSnapped && p.upsideDown == p.prevPlayer().upsideDown) {
			// GD zeroes velocity when it hits the ceiling and adds gravity before the next move
			if (p.velocity > 0) {
				p.setVelocity(0, false);
				p.velocity = roundVel(shipAcceleration(p) * p.dt, p.upsideDown);
			}
		}
	};

	v.update = +[](Player& p) {
		p.buffer = false;
	
		if (p.grounded)
			p.setVelocity(0, !p.input);

		p.acceleration = shipAcceleration(p);

		if (p.grav(p.pos.y) >= p.gravCeiling()) {
			p.setVelocity(0, false);
		}

		rotateFly(p, 0.15f);
	};

	v.bounds = 300;

	return v;
}

Vehicle ball() {
	Vehicle v;

	v.type = VehicleType::Ball;
	v.clamp = +[](Player& p) {
		if (p.velocity >= 810)
			p.velocity = 810;
		if (p.velocity <= -810)
			p.velocity = -810;

		// GD stops the ball with its top at the ceiling
		if (p.gravTop(p) >= p.gravCeiling() && p.velocity > 0) {
			p.pos.y = p.grav(p.gravCeiling()) - p.grav(p.size.y / 2);
			double hitVelocity = p.velocity;
			p.setVelocity(0, true);

			// Hitting the ceiling with the button held flips the ball, which uses up the click. GD's hitGround
			// only counts the ceiling as ground when the ball reaches it at y velocity <= 5 (270 here).
			if (p.input && hitVelocity <= 270) {
				p.flipGravity(!p.upsideDown);
				p.input = false;
				p.buffer = false;
			} else {
				// Otherwise it leaves the ceiling on the next step, which adds gravity before the move
				p.velocity = roundVel(gdFall(p, 0.958199f, 0.6f) * p.dt, p.upsideDown);
			}
		}
	};

	v.enter = +[](Player& p) {
		if (p.input)
			p.vehicleBuffer = true;

		switch (p.prevPlayer().vehicle.type) {
			case VehicleType::Ship:
			case VehicleType::Ufo:
			case VehicleType::Wave:
				p.velocity = p.velocity / 2;
				break;
			default: break;
		}
	};

	v.update = +[](Player& p) {
		// A ball falls with GD's fixed 0.958199 (not m_gravity), times 0.6
		p.acceleration = gdFall(p, 0.958199f, 0.6f);

		if (!p.input)
			p.vehicleBuffer = false;

		bool jump = false;

		if (p.grounded) {
			//p.rotVelocity = p.grav(600);
			if (p.input && (p.prevPlayer().buffer || !p.prevPlayer().input || p.vehicleBuffer)) {
				jump = true;
			} else {
				p.setVelocity(0, true);
			}
			p.buffer = false;
		} else if (p.buffer && p.coyoteFrames < (p.upsideDown ? 16 : 1)) {
			jump = true;
		}

		if (jump) {
			static double jumpHeights[] = {
				-172.044007,
				-181.11601,
				-185.00401,
				-181.92601,
				-181.92601
			};

			bool slopeBoost = (p.slope.was || p.slope.on) && 0 < p.grav(p.slope.velocity);
			bool override = p.prevPlayer().buffer || p.vehicleBuffer;
			if (slopeBoost) {
				// GD jumps up (rounded), adds a quarter of the slope's launch (up to 1.4 times the jump), then
				// flips gravity (half the velocity) and takes 0.6 of it, unrounded
				p.setVelocity(-jumpHeights[p.speed] / (0.5 * 0.6000000238418579), override);
				p.slopeJump(0.25f);
				double v = p.gdVelocity() * 0.5 * 0.6000000238418579;
				p.flipGravity(!p.upsideDown);
				p.velocity = p.grav(v * 54);
			} else {
				p.flipGravity(!p.upsideDown);
				p.setVelocity(jumpHeights[p.speed], override);
			}
			p.boosted = true;
			p.vehicleBuffer = false;
			p.buffer = false;
			p.input = false;
			// GD rounds y velocity only where it applies gravity, which a buffered jump skips
			p.roundVelocity = !p.velocityOverride;
		}
	};

	v.bounds = 240;

	return v;
}

Vehicle ufo() {
	Vehicle v;

	v.type = VehicleType::Ufo;
	v.enter = +[](Player& p) {
		VehicleType pv = p.prevPlayer().vehicle.type;
		if ((pv == VehicleType::Ship || pv == VehicleType::Wave) && p.input)
			p.buffer = true;

		p.velocity = p.velocity / (pv == VehicleType::Ship || pv == VehicleType::Wave ? 4 : 2);
	};

	v.clamp = +[](Player& p) {
		p.velocity = std::clamp(p.velocity, 
			p.small ? -406.566 : -345.6,
			p.small ? 508.248 : 432.0
		);

		p.input = p.button;

		// Put back under the ceiling before collisions (Player::preCollision, like GD); something that moves
		// it over the ceiling later in the frame (a size portal) only counts next frame
		if (p.ceilingSnapped && p.upsideDown == p.prevPlayer().upsideDown) {
			// GD zeroes velocity when it hits the ceiling and adds gravity before the next move. A jump
			// this frame (GD's next step) still moves up into the ceiling first, so the ufo stays there
			// a step longer (preCollision stops it).
			if (p.velocity > 0 && !p.velocityOverride) {
				p.setVelocity(0, false);
				p.velocity = roundVel(ufoAcceleration(p) * p.dt, p.upsideDown);
			}
		}

	};

	v.update = +[](Player& p) {
		if (p.buffer) {
			p.velocity = std::max(p.velocity, p.small ? 358.992 : 371.034);
			p.velocityOverride = true;
			p.buffer = false;
			p.grounded = false;
		} else {
			p.acceleration = ufoAcceleration(p);

			if (p.grounded) {
				p.setVelocity(0, true);
			}

			if (p.button)
				p.input = false;
		}
	};

	v.bounds = 300;

	return v;
}

Vehicle wave() {
	Vehicle v;
	v.type = VehicleType::Wave;
	v.enter = +[](Player& p) {
		p.actions.push_back(+[](Player& p) {
			p.size = p.small ? Vec2D(6, 6) : Vec2D(10, 10);
		});
	};

	v.clamp = +[](Player& p) {
		float waveTop = p.grav(p.pos.y + p.grav(p.size.y)) ;
		float waveBottom = p.grav(p.pos.y - p.grav(p.size.y));

		p.velocity = (p.input * 2 - 1) * player_speeds[p.speed] * (p.small ? 2 : 1);

		if (waveBottom <= p.gravFloor()) {
			p.pos.y = p.grav(p.gravFloor() + p.size.y);
			if (waveBottom == p.gravFloor() && !p.input)
				p.velocity = 0;
		} else if (waveTop >= p.gravCeiling()) {
			p.pos.y = p.grav(p.gravCeiling() - p.size.y);
			if (waveTop == p.gravCeiling() && p.input)
				p.velocity = 0;
		}

		//p.rotation = (p.input ? 1 : -1) * (p.small ? 63.4258423 : 45);
	};
	v.update = +[](Player& p) {
		p.acceleration = 0;
		p.buffer = false;

		rotateFly(p, p.small ? 0.4 : 0.25);
	};

	v.bounds = 300;

	return v;
}

Vehicle Vehicle::from(VehicleType v) {
	switch (v) {
		case VehicleType::Cube:
			return cube();
		case VehicleType::Ship:
			return ship();
		case VehicleType::Ball:
			return ball();
		case VehicleType::Ufo:
			return ufo();
		case VehicleType::Wave:
			return wave();
	}
}