#include <Player.hpp>
#include <Level.hpp>
#include <Slope.hpp>
#include <cmath>
#include <climits>

Entity Player::innerHitbox() const {
	return {pos, Vec2D{9, 9}, 0};
}

Entity Player::unrotatedHitbox() const {
	return {pos, size, 0};
}

void Player::setVelocity(double v, bool override) {
	velocityOverride = override;

	// Being small commonly means velocity is 4/5 the original velocity.
	velocity = v * (small ? 0.8 : 1);

	if (v != 0)
		grounded = false;
}

void Player::applyResize() {
	if (resizePending) {
		// From the base size, like GD's scale (0.6 or exactly 1), so mini and back doesn't drift in float
		if (vehicle.type == VehicleType::Wave)
			size = small ? Vec2D(6, 6) : Vec2D(10, 10);
		else
			size = Vec2D(30, 30) * (small ? 0.6 : 1);
		resizePending = false;
	}
}

std::string Player::modeString() const {
	std::string mode(1, "csbuw"[(int)vehicle.type]);
	if (upsideDown) mode += 'u';
	if (small) mode += 'm';
	if (grounded) mode += 'g';
	return mode;
}

Player const& Player::prevPlayer() const {
	return level->getState(frame - 1);
}

Player const* Player::nextPlayer() const {
	return level->currentFrame() <= frame ? nullptr : &level->getState(frame + 1);
}

/**
 * In Geometry Dash, velocity is stored as 1/54 of distance per second.
 * It is also rounded to the nearest hundredth after (almost) every operation.
 * This function accounts for that rounding
 */
double roundVel(double velocity, bool upsideDown) {
	double nVel = velocity / 54.0 * (upsideDown * 2 - 1);
	double floored = (int)nVel;
	if (nVel != floored) {
		nVel = (double)std::round((nVel - floored) * 1000.0) / 1000.0 + floored;
	}
	return nVel * 54.0 * (upsideDown * 2 - 1);
}

void Player::moveY(double vel) {
	// PlayerObject::update moves by m_yVelocity (vel / 54) times the step's dt (0.25 at 240 tps) * 0.9f, both
	// floats, and adds that as a CCPoint to GD's player y: a float 90 higher (its ground is at 90), which rounds
	// coarser than ours.
	float step = (float)(dt * 60) * 0.9f;
	pos.y = ((pos.y + 90.f) + (float)(vel / 54.0 * (double)step)) - 90.f;
}

float Player::flipValueAt(int f) const {
	if (flip.start == 0 || f < flip.start)
		return flip.from;
	float t = std::min(1.f, (f - flip.start + 1) / 96.f);
	float eased = t < 0.5f ? 0.5f * std::pow(2 * t, 1.2f) : 1 - 0.5f * std::pow(2 - 2 * t, 1.2f);
	return t >= 1 ? flip.to : flip.from + (flip.to - flip.from) * eased;
}

float Player::spriteShift(float x) const {
	// PlayLayer::screenFlipObject only runs mid-flip
	if (flip.value == 0 || flip.value == 1)
		return 0;
	float amount = flip.fromMirrored ? 1 - flip.value : flip.value;
	float fromCamera = x - flip.camera;
	return (level->screenWidth - (fromCamera + fromCamera)) * amount;
}

void Player::preCollision(bool pressed) {
	// GD updates sprites (updateVisibility) between steps, with the camera and flip of the last step. The
	// camera keeps the player screenWidth / 2 - 75 from its left edge.
	flip.camera = pos.x - (level->screenWidth / 2 - 75);
	flip.value = flipValueAt(frame);
	if (flip.start != 0 && flip.value == flip.to)
		flip.fromMirrored = flip.to == 1;

	// PlayerObject::update adds the step as a CCPoint, so it is rounded to a float first
	pos.x += (float)(player_speeds[(int)speed] * dt);
	moveY(grav(velocity));

	frame++;
	timeElapsed += dt;
	grounded = false;
	velocityOverride = false;
	gravityPortal = false;
	hitPad = false;
	clickedOrb = false;
	onBlock = false;
	headHit = false;
	walkedOff = false;
	scaleRefreshed = false;
	portalVelocityScale = 1;
	roundVelocity = true;

	if (button != pressed) {
		button = pressed;
		input = button;
		buffer = button;
	}

	for (auto& i : actions)
		i(*this);
	actions.clear();

	// GJBaseGameLayer::checkCollisions starts the frame's slope state: last frame's slope, and the slopes
	// blocks have to look out for (m_potentialSlopeMap)
	slope.was = slope.on;
	slope.on = false;
	if (slope.keepPotential)
		slope.keepPotential = false;
	else
		slope.potential = nullptr;
	potentialSlopes.clear();
	if (slope.potential)
		potentialSlopes.push_back(slope.potential);
	if (slope.current && slope.current != slope.potential)
		potentialSlopes.push_back(slope.current);

	// GJBaseGameLayer::checkCollisions puts a player below the mode floor back onto it before object
	// collisions (postCollision then decides about grounding)
	floorSnapped = gravBottom(*this) < gravFloor();
	if (floorSnapped)
		pos.y = grav(gravFloor()) + grav(size.y / 2);

	// ...and a ship or ufo above the ceiling back under it, stopping it (the vehicle's clamp then adds the
	// gravity of the next step), so portals touched this frame start from there
	ceilingSnapped = (vehicle.type == VehicleType::Ship || vehicle.type == VehicleType::Ufo) && gravTop(*this) > gravCeiling();
	if (ceilingSnapped) {
		pos.y = grav(gravCeiling()) - grav(size.y / 2);
		if (velocity > 0)
			velocity = 0;
	}

	// A flying player (or ball) put onto the mode's floor or ceiling is off any slope
	if ((floorSnapped || ceilingSnapped) && vehicle.type != VehicleType::Cube)
		slope.was = false;
}

