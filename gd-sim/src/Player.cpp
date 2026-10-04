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
	return level->getState(frame - 1, isP2);
}

Player const* Player::nextPlayer() const {
	return level->currentFrame() <= frame ? nullptr : &level->getState(frame + 1, isP2);
}

/// GJBaseGameLayer::getGroundHeightForMode: 300 for the ship, ufo, wave (and swing), 240 for the ball, 270
/// otherwise (which outside dual mode means no ground)
static float groundHeight(VehicleType t) {
	switch (t) {
		case VehicleType::Ship: case VehicleType::Ufo: case VehicleType::Wave: return 300;
		case VehicleType::Ball: return 240;
		default: return 270;
	}
}

bool Player::changedWithin(double seconds) const {
	int steps = (int)std::ceil(seconds / dt - 1e-9);
	Player const* a = this;
	for (int i = 1; i < steps; ++i) {
		auto const& b = level->getState(frame - i, isP2);
		if (a->upsideDown != b.upsideDown || a->vehicle.type != b.vehicle.type)
			return true;
		a = &b;
	}
	return false;
}

bool Player::fallingBugged() const {
	// m_gravity per speed (updateTimeMod). Not upside down the test uses it as a float, and in dual mode (m_unkA99,
	// set on both players by toggleDualMode) negated, so there it is the same as upside down.
	static double const gravity[5] = {0.940199, 0.9581990242004395, 0.957199, 0.961199, 0.961199};
	double g = gravity[speed], v = gdVelocity();
	if (upsideDown)
		return g + g < v;
	float gf = (float)(dual ? -g : g);
	return v < (double)(gf + gf);
}

void Player::copyAttributes(Player const& other) {
	float rot = rotation, rotSpeed = rotationSpeed, rotMult = rotateSpeed;
	bool rotating = isRotating, ball = ballRotating, ball2 = ballRotating2;
	bool p2 = isP2;
	Player* other2 = partner;
	*this = other;
	rotation = rot;
	rotationSpeed = rotSpeed;
	rotateSpeed = rotMult;
	isRotating = rotating;
	ballRotating = ball;
	ballRotating2 = ball2;
	pendingSpin.reset();
	isP2 = p2;
	partner = other2;
}

void Player::startDual() {
	velocityOverride = false;
	coyoteFrames = INT_MAX;
	vehicle.update(*this);
	if (!velocityOverride)
		velocity += acceleration * dt;
	velocity = roundVel(velocity, upsideDown);
	// (updateJump: a cube falling faster than 0.25 starts spinning)
	if (vehicle.type == VehicleType::Cube && !isRotating && gdVelocity() * (upsideDown ? -1 : 1) < -0.25) {
		stopRotation();
		runNormalRotation();
	}
	vehicle.clamp(*this);
}

void Player::updateGround(float portalY) {
	float h = groundHeight(vehicle.type);
	if (dual && partner) {
		// The larger of the two modes' heights (a ball's own counts as 270), centred on the dual portal
		h = std::max(h == 240 ? 270.f : h, groundHeight(partner->vehicle.type));
		portalY = dualPortalY;
	} else if (h == 270) {
		floor = 0;
		ceiling = vehicle.bounds;
		return;
	}
	// animateInDualGroundNew: the bottom is the portal's y minus half the height, rounded down to 30 in GD's
	// coordinates (90 higher), and at least GD's ground at 90
	float bottom = std::floor((portalY + 90.f - h * 0.5f) / 30.f) * 30.f;
	floor = std::max(0., (double)bottom - 90.);
	ceiling = floor + h;
	if (dual && partner) {
		partner->floor = floor;
		partner->ceiling = ceiling;
	}
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
		if (pressed)
			clickUsedByOrb = false;
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

	// An upside down cube below the ground dies, unless it flipped gravity within 0.1 s (m_lastFlipTime):
	// then GD puts it back on and stops it, and hitGround sets the on-ground flag if it was slow enough
	// (5, see the ball)
	if (vehicle.type == VehicleType::Cube && upsideDown && getBottom() < floor && !dual) {
		if (lastFlipFrame <= 0 || (frame - lastFlipFrame) * dt >= 0.1 - 1e-9) {
			dead = true;
			return;
		}
		pos.y = floor + size.y / 2;
		if (velocity <= 270)
			grounded = true;
		if (velocity > 0)
			velocity = 0;
	}

	// In dual mode GJBaseGameLayer::checkCollisions bounds every mode like a flying one: a cube past the
	// ceiling (relative to its gravity) dies, unless gravity or the mode changed within 0.2 s, which puts it
	// back under it, stopped and off the ground
	if (dual && vehicle.type == VehicleType::Cube && gravTop(*this) > gravCeiling()) {
		if (!changedWithin(0.2)) {
			dead = true;
			return;
		}
		pos.y = grav(gravCeiling()) - grav(size.y / 2);
		velocity = 0;
		onGround2 = false;
	}

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
	bool flips = to != upsideDown;
	if (flips)
		lastFlipFrame = frame;
	upsideDown = to;
	// ...and spins a ball the new way round
	if (flips && vehicle.type == VehicleType::Ball) {
		stopRotation();
		runBallRotation2();
	}
}

