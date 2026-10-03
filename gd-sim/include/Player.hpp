#pragma once

#undef small
#include <util.hpp>
#include <Vehicle.hpp>
#include <Slope.hpp>
#include <vector>
#include <functional>
#include <optional>


/// Player X velocity per speed
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

	// Potential slopes are important for block collisions. Reset per frame.
	std::vector<Slope const*> potentialSlopes;

	/// Actions will be ran at the beginning of every frame.
	std::vector<std::function<void(Player&)>> actions;

	/// Slopes have special collision rules. See Slope.cpp for more information.
	struct {
		std::optional<Slope> slope;

		/// Time on slope
		double elapsed;

		/// When colliding with a downhill slope with a positive velocity.
		bool snapDown;
	} slopeData;

	/// X-snapping. See Block.cpp for more information
	struct {
		Entity object;
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

	/// A pad was touched this frame (pads, unlike orbs, don't snap the player onto a block)
	bool hitPad;

	/// An orb was clicked this frame
	bool clickedOrb = false;

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

	/// Move by dy, rounding y like GD does
	void moveY(double dy);

	/// Apply a size portal's size change if it hasn't been yet (see resizePending)
	void applyResize();

	/// Vehicle letter (c s b u w) + u upside down, m mini, g grounded. Used in debug traces.
	std::string modeString() const;
};