#pragma once

enum class VehicleType {
	Cube,
	Ship,
	Ball,
	Ufo,
	Wave
};

struct Player;
struct Object;

/// NOT the same as vehicle portal. This class exists to hold vehicle-specific logic.
struct Vehicle {
	VehicleType type;

	/// When the vehicle is changed into this one, from `from` (the vehicle of the portal before it when
	/// several are touched in one frame)
	void (*enter)(Player&, VehicleType from) = nullptr;

	/// Ran after everything else, used mainly for vehicles that have ceilings
	void (*clamp)(Player&) = nullptr;

	/// Vehicle-specific movement, done after collisions
	void (*update)(Player&) = nullptr;

	/// A ship's, ufo's or wave's turn towards its travel (PlayerObject::updateShipRotation), after the update and
	/// the step's walk-off move
	void (*turn)(Player&) = nullptr;

	/// How far away the floor and ceiling are from each other, relative to portal
	float bounds;

	static Vehicle from(VehicleType v);
};

/// GD's per-step fall (PlayerObject::updateJump) as an acceleration in sim units, see Vehicle.cpp
double gdFall(Player const& p, float gravity, float mult, float size = 1);

/// The acceleration (sim units) the vehicle's update would give the player in its current state (velocity,
/// gravity, button): GD's next updateJump's gravity. No side effects.
double stepAcceleration(Player const& p);
