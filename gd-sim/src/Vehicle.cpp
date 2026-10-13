#include <Vehicle.hpp>
#include <Player.hpp>
#include <Object.hpp>
#include <climits>
#include <algorithm>
#include <Slope.hpp>
#include <cfloat>
#include <cmath>

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

/// The direction of travel updateShipRotation turns towards: from where the player was at the last step's turn
/// (m_shipRotation, see Player::shipRotation)
static Vec2D travel(Player const& p) {
	return p.pos - p.shipRotation;
}

/// PlayerObject::updateShipRotation (ship, wave): turns towards the direction of travel
void rotateFly(Player& p, float mult) {
	// (not on a slope: Player::updateRotation turns it towards the slope)
	if (p.slope.on)
		return;
	auto diff = travel(p);

	// Unknown why this happens
	if (p.dt * 72 <= std::pow(diff.x, 2) + std::pow(diff.y, 2)) {
		// (atan2f of GD's float travel)
		p.rotation = slerp(p.rotation * 0.017453292f, atan2f((float)diff.y, (float)diff.x), (p.dt * 60) * mult) * 57.29578f;
	}
}

/// PlayerObject::updateShipRotation for the ufo: it leans back by 0.4 of its direction of travel, forward by at
/// most 0.1 rad (GD's rotation is clockwise, the sim's counter-clockwise), turning at 0.07
void rotateUfo(Player& p) {
	if (p.slope.on)
		return;
	auto diff = travel(p);
	if (p.dt * 72 <= std::pow(diff.x, 2) + std::pow(diff.y, 2)) {
		float lean = atan2f((float)diff.y, (float)diff.x) * 0.4f;
		// (in the gravity GD has at its turn: not yet an orb's it clicks at its next step)
		bool upsideDown = p.upsideDown != p.orbFlipPending;
		float gdTarget = !upsideDown ? std::max(-0.1f, lean) : std::min(0.1f, lean);
		p.rotation = slerp(p.rotation * 0.017453292f, -gdTarget, (float)(p.dt * 60) * 0.07f) * 57.29578f;
	}
}

Vehicle cube() {
	Vehicle v;
	 v.type = VehicleType::Cube;

	v.enter = +[](Player& p, VehicleType from) {
		// GD halves y velocity when the ship, ufo or wave mode is toggled off (or on)
		if (from != VehicleType::Ball)
			p.velocity = p.velocity / 2;

		if (from == VehicleType::Ship && p.input)
			p.buffer = true;
	};

	v.clamp = +[](Player& p) {
		if (p.velocity < -810)
			p.velocity = -810;

		if (p.gravTop(p.innerHitbox()) >= p.gravCeiling())
			p.dead = true;
	};

	v.update = +[](Player& p) {
		p.acceleration = stepAcceleration(p);

		bool jump = false;

		if (p.grounded) {
			// (not when GD cleared the on-ground flag after the landing, e.g. a ufo portal off a slope: standing, it
			// can't jump in GD's next step)
			if (p.input && !p.groundCleared) {
				jump = !p.partnerFlipsNext;
			} else {
				p.setVelocity(0, true);
			}
			p.buffer = false;
		}

		// GD's on-ground flag outlives a walk-off until updateJump finds the cube falling (playerIsFallingBugged):
		// faster than 2g, 9 steps, upside down or in dual mode (m_unkA99); otherwise at once
		if ((p.upsideDown || p.dual) && p.input && p.coyoteFrames < 10) {
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

			p.cubeJumped = true;
			p.beforeCubeJump = {p.boosted, p.clickUsed, p.buffer, p.onGround2, p.grounded,
				Player::Spin {p.rotationSpeed, p.rotateSpeed, p.isRotating, p.ballRotating, p.ballRotating2}};
			p.setVelocity(jumpHeights[p.speed], p.prevPlayer().input);
			p.grounded = false;
			p.boosted = true;
			p.clickUsed = true;
			// Jumping off a slope adds a quarter of its launch
			p.slopeJump(0.25f);
			p.cubeJumpVelocity = p.velocity;
			p.stopRotation();
			p.runNormalRotation();
			p.onGround2 = false;
		}
	};

	v.bounds = FLT_MAX;

	return v;
}

