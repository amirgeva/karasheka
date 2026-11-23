#pragma once

// The fight itself: two karateka, their moves, hits and blocks, furniture,
// rounds and the clock. Runs in fixed 1/60 s ticks with no SDL dependency, so
// whole matches play headlessly in the tests.

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/input.h"
#include "core/pose.h"
#include "core/rng.h"
#include "core/room.h"

namespace kara {

class Brain;

constexpr float kTick = 1.f / 60.f;
constexpr float kGravity = 720.f;    // px/s^2
constexpr float kJumpSpeed = 252.f;  // a standing jump rises about 44 px
constexpr float kJumpVx = 78.f;
constexpr float kWalkFwd = 62.f;
constexpr float kWalkBack = 50.f;
constexpr float kRollSpeed = 150.f;
constexpr int kRollFrames = 22;
constexpr float kDiveVx = 150.f;
constexpr float kDiveVy = 290.f;
constexpr float kMaxHealth = 100.f;
constexpr float kPushWidth = 20.f;  // fighters' bodies keep this far apart
constexpr float kElevation = 14.f;  // height gap that turns attacks overhead (or low)

// ---------------------------------------------------------------- moves

enum class Move : int {
    None, Punch, FrontKick, Roundhouse, Uppercut, LowPunch, LowKick, Sweep, FlyingKick, Backflip, AirKick, DiveKick,
    Count
};
constexpr int kMoveCount = int(Move::Count);

// How an attack has to be guarded: Mid by either guard, Low only crouching,
// Overhead only standing.
enum class Level { Mid, Low, Overhead };
enum class Striker { FrontHand, BackHand, FrontFoot, BackFoot };

struct MoveInfo {
    const char* name;
    int startup, active, recovery;  // frames
    Striker striker;
    float hitR;  // radius of the strike point
    Level level;
    float damage;
    int hitstun, blockstun;
    float push;      // knockback speed on a hit, px/s
    bool knockdown;  // a clean hit floors the opponent
    bool airborne;   // performed in the air
    PoseId chamber, strike, finish;
    bool crouched;   // done from, and back into, a crouch
    float lunge;     // forward speed while it comes out, px/s
    float jumpVx, jumpVy;  // jumping attacks: launch velocity (vx toward the facing)
    int landLag;     // air attacks: frames stuck after landing
};
const MoveInfo& moveInfo(Move m);

// The attack that keypad direction `pad` (1-9, 0 = none) plus strike starts.
// Back and forward are relative to the fighter's facing.
Move moveFor(int pad, int facing, bool airborne);

// How far ahead of the fighter's centre a move connects, and at what height
// above its feet (measured on the skeleton, lunge included).
struct Reach {
    float ahead = 0.f;
    float low = 0.f, high = 0.f;
};
const Reach& moveReach(Move m);

// ---------------------------------------------------------------- fighters

enum class Act {
    Intro, Stand, Walk, Guard, Crouch, PreJump, Air, Land, Roll, Attack, Hit, Block, Fall, Down, GetUp, Win, Ko
};

struct Fighter {
    int id = 0;
    float x = 0.f, y = 0.f;  // centre, and the lowest point of the body
    float vx = 0.f, vy = 0.f;
    int facing = 1;  // +1 right, -1 left
    Act act = Act::Intro;
    int frame = 0;  // ticks spent in the current act
    Move move = Move::None;
    bool connected = false;  // the current attack already hit or was blocked
    float health = kMaxHealth;
    bool airborne = false;
    int support = -1;  // furniture stood on, -1 = the floor
    float peakY = 0.f;  // highest point since leaving the ground
    bool airAttack = false;  // already attacked during this jump
    int stun = 0;       // Hit / Block / Land: frames until free
    bool crouchBlock = false;
    bool lowHit = false;  // hit while crouching
    int rollDir = 0;
    int jumpDx = 0;     // direction chosen when the jump began
    float walkPhase = 0.f;
    bool ko = false;
    int flash = 0;  // frames of hit flash left (cosmetic)
    int bufPad = 0, bufAge = 0;  // a strike pressed a moment too early
    Input in, prevIn;
    Pose from;  // the pose when the act began, blended away from
    Pose pose;
    Skeleton skel;
};

// Where a fighter's current strike would land.
Vec strikePoint(const Fighter& f);

// ---------------------------------------------------------------- match

enum class Control : int { Human, CpuEasy, CpuNormal, CpuHard };
constexpr int kControlCount = 4;
const char* controlName(Control c);

struct MatchSetup {
    std::array<Control, 2> control{{Control::Human, Control::CpuNormal}};
    int rounds = 3;      // best of
    int roundTime = 60;  // seconds; 0 = no time limit
    int room = -1;       // -1 = a different room every round
    uint64_t seed = 1;
};

enum class Phase { Intro, Fight, Ko, TimeUp, RoundOver, MatchOver };

constexpr float kIntroTime = 2.0f;  // "ROUND n" then "FIGHT!"
constexpr float kFightAt = 1.25f;   // when "FIGHT!" replaces "ROUND n"
constexpr float kKoTime = 2.6f;
constexpr float kTimeUpTime = 1.6f;
constexpr float kRoundOverTime = 3.0f;

enum class EventType {
    RoundStart, Fight, Swing, Kiai, Jump, Land, Hit, Block, Knockdown, Crack, Break, Ko, TimeUp, RoundOver, MatchOver
};

// Things the front end may want to show or play.
struct Event {
    EventType type = EventType::RoundStart;
    int fighter = -1;  // who acted, or who was hit
    float x = 0.f, y = 0.f;
    float amount = 0.f;  // damage (Hit), drop height (Land)
    Move move = Move::None;
    int piece = -1;  // furniture (Crack, Break, Land)
    bool heavy = false;
};

// A fighter as the computer players perceive it, a few frames late.
struct Snapshot {
    Act act = Act::Intro;
    Move move = Move::None;
    int frame = 0;
    float x = 0.f, y = 0.f, vx = 0.f, vy = 0.f, top = 0.f;
    int facing = 1;
    bool airborne = false;
    int support = -1;
    float health = kMaxHealth;
};

class Match {
public:
    explicit Match(const MatchSetup& setup);
    ~Match();
    Match(const Match&) = delete;
    Match& operator=(const Match&) = delete;