void Player::flipGravity(bool to) {
	if (to != upsideDown && (slope.was || slope.on))
		slope.downhill = !slope.downhill;
	upsideDown = to;
}

void Player::setGdVelocity(double v) {
	double whole = (int)v;
	if (v != whole)
		v = std::round((v - whole) * 1000.0) / 1000.0 + whole;
	velocity = grav(v * 54);
}

/// Seconds on the current slope, GD's m_totalTime - m_slopeStartTime, scaled for the launch: 0.4 to 1
/// over the first 0.1 s. GD's updateJump runs a step after this frame's collisions (nextStep).
static double slopeTimeFactor(Player const& p, bool nextStep) {
	double t = p.timeElapsed + (nextStep ? p.dt : 0) - p.slope.startTime;
	if (t < (double)0.1f)
		return std::max((double)0.4f, t * 10);
	return 1;
}

void Player::leaveSlope() {
	// PlayerObject::postCollision, on the frame after the last slope collision. GD's button state for
	// this frame is the last frame's (see Slope.cpp)
	bool held = vehicle.type == VehicleType::Ball ? prevPlayer().input : prevPlayer().button;
	double v = gdVelocity();
	float sv = slope.velocity;
	bool launches = (sv < 0 && sv < v && slope.ceiling) || (0 < sv && v < sv && !slope.ceiling);
	bool flyOrBall = vehicle.type != VehicleType::Cube;
	if ((!boosted || flyOrBall) && launches) {
		// Launched at the slope's speed (less in its first 0.1 s) if that's faster: boostPlayer
		float b = (float)((double)sv * slopeTimeFactor(*this, false));
		bool fire;
		if (!upsideDown)
			fire = (!slope.ceiling || !slope.downhill) ? v < b : b < v;
		else
			fire = (slope.ceiling || !slope.downhill) ? b < v : v < b;
		if (fire) {
			setGdVelocity(b);
			velocityOverride = false;
			grounded = false;
			boosted = true;
		}
	} else if (!upsideDown ? sv < 0 : 0 < sv) {
		// Off the bottom of a downhill slope: keeps falling at its speed
		if (!boosted && !held && slope.ceiling == upsideDown) {
			setGdVelocity((upsideDown ? -1 : 1) * -slope.yVelocity);
			velocityOverride = false;
			grounded = false;
		}
	}
	slope.yVelocity = 0;
	slope.velocity = 0;
	slope.rotation = 0;
	slope.current = nullptr;
}

void Player::slopeJump(float share) {
	// PlayerObject::updateJump: jumping from a slope that would launch the player adds part of the
	// launch, up to 1.4 times the jump
	if (!(slope.was || slope.on) || !(0 < grav(slope.velocity)))
		return;
	setGdVelocity(gdVelocity());
	double v = gdVelocity();
	float cap = (float)(v * 1.399999976158142);
	float add = vehicle.type == VehicleType::Ball ? slope.velocity : (float)((double)slope.velocity * slopeTimeFactor(*this, true));
	setGdVelocity((double)(add * share) + v);
	double nv = gdVelocity();
	setGdVelocity(!upsideDown ? std::min(nv, (double)cap) : std::max(nv, (double)cap));
}