void Player::stopRotation() {
	isRotating = ballRotating = ballRotating2 = false;
	rotationSpeed = 0;
}

void Player::runNormalRotation(float mult) {
	if (vehicle.type != VehicleType::Cube && vehicle.type != VehicleType::Ball)
		return;
	isRotating = true;
	rotationSpeed = (float)(upsideDown ? -180 : 180) * mult / (small ? 0.33333334f : 0.43333334f);
}

/// runBallRotation's factor per speed (m_playerSpeed 0.7 0.9 1.1 1.3 1.6)
static float const ballSpeedFactor[5] = {1.2405637502670288f, 1, 0.8042434453964233f, 0.6657692790031433f, 0.5409374833106995f};

void Player::runBallRotation(float speedMult) {
	ballRotating = speedMult != 1;
	rotateSpeed = speedMult;
	float size = small ? 0.8f : 1;
	rotationSpeed = (float)(upsideDown ? -120 : 120) / (size * 0.2f * ballSpeedFactor[speed]);
	isRotating = true;
}

void Player::runBallRotation2() {
	float size = small ? 0.8f : 1;
	ballRotating2 = true;
	rotationSpeed = (float)((double)(float)(upsideDown ? 340 : -340) / ((double)size * 0.8 * (double)ballSpeedFactor[speed]));
}

float Player::closestRotation(float target) const {
	// PlayerObject::convertToClosestRotation: the angle = target (mod 90) nearest the current rotation
	if (vehicle.type != VehicleType::Cube && vehicle.type != VehicleType::Ball)
		return target;
	float current = (float)((int)rotation % 360);
	float below = target;
	if (target <= current) {
		for (; target < current; target += 90)
			below = target;
	} else {
		do {
			below = target;
			target = below - 90;
		} while (current < target);
	}
	return std::abs(current - below) <= std::abs(current - target) ? below : target;
}

void Player::easeRotation(float target) {
	// PlayerObject::updateRotation(dt, target): slerp towards it by m_playerSpeed * 0.175 of a step (3x for a
	// cube on the ground, 0.5x for a ship), at most the whole step
	float t = (float)(dt * 60);
	float rate = player_speedmults[speed] * 0.175f;
	if (vehicle.type == VehicleType::Ship)
		rate *= 0.5f;
	else if (vehicle.type == VehicleType::Cube && onGround2 && !slope.on && !slope.was)
		rate *= 3;
	if (rate * t <= t)
		t = rate * t;
	rotation = slerp(rotation * 0.017453292f, target * 0.017453292f, t) * 57.29578f;
}

void Player::hitGroundRotation() {
	// A cube stops spinning; a ball stops its flip spin (and a faster roll, off a slope) and rolls
	onGround2 = true;
	if (vehicle.type == VehicleType::Cube) {
		if (isRotating || slope.was)
			stopRotation();
	} else if (vehicle.type == VehicleType::Ball) {
		if (ballRotating && !slope.on)
			stopRotation();
		if (ballRotating2)
			stopRotation();
		if (!isRotating) {
			stopRotation();
			runBallRotation();
		}
	}
}

