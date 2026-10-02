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

	/// When the vehicle is changed into this one.
	void (*enter)(Player&) = nullptr;

	/// Ran after everything else, used mainly for vehicles that have ceilings
	void (*clamp)(Player&) = nullptr;

	/// Vehicle-specific movement, done after collisions
	void (*update)(Player&) = nullptr;

	/// How far away the floor and ceiling are from each other, relative to portal
	float bounds;

	static Vehicle from(VehicleType v);
};