void Player::postCollision() {
	// GD snaps to the floor before object collisions (see preCollision), so a pad touched this frame still
	// gets snapped (but launches the player rather than grounding them), and a block that stopped the
	// player's head after that can leave it below the floor (still grounded)
	if (gravBottom(*this) <= gravFloor() && !velocityOverride && (velocity <= 0 || hitPad)) {
		if (!headHit)
			pos.y = grav(gravFloor()) + grav(size.y / 2);
		if (!hitPad) {
			grounded = true;
			snapData.playerFrame = 0;
		}
	}

	// After GD's floor snap, which runs before object collisions
	applyResize();

	if (slope.was && !slope.on)
		leaveSlope();

	// Fell through ceiling, or hit floor
	if (pos.y > 1476.3 || (upsideDown && getBottom() < floor)) {
		dead = true;
		return;
	}

	// Coyote frames 
	if (prevPlayer().gravBottom(*this) > prevPlayer().gravFloor() && upsideDown == prevPlayer().upsideDown && !grounded && velocity <= 0) {
		if (prevPlayer().grounded && !prevPlayer().input)
			coyoteFrames = 0;
		coyoteFrames++;
	} else {
		// Nothing will check for coyote frames this high
		coyoteFrames = INT_MAX;
	}

	// An upside down cube that just left a block can still jump (see cube update), but GD moves it
	// off the block first
	bool coyoteJump = vehicle.type == VehicleType::Cube && !grounded && upsideDown && input && coyoteFrames < 10;
	bool ufoJump = vehicle.type == VehicleType::Ufo && buffer;
	bool groundedBefore = grounded;
	bool boostedBefore = boosted;

	vehicle.update(*this);

	// A ufo jumping as it walks off a block (or a mini ufo, which starts above the floor, pressed on the
	// level's first frame) still takes GD's walk-off step: GD walks off one step and jumps on the next
	if (ufoJump && velocityOverride && !groundedBefore && prevPlayer().grounded
		&& prevPlayer().velocity == 0 && prevPlayer().gravBottom(*this) > prevPlayer().gravFloor()) {
		double fallAccel = gdFall(*this, 0.958199f, 0.4f, small ? 0.85f : 1); // ufoAcceleration at velocity 0
		moveY(roundVel(prevPlayer().grav(fallAccel) * dt, prevPlayer().upsideDown));
	}

	if (!velocityOverride) {
		double newVel = velocity + acceleration * dt;

		// Player will fall off blocks a frame faster than expected. A grounded ship, ufo or ball flipped by a
		// gravity portal stays put for that frame (GD flips it after the move). A ship pressed this frame still
		// walks off first (unless an orb or pad launched it), since GD handles the press one step later;
		// so does a player clicking an orb, or a ball clicking as it leaves a block.
		bool shipPress = vehicle.type == VehicleType::Ship && button && !prevPlayer().button && velocity == 0;
		double shipFall = small ? -1051.8984 : -894.11464;
		bool ballPress = vehicle.type == VehicleType::Ball && button && !prevPlayer().button && !onBlock && !clickedOrb;
		if (!grounded && prevPlayer().grounded && ((!input && (prevPlayer().button || !button)) || buffer || (clickedOrb && !onBlock && (upsideDown == prevPlayer().upsideDown || vehicle.type == VehicleType::Ball)) || coyoteJump || shipPress || ballPress) && prevPlayer().gravBottom(*this) > prevPlayer().gravFloor() && prevPlayer().velocity == 0 && !(gravityPortal && vehicle.type != VehicleType::Cube)) {
			// The level's first frame has no previous acceleration. A ufo's previous acceleration was
			// chosen for its velocity before landing; GD falls with the one for velocity 0 (this frame's).
			double fallAccel = prevPlayer().acceleration;
			if (fallAccel == 0 || vehicle.type == VehicleType::Ufo)
				fallAccel = shipPress ? shipFall : acceleration;
			if (!walkedOff)
				moveY(roundVel(prevPlayer().grav(fallAccel) * dt, prevPlayer().upsideDown));

			// GD already had this frame's velocity (one step of gravity) when it touched a gravity
			// portal, which multiplies it by -0.5 (flipGravity)
			if (velocity == 0) {
				double fallVel = roundVel(fallAccel * dt, upsideDown);
				// ...and a vehicle portal scales it (toggleFlyMode etc.)
				newVel += (gravityPortal ? fallVel * -0.5 : fallVel) * portalVelocityScale;
			}
		} else if (shipPress && grounded && prevPlayer().acceleration == 0) {
			// On the level's first frame GD snaps a ship onto the floor but keeps its step of falling velocity
			newVel += roundVel(shipFall * dt, upsideDown);
		}
		velocity = newVel;
	}

	// GD rounds velocity to 1/1000 (of 1/54 units) after applying gravity; see roundVel
	if (roundVelocity)
		velocity = roundVel(velocity, upsideDown);

	// m_maybeIsBoosted ends when GD's updateJump finds the player falling (playerIsFallingBugged: y
	// velocity below twice m_gravity, with the wrong sign upside down), except on the step of a jump
	if (boosted && boostedBefore) {
		static double const gravity[5] = {0.940199, 0.9581990242004395, 0.957199, 0.961199, 0.961199};
		double g = gravity[speed], v = gdVelocity();
		if (!upsideDown ? v < (double)((float)g + (float)g) : g + g < v)
			boosted = false;
	}

	// Ensure the player hasn't gone beyond the bounds of the vehicle
	vehicle.clamp(*this);
}

Player::Player() :
	Entity({{0, 15}, {30, 30}, 0}), frame(1), timeElapsed(0), dead(false),
	vehicle(Vehicle::from(VehicleType::Cube)),
	ceiling(999999), floor(0), grounded(true),
	coyoteFrames(0), acceleration(0), velocity(0),
	velocityOverride(false), button(false), input(false),
	vehicleBuffer(false), upsideDown(false), small(false),
	speed(1), hitPad(false), roundVelocity(true) {}