double shipAcceleration(Player const& p) {
	// PlayerObject::updateJump: gravity * a factor, 1.2 / 0.8 falling or -1 rising (* 0.5 / 0.4 held,
	// by playerIsFallingBugged). While m_isAccelerating, a held ship that is rising gets the 0.8 (it
	// slows down), and one falling without being held gets the -1 (it slows its fall).
	bool rising = p.velocity >= 0;
	if (p.input && !(p.accelerating && rising)) {
		if (p.fallingBugged())
			return p.small ? 1643.5872 : 1397.0491;
		return p.small ? 1314.86976 : 1117.64328;
	}
	if (p.input) {
		if (p.fallingBugged())
			return p.small ? -1314.86976 : -1117.64328;
		return p.small ? -1051.8984 : -894.11464;
	}
	if (p.accelerating && !rising)
		return p.small ? 1314.86976 : 1117.64328;
	if (!p.fallingBugged())
		return p.small ? -1577.85408 : -1341.1719;
	return p.small ? -1051.8984 : -894.11464;
}

double ufoAcceleration(Player const& p) {
	float size = p.small ? 0.85f : 1;
	if (!p.fallingBugged())
		return gdFall(p, 0.958199f, 0.6f, size);
	return gdFall(p, 0.958199f, 0.4f, size);
}

Vehicle ship() {
	Vehicle v;
	v.type = VehicleType::Ship;

	v.enter = +[](Player& p, VehicleType from) {
		if (from == VehicleType::Ufo || from == VehicleType::Wave)
			p.velocity = p.velocity / 4.0;
		else 
			p.velocity = p.velocity / 2.0;
	};

	v.clamp = +[](Player& p) {
		// Can't buffer clicks on ship
		p.buffer = false;

		// Max velocity, not while m_isAccelerating
		if (!p.accelerating)
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
	};
	v.turn = +[](Player& p) { rotateFly(p, 0.15f); };

	v.bounds = 300;

	return v;
}

