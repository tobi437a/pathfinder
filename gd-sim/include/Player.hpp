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
	/// An orb's new rotation, which starts after this frame's updateRotation: GD clicks the ring a step
	/// after the sim (see Orb::collide)
	struct Spin { float speed, rotateSpeed; bool isRotating, ballRotating, ballRotating2; };
	std::optional<Spin> pendingSpin;

	/// A pad was touched this frame (pads, unlike orbs, don't snap the player onto a block)
	bool hitPad;

	/// An orb was clicked this frame
	bool clickedOrb = false;
	/// The current click (button still held) was used by an orb
	bool clickUsedByOrb = false;

	/// The player was snapped onto a block top this frame (even if an orb click keeps it ungrounded)
	bool onBlock = false;

	/// A block stopped the player's head this frame
	bool headHit = false;

	/// The player was below the mode floor after moving and was put back onto it before collisions
	bool floorSnapped = false;
	/// A ship or ufo above the mode ceiling after moving was put back under it before collisions
	bool ceilingSnapped = false;

	/// GD's walk-off step (a step of gravity for a player leaving a block) was already applied this frame
	bool walkedOff = false;

	/// GD caches the player's hitbox, so a size portal's new size only shows once the player is moved
	/// (e.g. snapped onto a block) or at the end of the frame
	bool resizePending = false;
	/// GD's updatePlayerScale ran this frame (switching into or out of ship, ufo or wave), which marks the
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

	void setVelocity(double v, bool override=false);

	/// PlayerObject::flipGravity's slope part: a player on a slope flips which way is downhill
	void flipGravity(bool upsideDown);

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
	void updateRotation();

	/// Move y by a step of velocity `vel` (gravity-relative sign already applied), like PlayerObject::update
	void moveY(double vel);

	/// Apply a size portal's size change if it hasn't been yet (see resizePending)
	void applyResize();

	/// Vehicle letter (c s b u w) + u upside down, m mini, g grounded. Used in debug traces.
	std::string modeString() const;
};