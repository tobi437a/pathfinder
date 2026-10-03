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

	/// How far away the floor and ceiling are from each other, relative to portal
	float bounds;

	static Vehicle from(VehicleType v);
};

/// GD's per-step fall (PlayerObject::updateJump) as an acceleration in sim units, see Vehicle.cpp
double gdFall(Player const& p, float gravity, float mult, float size = 1);