Vehicle ball() {
	Vehicle v;

	v.type = VehicleType::Ball;
	v.clamp = +[](Player& p) {
		// (updateJump caps the fall only: a pad launches a ball faster than 810)
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
				p.ballJumped = true;
			p.jumpBuffered = false;
			} else {
				// Otherwise it leaves the ceiling on the next step, which adds gravity before the move. hitGround
				// still starts its roll (without the on-ground flags)
				bool onGround2 = p.onGround2;
				p.hitGroundRotation();
				p.onGround2 = onGround2;
				// (hitGround's m_isOnGround, at <= 270: updateJump clears it before falling, unless the ball is
				// boosted, whose updateJump only applies gravity)
				p.ceilingGround = hitVelocity <= 270 && p.boosted;
				p.velocity = roundVel(gdFall(p, 0.958199f, 0.6f) * p.dt, p.upsideDown);
			}
		}
	};

	v.enter = +[](Player& p, VehicleType from) {
		if (p.input)
			p.vehicleBuffer = true;

		switch (from) {
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
			// (not when GD cleared the on-ground flag after the landing, e.g. a ball portal off a slope, see cube)
			if (p.input && !p.groundCleared && (p.prevPlayer().buffer || !p.prevPlayer().input || p.vehicleBuffer)) {
				jump = !p.partnerFlipsNext;
			} else {
				p.setVelocity(0, true);
			}
			p.buffer = false;
		} else if (p.buffer && p.coyoteFrames < (p.upsideDown || p.dual ? 16 : 1)) {
			jump = !p.partnerFlipsNext;
		} else if (p.prevPlayer().ceilingGround && p.input && (p.prevPlayer().buffer || !p.prevPlayer().input || p.vehicleBuffer)) {
			jump = !p.partnerFlipsNext;
		}

		if (jump) {
			static double jumpHeights[] = {
				-172.044007,
				-181.11601,
				-185.00401,
				-181.92601,
				-181.92601
			};

			p.beforeBallJump = {p.boosted, p.jumpBuffered, p.vehicleBuffer, p.clickUsed};
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
			p.ceilingGround = false;
			p.ballJumped = true;
			p.jumpBuffered = false;
			p.clickUsed = true;
			// (GD's jump clears m_isOnGround: no coyote jump after it until the ball lands)
			p.coyoteFrames = INT_MAX;
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
	v.enter = +[](Player& p, VehicleType from) {
		if ((from == VehicleType::Ship || from == VehicleType::Wave) && p.input)
			p.buffer = true;

		p.velocity = p.velocity / (from == VehicleType::Ship || from == VehicleType::Wave ? 4 : 2);
	};

	v.clamp = +[](Player& p) {
		// Max velocity, not while m_isAccelerating
		if (!p.accelerating)
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
		// PlayerObject::updateJump: a click sets the jump velocity (7, mini 0.85 * 8) only if the ufo is slower;
		// a faster one just keeps falling
		double jumpSpeed = (p.small ? (double)(0.85f * 8.0f) : 7.0) * 54;
		// (either way the click is used up, m_stateRingJump: a ball entered later with it still held can't take an
		// orb with it)
		if (p.buffer)
			p.clickUsed = true;
		if (p.buffer && !(p.velocity < jumpSpeed)) {
			p.buffer = false;
			p.grounded = false;
			p.acceleration = ufoAcceleration(p);
		} else if (p.buffer) {
			double jump = p.small ? 358.992 : 371.034; // the jump after the step's gravity
			if ((p.slope.was || p.slope.on) && 0 < p.slope.velocity) {
				// PlayerObject::updateJump: jumping from a slope adds half its launch speed (m_slopeVelocity, not
				// relative to gravity), up to 1.4 times the jump, before that step's gravity
				p.setGdVelocity(p.grav((double)(p.small ? 0.85f * 8.0f : 7.0f)));
				float cap = (float)(p.gdVelocity() * 1.399999976158142);
				p.setGdVelocity((double)(p.slope.velocity * 0.5f) + p.gdVelocity());
				p.setGdVelocity(std::min(p.gdVelocity(), (double)cap));
				p.velocity = roundVel(p.velocity + ufoAcceleration(p) * p.dt, p.upsideDown);
			} else {
				p.velocity = jump;
			}
			p.velocityOverride = true;
			p.buffer = false;
			p.grounded = false;
		} else {
			if (p.grounded) {
				p.setVelocity(0, true);
			}
			// (the next updateJump's gravity, for a grounded ufo from GD's stopped velocity: hitGround zeroed it, even
			// when a pad's launch is what the sim's velocity still has)
			p.acceleration = ufoAcceleration(p);

			if (p.button)
				p.input = false;
		}
	};
	v.turn = rotateUfo;

	v.bounds = 300;

	return v;
}

Vehicle wave() {
	Vehicle v;
	v.type = VehicleType::Wave;
	v.enter = +[](Player& p, VehicleType from) {
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
	};
	v.turn = +[](Player& p) { rotateFly(p, p.small ? 0.4 : 0.25); };

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

double stepAcceleration(Player const& p) {
	switch (p.vehicle.type) {
		case VehicleType::Cube: {
			// m_gravity per speed (updateTimeMod)
			static float const gravity[] = {0.940199f, 0.958199f, 0.957199f, 0.961199f, 0.961199f};
			return gdFall(p, gravity[p.speed], 1);
		}
		// A ball falls with GD's fixed 0.958199 (not m_gravity), times 0.6
		case VehicleType::Ball: return gdFall(p, 0.958199f, 0.6f);
		case VehicleType::Ship: return shipAcceleration(p);
		case VehicleType::Ufo: return ufoAcceleration(p);
		default: return 0;
	}
}
