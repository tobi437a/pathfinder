#pragma once

#undef small
#include <util.hpp>
#include <Vehicle.hpp>
#include <Slope.hpp>
#include <vector>
#include <functional>
#include <optional>


/// Player X velocity per speed: GD's m_playerSpeed (float) * m_speedMultiplier * 60
inline double player_speeds[5] = {
	251.16007972276924,
	311.580093712804,
	387.42014039710523,
	468.0001388338566,
	576.00020058307177
};

/// Used in player rotation. Similar to m_playerSpeed member variable
inline float player_speedmults[5] = {
	0.7,
	0.9,
	1.1,
	1.3,
	1.6	
};

double roundVel(double velocity, bool upsideDown);

/// Whether something `steps` steps ago counts as within `seconds` for GD's timers (m_lastFlipTime etc.)
bool stepsWithin(int steps, double seconds);

struct Object;
class Level;
struct Slope;

/**
 * The main player. This contains the entire player state and is the only thing that changes each frame.
 */
struct Player : public Entity {
	Vehicle vehicle;
	Level* level;

	double timeElapsed;
	double acceleration;
	double velocity;

	/// See util.hpp for what cow_set is
	cow_set<int> usedEffects;

	/// GD's m_potentialSlopeMap: slopes near the player this frame. A block whose top (or bottom) one of
	/// them covers isn't landed on (or hit with the head). Rebuilt every frame, see Player::preCollision.
	std::vector<Slope const*> potentialSlopes;

	/// Actions will be ran at the beginning of every frame.
	std::vector<std::function<void(Player&)>> actions;

	/// GD's slope state (PlayerObject members named in the comments). Velocities are in GD units
	/// (sim velocity / 54, positive = up in the world), y values in GD coordinates (sim y + 90).
	struct SlopeState {
		Slope const* current = nullptr;   // m_currentSlope
		Slope const* potential = nullptr; // m_currentPotentialSlope
		bool keepPotential = false;       // !m_unk669: keep `potential` for another frame
		bool on = false;                  // m_isOnSlope: a slope collision this frame
		bool was = false;                 // m_wasOnSlope: one last frame
		bool head = false;                // m_maybeUpsideDownSlope: the slope is on the player's head side
		bool ceiling = false;             // m_isCurrentSlopeTop: the slope's solid part is on top
		bool downhill = false;            // m_slopeFlipGravityRelated: downhill relative to gravity
		bool topClamped = false;          // m_unk3e0: put at the slope's high end
		bool bottomClamped = false;       // m_unk3e1: put at its low end
		int collidingId = -1;             // m_collidingWithSlopeId
		float velocity = 0;               // m_slopeVelocity: the launch speed leaving its top
		float angle = 0;                  // m_slopeAngleRadians
		double yVelocity = 0;             // m_currentSlopeYVelocity: rise per step along it
		double startTime = 0;             // m_slopeStartTime
		double rotation = 0;              // m_slopeRotation: signed angle (radians)
		double target = 0;                // m_unk3d0: the y the last slope collision put the player at
		double clipExtra = 0;             // unk_584: extra half height on the slope, widens block landing
	} slope;

	/// m_maybeIsBoosted: launched upwards (jump, pad, orb, slope) and not yet falling
	bool boosted = false;

	/// X-snapping. See Block.cpp for more information
	struct {
		Entity object;
		/// That block's Object::id (GD compares the unique ids: landing on the same block again doesn't snap)
		int objectId = -1;
		/// GD only snaps from a plain solid (GameObject type 0), not from a breakable block
		bool fromSolid = true;
		int playerFrame = 0;
		/// How far the block's sprite was from its hitbox then (see spriteShift)
		float spriteDx = 0;
		/// The player's x after that snap, for a snap from before Level::resume (playerFrame <= baseFrame)
		float playerX = 0;
	} snapData;

