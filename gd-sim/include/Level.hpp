#pragma once
#include <Object.hpp>
#include <Player.hpp>
#include <vector>


/**
 * In order to use the simulator, you must create a Level. The Level class is
 * the root class of everything else, containing both objects and player states,
 * as well as the main update function. See Level.cpp for implementation info.
 */
class Level {
	/// Called by constructor, applies level settings to the initial player state
	void initLevelSettings(std::string const& lvlSettings, Player& player);
 public:
 	/**
 	 * All player states are stored, including previous states. This way, Pathfinder
 	 * is able to seamlessly rewind when searching for solutions.
 	 */
	std::vector<Player> gameStates;
	/// Player 2's states, one per gameStates entry (a copy of player 1 while not in dual mode)
	std::vector<Player> gameStates2;

	/// Frames before gameStates[0] (which is frame baseFrame + 1). Non-zero after `resume`.
	int baseFrame = 0;

	size_t objectCount = 0;

	/// Sections are used just like real GD. See Object.hpp for more info on ObjectContainer.
	std::vector<std::vector<ObjectContainer>> sections;

	/// x at which the level is complete: the last object's x + 40 (see Level::Level)
	float length = 0.0;

	/// Two-player mode (level setting kA10, LevelSettingsObject 0x154): in dual mode each player gets only its own
	/// inputs (GJBaseGameLayer::handleButton), which the sim's single button is player 1's, and a gravity flip
	/// doesn't flip the other player (GJBaseGameLayer::flipGravity)
	bool twoPlayer = false;

	/// Per-frame scratch lists for runFrame, kept here so they don't reallocate every frame
	std::vector<ObjectContainer const*> blocks;
	std::vector<ObjectContainer const*> hazards;

 	static constexpr uint32_t sectionSize = 100;
 	/// GD's window width in points (320 high, so 569 is 16:9). Mirror portal sprites move relative to it.
 	float screenWidth = 569;
 	bool debug = false;

 	Level(std::string const& lvlString);

 	/// The main update function. Every frame is associated with a press/release state.
 	Player& runFrame(bool pressed, float dt = 1/240.);
 	/// One player's part of a frame (move, collisions); returns how many objects it touched
 	int stepPlayer(Player& p, bool pressed, float dt);
 	/// GJBaseGameLayer::checkRepellPlayer, after both players' steps in dual mode: two balls of the same gravity
 	/// closer in y than their radii plus 5 push apart, the one in the air flipping away from the grounded one
 	void repelBalls(Player& p, Player& q) const;

 	/// Go back to a certain frame. Used in Pathfinder. Never drops gameStates[0].
 	void rollback(int frame);

 	/**
 	 * Continue from a player state made outside the sim (e.g. copied from GD) instead of the level start.
 	 * `state.frame` is kept, so frame numbers stay those of the whole level; earlier frames don't exist
 	 * and getState returns the first state for them.
 	 */
 	void resume(Player state);

 	int currentFrame() const;
 	Player const& getState(int frame, bool p2 = false) const;
 	Player& latestState();
};