void Player::updateRotation() {
	if (vehicle.type != VehicleType::Cube && vehicle.type != VehicleType::Ball) {
		pendingSpin.reset();
		return;
	}

	// PlayerObject::hitGround runs every step on the ground, before this
	if (grounded)
		hitGroundRotation();

	// PlayerObject::updateRotation(dt): a cube eases to the slope it's on, or to the nearest 90 degrees on the
	// ground (m_isOnGround2, unless falling); otherwise the spin goes on
	if (vehicle.type == VehicleType::Cube) {
		if (slope.on) {
			easeRotation(closestRotation((float)(slope.rotation * 57.295780181884766)));
		} else if (onGround2 && !slope.was && (grounded || (upsideDown ? gdVelocity() <= 0.25 : gdVelocity() >= -0.25))) {
			// (A landed player isn't rising: the landing stopped it, though the sim only zeroes the velocity
			// in the vehicle's update, after this)
			stopRotation();
			easeRotation(closestRotation(0));
		}
	}
	if (rotationSpeed != 0) {
		float mult = ballRotating && vehicle.type == VehicleType::Ball ? rotateSpeed : 1;
		rotation = rotation + ((float)(dt * 60) / 60.0f) * rotationSpeed * mult;
	}

	if (pendingSpin) {
		rotationSpeed = pendingSpin->speed;
		rotateSpeed = pendingSpin->rotateSpeed;
		isRotating = pendingSpin->isRotating;
		ballRotating = pendingSpin->ballRotating;
		ballRotating2 = pendingSpin->ballRotating2;
		pendingSpin.reset();
	}
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
			if (grounded)
				walkedOff = true; // (as below)
			setGdVelocity(b);
			velocityOverride = false;
			grounded = false;
			boosted = true;
			accelerating = true;
			onGround2 = false;
			// ...which spins a cube the other way round, a little faster
			if (vehicle.type == VehicleType::Cube) {
				ballRotating = ballRotating2 = false;
				rotationSpeed = (float)(upsideDown ? 180 : -180) / (small ? 0.6666667f : 0.8666667f);
				isRotating = true;
			}
		}
	} else if (!upsideDown ? sv < 0 : 0 < sv) {
		// Off the bottom of a downhill slope: keeps falling at its speed
		if (!boosted && !held && slope.ceiling == upsideDown) {
			// A player that landed on a block this step stays there for it: no walk-off step (see postCollision)
			if (grounded)
				walkedOff = true;
			setGdVelocity((upsideDown ? -1 : 1) * -slope.yVelocity);
			velocityOverride = false;
			grounded = false;
			onGround2 = false;
			stopRotation();
			if (vehicle.type == VehicleType::Ball)
				runBallRotation();
			else
				runNormalRotation();
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
	if (pos.y > 1476.3 || (upsideDown && getBottom() < floor && vehicle.type != VehicleType::Cube)) {
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

	// GJBaseGameLayer::update turns the player after the step's collisions (before the next step's updateJump)
	updateRotation();

	// PlayerObject::updateJump first ends m_isAccelerating for a ship, ufo or wave whose velocity is
	// within the ship's caps: [0, 8) up or (-6.4, 0] down (/ 0.85 when mini)
	if (vehicle.type == VehicleType::Ship || vehicle.type == VehicleType::Ufo || vehicle.type == VehicleType::Wave) {
		float size = small ? 0.85f : 1;
		double v = velocity / 54;
		if ((v >= 0 && v < (double)(8.0f / size)) || (v <= 0 && v > (double)(-6.4f / size)))
			accelerating = false;
	}

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
		bool ballPress = vehicle.type == VehicleType::Ball && button && !prevPlayer().button && !onBlock && !clickedOrb && !slope.on;
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

	// PlayerObject::updateJump: a cube that starts falling (faster than 0.25) starts spinning, and one
	// falling faster than 4 is off the ground
	double fall = gdVelocity() * (upsideDown ? -1 : 1);
	if (vehicle.type == VehicleType::Cube && !isRotating && !slope.on && fall < -0.25) {
		stopRotation();
		runNormalRotation();
	}
	if ((vehicle.type == VehicleType::Cube || vehicle.type == VehicleType::Ball) && fall < -4)
		onGround2 = false;

	// m_maybeIsBoosted ends when GD's updateJump finds the player falling (playerIsFallingBugged: y
	// velocity below twice m_gravity, with the wrong sign upside down), except on the step of a jump
	if (boosted && boostedBefore && fallingBugged())
		boosted = false;

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