	/**
	 * Mirror portals (GJBaseGameLayer::toggleFlipped) tween GD's level flip from flipFrom to flipTo over
	 * 0.4 s (96 steps, ease in-out rate 1.2), starting with the step the portal is touched (frame flipStart).
	 * While it runs, PlayLayer::screenFlipObject moves every block's sprite towards its mirrored position;
	 * hitboxes stay put, but X-snapping reads sprite positions.
	 */
	struct {
		/// Target of the last mirror portal (GD ignores a portal that doesn't change it)
		bool mirrored = false;
		float from = 0, to = 0;
		int start = 0;
		/// After a completed flip to mirrored, GD measures the shift from the mirrored side (1 - flip)
		bool fromMirrored = false;
		/// Flip value and camera x GD moved the sprites with before this step's collisions (end of last step)
		float value = 0, camera = 0;
	} flip;

	/// Flip value at the end of the step with the given frame
	float flipValueAt(int frame) const;
	/// How far GD has moved the sprite of a block at x from x this step (see flip)
	float spriteShift(float x) const;

	float ceiling;
	float floor;
	float dt = 1 / 240.f;

	/// Some vehicles have coyote frames for valid inputs
	unsigned int coyoteFrames;

	int speed;
	int frame;

	bool dead;
	bool grounded;

	/**
	 * Under normal circumstances, acceleration is applied to the velocity
	 * at the end of a frame. When this field is set, acceleration will not
	 * be applied at the end of the frame. Mainly set via `setVelocity`
	 */
	bool velocityOverride;

	/**
	 * `button` vs `input`: Button always refers to whether a click was applied; input can be disabled
	 * for niche circumstances where no other normal operations are allowed despite a click.
	 */
	bool button, input;

	/// If a click is being buffered. Used for things like orb clicks.
	bool buffer = false;
	/**
	 * In the ball vehicle, holding a click while transitioning into another vehicle will cause a
	 * buffered input, despite things like orbs not buffering in the same way.
	 */
	bool vehicleBuffer;

	bool upsideDown;
	bool small;

	/// Entering a gravity portal can cause the next frame to have certain edge cases
	bool gravityPortal = false;
	/// Flipped this frame by entering the mode the other player is in (dual mode, VehiclePortal): like a gravity
	/// portal, it flips a grounded player in place
	bool modeFlip = false;
	/// Frame of the last gravity flip (PlayerObject::m_lastFlipTime), 0 if none
	int lastFlipFrame = 0;

	/// PlayerObject::m_isAccelerating: set by a black orb click and a slope launch
	/// (boostPlayer), cleared by a pad, and at the start of a ship /
	/// ufo / wave step whose velocity is within the ship's caps. While set, the ship and ufo velocity caps
	/// don't apply, and a falling ship that isn't held is pushed back up (shipAcceleration).
	bool accelerating = false;

	/// The cube's and ball's rotation (Entity::rotation, degrees, clockwise like GD), which rotated objects are
	/// tested against (Object::touching): PlayerObject's m_rotationSpeed (degrees per second, added each step
	/// by updateRotation), m_isRotating, m_isBallRotating / m_isBallRotating2 and m_rotateSpeed
	float rotationSpeed = 0;
	bool isRotating = false, ballRotating = false, ballRotating2 = false;
	float rotateSpeed = 1;
	/// PlayerObject::m_isOnGround2: set by landing (hitGround), cleared by a jump, pad, ring, slope launch or
	/// vehicle portal and by falling faster than 4 (GD units); a cube with it eases its rotation like on the
	/// ground (updateRotation), even rising after a gravity flip
	bool onGround2 = false;