    void setInput(int fighter, const Input& in) { human_[size_t(fighter & 1)] = in; }
    void update(float dt);  // runs whole ticks
    void tick();

    const MatchSetup& setup() const { return setup_; }
    Phase phase() const { return phase_; }
    float phaseTime() const { return phaseTime_; }
    float clock() const { return clock_; }
    int ticks() const { return ticks_; }
    int round() const { return round_; }
    int room() const { return room_; }
    bool timed() const { return setup_.roundTime > 0; }
    float timeLeft() const { return timeLeft_; }
    const Fighter& fighter(int i) const { return fighters_[size_t(i & 1)]; }
    const std::vector<Furniture>& furniture() const { return furniture_; }
    int wins(int i) const { return wins_[size_t(i & 1)]; }
    int roundsToWin() const { return (setup_.rounds + 1) / 2; }
    int roundWinner() const { return roundWinner_; }  // -1: a draw
    int matchWinner() const;                           // -1: a draw
    Control control(int i) const { return setup_.control[size_t(i & 1)]; }
    bool isCpu(int i) const { return control(i) != Control::Human; }
    bool frozen() const { return freeze_ > 0; }

    // A fighter as it was `delay` ticks ago (0 = now).
    const Snapshot& seen(int fighter, int delay) const;

    std::vector<Event> takeEvents();

    // Test hooks.
    Fighter& fighterForTest(int i) { return fighters_[size_t(i & 1)]; }
    std::vector<Furniture>& furnitureForTest() { return furniture_; }
    void skipIntro();
    void refresh(Fighter& f);

private:
    void push(const Event& e) { events_.push_back(e); }
    void setPhase(Phase p);
    void startRound();
    void finishRound();
    void record();

    void stepFighter(int i, const Input& in);
    void neutral(Fighter& f, const Fighter& o, int pad, int prevPad);
    void face(Fighter& f, const Fighter& o);
    void startMove(Fighter& f, Move m);
    void stepAttack(Fighter& f);
    void physics(Fighter& f);
    void goAirborne(Fighter& f);
    bool findLanding(float x, float fromY, float toY, int& support, float& top) const;
    void land(Fighter& f, int support);
    void damagePiece(int piece, float x);
    void separate();
    void resolveHits();
    bool testHit(const Fighter& a, const Fighter& d, Vec& point) const;
    void applyHit(int attacker, Move move, Vec point);
    void setAct(Fighter& f, Act a);
    Pose targetPose(const Fighter& f) const;
    Pose attackPose(const Fighter& f) const;

    MatchSetup setup_;
    Rng rng_;
    std::array<Fighter, 2> fighters_;
    std::array<Input, 2> human_{};
    std::array<std::unique_ptr<Brain>, 2> brains_;
    std::vector<Furniture> furniture_;
    std::vector<Event> events_;
    std::array<int, 2> wins_{{0, 0}};

    static constexpr int kHistory = 64;
    std::array<std::array<Snapshot, kHistory>, 2> history_{};

    Phase phase_ = Phase::Intro;
    float phaseTime_ = 0.f;
    float clock_ = 0.f;
    float acc_ = 0.f;
    int ticks_ = 0;
    int round_ = 0;
    int firstRoom_ = 0;
    int room_ = 0;
    float timeLeft_ = 0.f;
    int roundWinner_ = -1;
    int freeze_ = 0;  // hit stop: the fight pauses for a few frames on impact
};

}  // namespace kara
