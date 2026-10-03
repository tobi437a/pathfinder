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

void Player::moveY(double dy) {
	// GD's player y is a float 90 higher (its ground is at 90), which rounds coarser than ours
	pos.y = (float)((double)(pos.y + 90.f) + dy) - 90.f;
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

	pos.x += player_speeds[(int)speed] * dt;
	moveY(grav(velocity) * dt);

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
	roundVelocity = true;

	if (button != pressed) {
		button = pressed;
		input = button;
		buffer = button;
	}

	for (auto& i : actions)
		i(*this);
	actions.clear();

	// GJBaseGameLayer::checkCollisions puts a player below the mode floor back onto it before object
	// collisions (postCollision then decides about grounding)
	floorSnapped = gravBottom(*this) < gravFloor();
	if (floorSnapped)
		pos.y = grav(gravFloor()) + grav(size.y / 2);

	potentialSlopes.clear();

	// Downhill slopes snap you automatically
	if (slopeData.slope && slopeData.slope->gravOrient(*this) == 1) {
		grounded = true;
	}
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

	vehicle.update(*this);

	// A ufo jumping as it walks off a block (or a mini ufo, which starts above the floor, pressed on the
	// level's first frame) still takes GD's walk-off step: GD walks off one step and jumps on the next
	if (ufoJump && velocityOverride && !groundedBefore && prevPlayer().grounded
		&& prevPlayer().velocity == 0 && prevPlayer().gravBottom(*this) > prevPlayer().gravFloor()) {
		double fallAccel = small ? -1308.96 : -1114.56; // ufoAcceleration at velocity 0
		moveY(roundVel(prevPlayer().grav(fallAccel) * dt, prevPlayer().upsideDown) * dt);
	}

	if (!velocityOverride) {
		double newVel = velocity + acceleration * dt;

		// Player will fall off blocks a frame faster than expected. A grounded ship or ufo flipped by a
		// gravity portal stays put for that frame (GD flips it after the move). A ship pressed this frame still
		// walks off first (unless an orb or pad launched it), since GD handles the press one step later;
		// so does a player clicking an orb, or a ball clicking as it leaves a block.
		bool shipPress = vehicle.type == VehicleType::Ship && button && !prevPlayer().button && velocity == 0;
		double shipFall = small ? -1051.8984 : -894.11464;
		bool ballPress = vehicle.type == VehicleType::Ball && button && !prevPlayer().button && !onBlock && !clickedOrb;
		if (!grounded && prevPlayer().grounded && ((!input && (prevPlayer().button || !button)) || buffer || (clickedOrb && !onBlock && upsideDown == prevPlayer().upsideDown) || coyoteJump || shipPress || ballPress) && prevPlayer().gravBottom(*this) > prevPlayer().gravFloor() && prevPlayer().velocity == 0 && !(gravityPortal && (vehicle.type == VehicleType::Ship || vehicle.type == VehicleType::Ufo))) {
			// The level's first frame has no previous acceleration. A ufo's previous acceleration was
			// chosen for its velocity before landing; GD falls with the one for velocity 0 (this frame's).
			double fallAccel = prevPlayer().acceleration;
			if (fallAccel == 0 || vehicle.type == VehicleType::Ufo)
				fallAccel = shipPress ? shipFall : acceleration;
			if (!walkedOff)
				moveY(roundVel(prevPlayer().grav(fallAccel) * dt, prevPlayer().upsideDown) * dt);

			// GD already had this frame's velocity (one step of gravity) when it touched a gravity
			// portal, which multiplies it by -0.5 (flipGravity)
			if (velocity == 0) {
				double fallVel = roundVel(fallAccel * dt, upsideDown);
				newVel += gravityPortal ? fallVel * -0.5 : fallVel;
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

	if (slopeData.slope)
		slopeData.slope->calc(*this);

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
	speed(1), slopeData({{}, 0, false}), hitPad(false), roundVelocity(true) {}