	/// Dual mode (GJGameState::m_isDualMode), kept in both players' states. isP2 marks player 2's states
	/// (Level::gameStates2); partner is the other player while a frame runs (null otherwise). dualPortalY is
	/// the y of the dual portal the dual ground is centred on (m_lastActivatedPortal2).
	bool dual = false, isP2 = false;
	Player* partner = nullptr;
	float dualPortalY = 0;
	/// y of the last vehicle portal (m_lastActivatedPortal1), which the mode ground is centred on after dual mode
	float modePortalY = 0;
	/// Set the mode floor and ceiling (GJBaseGameLayer::updateDualGround / animateInDualGroundNew): in dual
	/// mode both players share the dual ground, centred on dualPortalY; otherwise the mode's, centred on portalY
	void updateGround(float portalY);
	/// PlayerObject::playerIsFallingBugged: y velocity below twice m_gravity, or upside down above minus that (so
	/// a player slowly rising counts as falling); in dual mode minus that either way. Picks the ship's and ufo's
	/// gravity and ends m_maybeIsBoosted.
	bool fallingBugged() const;
	/// The ufo's fall (ufoAcceleration) from velocity 0, e.g. a walk-off step: GD takes it before a slope launch
	/// or a pad changes the velocity later in the step
	double ufoRestFall() const;
	/// Gravity or vehicle changed within `seconds` (GD's m_lastFlipTime, m_gameModeChangedTime), see stepsWithin
	bool changedWithin(double seconds) const;
	/// Become a copy of `other` (PlayerObject::copyAttributes, toggleDualMode), keeping what GD doesn't copy: this
	/// player's rotation and spin. The held button is copied (m_jumpBuffered, which pushButton sets and
	/// releaseButton clears).
	void copyAttributes(Player const& other);
	void resetObject();
	/// PlayerObject::updateJump's first step: m_isAccelerating ends within the ship's velocity caps
	void endAccelerating();
	/// Player 2's start in the frame of the dual portal: GD steps it from the next step on, whose updateJump adds
	/// gravity before it moves; the sim does that at the end of this frame (like postCollision)
	void startDual();
	/// An orb's new rotation, which starts after this frame's updateRotation: GD clicks the ring a step
	/// after the sim (see Orb::collide)
	struct Spin { float speed, rotateSpeed; bool isRotating, ballRotating, ballRotating2; };
	std::optional<Spin> pendingSpin;
	/// The rotation state this frame's turn (updateRotation, after the landing part) started from: a flip by the other
	/// player after it, which GD has before both players turn, turns it again from there (flipPartner)
	struct TurnStart { int frame = -1; float rotation = 0; Spin spin {}; bool onGround2 = false; std::optional<Spin> pendingSpin; };
	TurnStart beforeTurn;
	/// GD's m_shipRotation: where the player was at its last turn (set after both players turned, player 2's only in
	/// dual mode; copied by copyAttributes), which a ship, ufo or wave turns towards its travel from. GD's (0, 0) before
	/// the level's first step.
	Vec2D shipRotation {0, -90};
	/// The rotation before this frame's fly turn (Vehicle::turn)
	float beforeFlyTurn = 0;
	/// updateRotation's turn: easing to the ground or slope, the spin, then a pending spin
	void turn();

	/// A pad was touched this frame (pads, unlike orbs, don't snap the player onto a block)
	bool hitPad;

	/// An orb was clicked this frame
	bool clickedOrb = false;
	/// The current click (button still held) was used by an orb or a jump (GD clears m_stateRingJump then, so
	/// a ring touched while holding doesn't take it)
	bool clickUsed = false;

	/// The player was snapped onto a block top this frame (even if an orb click keeps it ungrounded)
	bool onBlock = false;

	/// A block stopped the player's head this frame
	bool headHit = false;

	/// The player was below the mode floor after moving and was put back onto it before collisions
	bool floorSnapped = false;
	/// ...in dual mode in a step where the mode's top moved (GJBaseGameLayer::checkCollisions compares getMaxPortalY with
	/// the last step's): GD's snap calls hitGround, then puts the y velocity back, so the player isn't at rest
	bool boundKeptVelocity = false;
	/// Past the mode's bounds this frame (GD's m_isOutOfBounds); two frames in a row kill the player
	bool outOfBounds = false;
	/// The mode's floor and ceiling when this frame's collisions began: GD bounds the player before them, so a
	/// portal that moves the ground takes effect a step later (postCollision)
	float startFloor = 0, startCeiling = 0;
	/// The player's half height at GD's floor snap (before this step's portals)
	double startHalfHeight = 0;
	/// A ship or ufo above the mode ceiling after moving was put back under it before collisions
	bool ceilingSnapped = false;
	/// GD put this cube (in dual mode) back under the mode's ceiling and stopped it (Player::preCollision): after
	/// its walk-off step, which the sim then doesn't take
	bool dualCeilingPutBack = false;

	/// The velocity after this frame's collisions (GD's m_yVelocity until its next updateJump; the sim's velocity
	/// already has that step's gravity)
	double collisionVelocity = 0;
	/// ...and the gravity then (before the next step's updateJump, e.g. a ball's jump)
	bool collisionUpsideDown = false;

