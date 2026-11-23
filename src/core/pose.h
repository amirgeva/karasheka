#pragma once

// The fighters' skeleton. A Pose is a set of joint angles in a side view of a
// fighter facing right; solve() turns it into joint positions in the world.
// The same skeleton drives the drawing, the hurt boxes and the strike points,
// so what you see is what hits.

#include <array>
#include <cmath>

namespace kara {

struct Vec {
    float x = 0.f, y = 0.f;
};
inline Vec operator+(Vec a, Vec b) { return Vec{a.x + b.x, a.y + b.y}; }
inline Vec operator-(Vec a, Vec b) { return Vec{a.x - b.x, a.y - b.y}; }
inline Vec operator*(Vec a, float s) { return Vec{a.x * s, a.y * s}; }
inline float dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y; }
inline float length(Vec a) { return std::sqrt(dot(a, a)); }
inline Vec lerp(Vec a, Vec b, float t) { return a + (b - a) * t; }

// Distance from p to the segment ab.
float segmentDistance(Vec p, Vec a, Vec b);

inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
inline float smooth01(float t) {
    t = clamp01(t);
    return t * t * (3.f - 2.f * t);
}

// Body measurements, in world pixels. A fighter stands about 50 px tall.
constexpr float kThigh = 12.f;
constexpr float kShin = 12.f;
constexpr float kTorso = 16.f;
constexpr float kNeck = 2.f;
constexpr float kHeadR = 5.f;
constexpr float kUpperArm = 10.f;
constexpr float kForearm = 9.f;
constexpr float kFootLen = 4.f;
constexpr float kSole = 1.8f;  // how far the sole reaches below the ankle joint

enum Side { kFront = 0, kBack = 1 };

// Angles in degrees for a fighter facing right. Limb angles are absolute:
// 0 points straight down, +90 forward (toward the opponent), 180 straight up
// and -90 backward.
struct Pose {
    float lean = 0.f;  // torso tilt from upright, + = forward
    float head = 0.f;  // head tilt relative to the torso, + = chin down
    float arm[2][2] = {{0.f, 0.f}, {0.f, 0.f}};  // [front|back][upper arm|forearm]
    float leg[2][2] = {{0.f, 0.f}, {0.f, 0.f}};  // [front|back][thigh|shin]
    float spin = 0.f;  // whole-body rotation about the belly, + = forward roll
};

Pose lerp(const Pose& a, const Pose& b, float t);

enum class PoseId : int {
    Stance, StanceLow, Walk1, Walk2, Guard, GuardHigh, GuardLow, Crouch, PreJump, Tuck,
    PunchChamber, PunchStrike,
    FrontChamber, FrontStrike, FrontFinish,
    RoundChamber, RoundStrike, RoundFinish,
    UpperChamber, UpperStrike, UpperFinish,
    LowPunchChamber, LowPunchStrike,
    LowKickChamber, LowKickStrike,
    SweepChamber, SweepStrike, SweepFinish,
    FlyChamber, FlyStrike,
    AirKick, Dive,
    FlipChamber, FlipStrike,
    Hit, HitCrouch, Fall, Lying, Win, Bow,
    Count
};
constexpr int kPoseCount = int(PoseId::Count);
const Pose& pose(PoseId id);
const char* poseName(PoseId id);

struct Skeleton {
    Vec hip, shoulder, neck, head;
    Vec elbow[2], hand[2];
    Vec knee[2], ankle[2], toe[2];
    Vec up;           // unit vector from the neck toward the crown of the head
    int facing = 1;
    float top = 0.f;  // highest point of the body (smallest y), arms left out
};

// Joint positions for a fighter whose lowest point rests on (x, y).
Skeleton solve(const Pose& p, float x, float y, int facing);

struct Capsule {
    Vec a, b;
    float r;
};
constexpr int kHurtCount = 6;  // head, torso, two thighs, two shins
std::array<Capsule, kHurtCount> hurtCapsules(const Skeleton& s);

}  // namespace kara
