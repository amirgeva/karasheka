#include "core/pose.h"

#include <algorithm>

namespace kara {
namespace {

constexpr float kDeg = 3.14159265f / 180.f;

// A limb pointing at `deg` (0 down, 90 forward, 180 up) in a facing-right view.
Vec dirOf(float deg) { return Vec{std::sin(deg * kDeg), std::cos(deg * kDeg)}; }

constexpr Pose P(float lean, float head, float fu, float fl, float bu, float bl, float ft, float fs, float bt, float bs,
                 float spin = 0.f) {
    return Pose{lean, head, {{fu, fl}, {bu, bl}}, {{ft, fs}, {bt, bs}}, spin};
}

struct Named {
    const char* name;
    Pose pose;
};

// clang-format off
//                        lean  head   front arm    back arm     front leg    back leg
const Named kPoses[kPoseCount] = {
    {"STANCE",          P(  6,  -6,   45, 150,   -10, 105,    28,   8,   -24, -14)},
    {"STANCE LOW",      P(  7,  -7,   48, 152,    -8, 108,    31,   5,   -27, -11)},
    {"WALK 1",          P(  6,  -6,   45, 150,   -10, 105,    12,   2,    -6, -26)},
    {"WALK 2",          P(  5,  -5,   42, 146,   -12, 102,    36,  12,   -32,  -8)},
    {"GUARD",           P(  0,  -4,  100, 175,    70, 165,    30,  10,   -30, -18)},
    {"GUARD HIGH",      P( -4,   0,  165, 102,    60, 150,    30,  10,   -30, -18)},
    {"GUARD LOW",       P( 14, -10,   50,  40,   -30,  60,    45,   0,   -38, -24)},
    {"CROUCH",          P( 50, -45,   75, 165,    40, 125,    95, -15,    20, -95)},
    {"PRE JUMP",        P( 15, -15,   55, 155,     0, 115,    70, -10,   -10, -60)},
    {"TUCK",            P( 10, -10,   60, 160,    20, 130,   100,  10,    50, -40)},
    {"PUNCH CHAMBER",   P(  4,  -6,   60, 140,   -35,  70,    30,   8,   -26, -14)},
    {"PUNCH",           P( 14, -12,  -40,  60,    88,  90,    40,  12,   -34, -26)},
    {"FRONT CHAMBER",   P( -6,   0,   60, 150,   -20, 100,   105,   5,    -6,  -4)},
    {"FRONT KICK",      P(-12,   6,   50, 140,   -30,  90,    96,  92,    -8,  -4)},
    {"FRONT FINISH",    P(-12,   6,   50, 140,   -30,  90,    94,  95,    -8,  -4)},
    {"ROUND CHAMBER",   P(-15,  10,   60, 150,    30, 120,     8,   0,   120,  20)},
    {"ROUNDHOUSE",      P(-30,  22,   40, 130,   -60, -10,     6,   0,   155, 120)},
    {"ROUND FINISH",    P(-32,  24,   40, 130,   -60, -10,     6,   0,   160, 110)},
    {"UPPER CHAMBER",   P( 20, -15,   20,  70,   -20, 100,    50,   0,   -20, -40)},
    {"UPPERCUT",        P( -2, -10,  100, 140,   -30,  60,    25,   5,   -15, -10)},
    {"UPPER FINISH",    P( -8, -15,  160, 172,   -30,  60,    20,   5,   -10,  -8)},
    {"LOW P CHAMBER",   P( 30, -28,   40, 120,    30, 120,    95, -15,    20, -95)},
    {"LOW PUNCH",       P( 25, -22,   75,  80,    20, 110,    95, -15,    20, -95)},
    {"LOW K CHAMBER",   P( 25, -22,   70, 160,    30, 120,   100,  10,    20, -95)},
    {"LOW KICK",        P( 20, -18,   70, 160,    30, 120,    78,  88,    20, -95)},
    {"SWEEP CHAMBER",   P( 45, -40,   10,  10,   -20,  30,   100, -20,   -30,-110)},
    {"SWEEP",           P( 45, -40,   10,  10,   -10,  20,   100, -20,    50,  70)},
    {"SWEEP FINISH",    P( 45, -40,   10,  10,   -10,  20,   100, -20,    85,  92)},
    {"FLY CHAMBER",     P(  0,  -5,   60, 150,   -40,  40,   110,  10,    40, -60)},
    {"FLYING KICK",     P(-12,   5,   50, 140,   -70, -40,    92,  92,    30, -75)},
    {"AIR KICK",        P( -5,   0,   60, 150,   -50,  30,    72,  78,    30, -75)},
    {"DIVE KICK",       P( 25, -10,  -40, -70,   -70,-100,    38,  42,    75, -40)},
    {"FLIP CHAMBER",    P(-10,   0,   40, 120,   -30,  60,   110, 110,    -5,  -5)},
    {"BACKFLIP KICK",   P(-25,  10,  -20,  30,   -50,   0,   165, 172,   -10,  -8)},
    {"HIT",             P(-22, -18,   20,  70,   -40,  10,    30,  12,   -20, -10)},
    {"HIT CROUCH",      P( 30, -30,   40, 100,    20,  80,    95, -15,    20, -95)},
    {"FALL",            P(-10, -20,  150, 170,   120, 150,    30,  10,    10,   0, -10)},
    {"LYING",           P(  0,  10,  170, 175,   150, 170,     8,   4,    -4,  -2, -90)},
    {"WIN",             P( -4, -10,  172, 178,   -30,  40,    15,   5,   -15,  -5)},
    {"BOW",             P( 45,  10,   10,   5,     0,  -5,     4,   2,    -4,  -2)},
};
// clang-format on

Vec rotate(Vec p, Vec c, float deg) {
    const float s = std::sin(deg * kDeg), co = std::cos(deg * kDeg);
    const Vec d = p - c;
    // Clockwise on screen (y down) for positive angles: a forward roll.
    return Vec{c.x + d.x * co - d.y * s, c.y + d.x * s + d.y * co};
}

}  // namespace

float segmentDistance(Vec p, Vec a, Vec b) {
    const Vec ab = b - a;
    const float len2 = dot(ab, ab);
    const float t = len2 > 1e-6f ? clamp01(dot(p - a, ab) / len2) : 0.f;
    return length(p - (a + ab * t));
}

Pose lerp(const Pose& a, const Pose& b, float t) {
    auto l = [t](float x, float y) { return x + (y - x) * t; };
    Pose p;
    p.lean = l(a.lean, b.lean);
    p.head = l(a.head, b.head);
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j) {
            p.arm[i][j] = l(a.arm[i][j], b.arm[i][j]);
            p.leg[i][j] = l(a.leg[i][j], b.leg[i][j]);
        }
    p.spin = l(a.spin, b.spin);
    return p;
}

