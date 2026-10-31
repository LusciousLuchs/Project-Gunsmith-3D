// Gunsmith3D - First-person movement through a map: a floating camera with a body for collision.
// Quake-style "slide move": the body is an upright box swept against the map's brushes, sliding along whatever it
// hits, stepping up stairs and kerbs, with gravity, jumping, sprinting, crouching, going prone and leaning.
// No graphics here; the app reads the eye position and draws from it.
#pragma once
#include "map.hpp"

namespace gmap {

enum class Stance { Stand, Crouch, Prone };

struct PlayerInput {
    double forward = 0, right = 0;   // -1..1
    bool jump = false, sprint = false;
    Stance want = Stance::Stand;
    double lean = 0;                 // -1 left .. 1 right
};

struct Player {
    V3 feet;                         // bottom centre of the body
    V3 vel;
    double yaw = 0, pitch = 0;       // radians; yaw 0 looks along +X, pitch up is positive
    Stance stance = Stance::Stand;
    double eye = 1.62;               // eye height above the feet (eases between stances)
    double lean = 0;                 // eased lean amount
    bool onGround = false;
};

struct PlayerTuning {
    double halfWidth = 0.24;
    double standH = 1.80, crouchH = 1.20, proneH = 0.50;
    double standEye = 1.62, crouchEye = 1.05, proneEye = 0.32;
    double walk = 3.6, sprint = 6.2, crouchSpeed = 1.9, proneSpeed = 0.8;
    double accel = 12, airAccel = 1.5, friction = 7;
    double gravity = 9.81, jumpSpeed = 3.6, stepHeight = 0.42;
    double leanDist = 0.38, leanRollDeg = 12;
};

void playerSpawn(Player& p, const Map& m);  // at the map's player start (or the origin)
void playerStep(Player& p, const Map& m, const PlayerInput& in, double dt, const PlayerTuning& T = {});
double playerHeight(const Player& p, const PlayerTuning& T = {});
// Where the eye is, with the lean applied (and pulled back if a wall is in the way), and the lean's roll in radians.
V3 playerEye(const Player& p, const Map& m, double* roll = nullptr, const PlayerTuning& T = {});
V3 playerForward(const Player& p);

}  // namespace gmap