	/// GD's walk-off step (a step of gravity for a player leaving a block) was already applied this frame
	bool walkedOff = false;

	/// A speed portal touched this frame: GD's speed from its next step on (-1: none)
	int pendingSpeed = -1;

	/// GD's m_jumpBuffered: set by a press, cleared by the release, a ball's jump or an orb (until the next press)
	bool jumpBuffered = false;

	/// A fresh click took an orb off the ground this frame (GD's step still has the player on the ground)
	bool orbFromGround = false;

	/// A vehicle portal turned the player into a ball this frame
	bool becameBall = false;

	/// GD's velocity after that step on a slope (the sim's standing player has none; the slope zeroes it again)
	double slopeFallVelocity = 0;

	/// GD cleared m_isOnGround this frame (leaving a slope by a launch or off its bottom, a slope on the head
	/// side): no walk-off jump (coyote frames) after it
	bool groundCleared = false;

	/// A ball jumped (flipped) at the end of this frame: GD does that in its next step's updateJump, so its
	/// end-of-step checks still see the old gravity (Level::repelBalls)
	bool ballJumped = false;
	/// A boosted ball stopped at the mode's ceiling without jumping: GD's hitGround put it on the ground, and its
	/// updateJump only applies gravity while it's boosted (m_maybeIsBoosted), so it can still jump in the step after
	/// the boost ends
	bool ceilingGround = false;
	/// GD put this upside-down cube back on the ground it fell under after a flip (checkCollisions: hitGround, then
	/// m_isOnGround2 cleared): its landing rotation is done
	bool flipFloorPutBack = false;
	/// A slope's side (a block in the slopes' turn of the collisions, before the portals) landed the player this step
	bool slopeSideLanded = false;
	/// A speed portal (any speed) was taken this step
	bool tookSpeedPortal = false;
	/// A slope this frame turned the player's velocity upwards (a flying player held on an uphill slope)
	bool slopeRaised = false;
	/// The flags that jump cleared or set, for a repel that undoes it (GD never made that jump)
	struct {
		bool boosted, jumpBuffered, vehicleBuffer, clickUsed, grounded;
	} beforeBallJump{};
	/// A cube jumped at the end of this frame, which GD does in its next step's updateJump: in dual mode the other
	/// player's flip before that (flipPartner) clears m_isOnGround first, and GD never makes the jump. What the jump changed:
	bool cubeJumped = false;
	struct {
		bool boosted, clickUsed, buffer, onGround2, grounded;
		Spin spin;
	} beforeCubeJump{};
	/// The jump's velocity, before the step of gravity the sim adds with it: a fresh press makes GD's grounded cube
	/// jump in pushButton, at the start of its next step, so player 2's ring clicked by the same press (pushed after
	/// player 1) flips player 1 with that velocity (flipPartner)
	double cubeJumpVelocity = 0;

	/// GD caches the player's hitbox, so a size portal's new size only shows once the player is moved
	/// (e.g. snapped onto a block) or at the end of the frame
	bool resizePending = false;
	/// GD's updatePlayerScale ran this frame (switching into or out of ship, ufo or wave; or the ball off), which marks the
	/// hitbox dirty: a pending resize shows for the blocks (GD takes the player's rect once for the other
	/// objects)
	bool scaleRefreshed = false;
	/// What vehicle portals multiplied y velocity by this frame (GD halves it per ship/ufo/wave toggle)
	double portalVelocityScale = 1;

	/// Whether velocity at the end of the frame is to be rounded (typically used by ball)
	bool roundVelocity = true;

	Player();

	void preCollision(bool input);
	void postCollision();

	
	Entity unrotatedHitbox() const;

	/// Inner hitbox of {9, 9} that is used mainly for blocks
	Entity innerHitbox() const;

	Player const& prevPlayer() const;
	Player const* nextPlayer() const;