const Pose& pose(PoseId id) { return kPoses[std::clamp(int(id), 0, kPoseCount - 1)].pose; }
const char* poseName(PoseId id) { return kPoses[std::clamp(int(id), 0, kPoseCount - 1)].name; }

Skeleton solve(const Pose& p, float x, float y, int facing) {
    Skeleton s;
    s.facing = facing < 0 ? -1 : 1;

    // Facing right, hip at the origin.
    const Vec up = Vec{std::sin(p.lean * kDeg), -std::cos(p.lean * kDeg)};
    s.hip = Vec{0.f, 0.f};
    s.shoulder = up * kTorso;
    s.neck = s.shoulder + up * kNeck;
    const float hd = (p.lean + p.head) * kDeg;
    s.head = s.neck + Vec{std::sin(hd), -std::cos(hd)} * kHeadR;
    for (int i = 0; i < 2; ++i) {
        s.elbow[i] = s.shoulder + dirOf(p.arm[i][0]) * kUpperArm;
        s.hand[i] = s.elbow[i] + dirOf(p.arm[i][1]) * kForearm;
        s.knee[i] = s.hip + dirOf(p.leg[i][0]) * kThigh;
        s.ankle[i] = s.knee[i] + dirOf(p.leg[i][1]) * kShin;
        s.toe[i] = s.ankle[i] + dirOf(p.leg[i][1] + 90.f) * kFootLen;
    }

    Vec* all[] = {&s.hip, &s.shoulder, &s.neck, &s.head, &s.elbow[0], &s.elbow[1], &s.hand[0], &s.hand[1],
                  &s.knee[0], &s.knee[1], &s.ankle[0], &s.ankle[1], &s.toe[0], &s.toe[1]};
    if (p.spin != 0.f) {
        const Vec c = up * 6.f;  // the belly
        for (Vec* v : all) *v = rotate(*v, c, p.spin);
    }

    // The body rests on its lowest point: normally the soles, the back or
    // the side when lying down.
    auto lowest = [&] {
        float low = std::max({s.hip.y + 4.f, s.shoulder.y + 4.f, s.head.y + kHeadR - 0.5f});
        for (int i = 0; i < 2; ++i) low = std::max({low, s.ankle[i].y + kSole, s.toe[i].y + kSole * 0.6f});
        return low;
    };
    // Feet on (or nearly on) the ground lie flat.
    if (p.spin == 0.f) {
        float soles = -1e9f;
        for (int i = 0; i < 2; ++i) soles = std::max(soles, s.ankle[i].y);
        for (int i = 0; i < 2; ++i)
            if (s.ankle[i].y > soles - 2.5f) s.toe[i] = s.ankle[i] + Vec{kFootLen, 0.f};
    }
    const float low = lowest();

    for (Vec* v : all) {
        v->x = x + v->x * float(s.facing);
        v->y = y + v->y - low;
    }
    const Vec u = s.head - s.neck;
    const float ul = length(u);
    s.up = ul > 1e-4f ? u * (1.f / ul) : Vec{0.f, -1.f};
    s.top = std::min({s.head.y - kHeadR, s.shoulder.y - 5.f, s.hip.y - 5.f, s.knee[0].y - 4.f, s.knee[1].y - 4.f,
                      s.ankle[0].y - 3.f, s.ankle[1].y - 3.f});
    return s;
}

std::array<Capsule, kHurtCount> hurtCapsules(const Skeleton& s) {
    return {{
        Capsule{s.head, s.head, 5.5f},
        Capsule{s.hip, s.shoulder, 5.5f},
        Capsule{s.hip, s.knee[0], 3.8f},
        Capsule{s.hip, s.knee[1], 3.8f},
        Capsule{s.knee[0], s.ankle[0], 3.2f},
        Capsule{s.knee[1], s.ankle[1], 3.2f},
    }};
}

}  // namespace kara