	/// Values relative to player gravity.
	template <typename T>
	T grav(T value) const { return upsideDown ? -value : value; }
	inline float gravBottom(Entity const& e) const { return upsideDown ? -e.getTop() : e.getBottom(); }
	inline float gravTop(Entity const& e) const { return upsideDown ? -e.getBottom() : e.getTop(); }
	inline float gravFloor() const { return upsideDown ? -ceiling : floor; }
	inline float gravCeiling() const { return upsideDown ? -floor : ceiling; }
	/// GJBaseGameLayer::checkCollisions doesn't put a player that was on a slope (m_wasOnSlope) back inside the
	/// mode's bounds once its centre is past the floor or ceiling (a flying mode or ball, or any mode in dual): it is
	/// out of bounds
	inline bool pastBoundsOnSlope(bool wasOnSlope) const {
		return wasOnSlope && (vehicle.type != VehicleType::Cube || dual) && (pos.y < floor || ceiling < pos.y);
	}

	void setVelocity(double v, bool override=false);

	/// PlayerObject::flipGravity's slope part: a player on a slope flips which way is downhill
	void flipGravity(bool upsideDown);
	/// GJBaseGameLayer::flipGravity's dual part, after this player's gravity portal, blue pad or blue/green orb
	/// flip: with both players in the same mode, the other one takes the opposite gravity
	/// One step of the current spin (updateRotation's part after the easing)
	float spinStep() const;
	/// (turned: the other player has turned this step already, as when player 2's collisions flip player 1)
	void flipPartner(bool turned = true);
	/// flipPartner at the start of the next frame: GD handles a fresh orb click there (pushButton), before either
	/// player moves, where the sim clicks the orb a frame earlier
	bool flipPartnerNext = false;
	/// The other player's flipPartnerNext will flip this one: in GD that flip (clearing m_isOnGround) comes before
	/// this player's updateJump in the step this frame's jump would be, so it can't jump (Level::runFrame)
	bool partnerFlipsNext = false;
	/// Whether flipPartner would flip the other player now
	bool flipsPartnerKind() const;
	bool flipsPartner() const;
	/// The other player's flip (flipPartner) flipped this one before its step this frame: GD has flipped it by
	/// then, so its blocks act in the new gravity
	bool partnerFlipped = false;
	/// Player 1 took player 2's place at a solo portal this frame (DualPortal::collide), with player 2's state
	/// from its collisions: GD steps it from there next step (Level::runFrame)
	bool copiedFromP2 = false;
	/// Where player 1 touched this frame's rings before player 2's solo portal moved it (copyAttributes): GD's
	/// m_touchingRings, which a press in its next step still tries (Orb::touching)
	std::optional<Vec2D> ringPos;
	/// A fresh click on a blue or green orb flipped gravity this frame, which GD only does at its next step
	/// (pushButton): this step's turn still has the old gravity (rotateUfo)
	bool orbFlipPending = false;

	/// GD's y velocity (m_yVelocity: GD units, positive = up in the world) and setYVelocity (rounds to 1/1000)
	double gdVelocity() const { return grav(velocity) / 54; }
	void setGdVelocity(double v);

	/// PlayerObject::postCollision on leaving a slope: the launch off its top, or the fall off its bottom
	void leaveSlope();
	/// PlayerObject::updateJump: part (share) of a slope's launch added to a jump from it
	void slopeJump(float share);

	/// PlayerObject's rotation: stop (the inline reset GD does before most of these), runNormalRotation (a
	/// cube's jump spin), runBallRotation (a ball's roll, `speed` times), runBallRotation2 (a ball's flip
	/// spin), convertToClosestRotation, updateRotation(dt, target) (ease towards `target`) and
	/// updateRotation(dt) (once per step, after collisions)
	void stopRotation();
	void runNormalRotation(float mult = 1);
	void runBallRotation(float speed = 1);
	void runBallRotation2();
	float closestRotation(float target) const;
	void easeRotation(float target);
	/// PlayerObject::updateSlopeRotation
	void slopeRotation();
	void updateRotation();
	/// PlayerObject::hitGround's part in the rotation (and m_isOnGround2)
	void hitGroundRotation();

	/// Move y by a step of velocity `vel` (gravity-relative sign already applied), like PlayerObject::update
	void moveY(double vel);

	/// Apply a size portal's size change if it hasn't been yet (see resizePending)
	void applyResize();

	/// Vehicle letter (c s b u w) + u upside down, m mini, g grounded. Used in debug traces.
	std::string modeString() const;
};
