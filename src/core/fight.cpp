#include "core/fight.h"

#include <algorithm>
#include <cmath>

#include "core/ai.h"

namespace kara {
namespace {

constexpr int kPreJumpFrames = 3;
constexpr int kLandFrames = 4;
constexpr int kDownFrames = 44;
constexpr int kGetUpFrames = 24;
constexpr int kBufferFrames = 8;
constexpr int kBlend = 4;  // frames to blend into a new stance
constexpr float kFriction = 0.86f;
constexpr float kMaxFall = 420.f;

using P = PoseId;

// clang-format off
const MoveInfo kMoves[kMoveCount] = {
    // name          start act rec  striker             hitR  level            dmg  hstun bstun push  kd     air    chamber             strike             finish            crch   lunge jumpVx jumpVy lag
    {"",               0,   0,  0, Striker::FrontHand, 0.f,  Level::Mid,      0.f,  0,   0,    0.f, false, false, P::Stance,          P::Stance,         P::Stance,        false,  0.f,   0.f,    0.f,  0},
    {"PUNCH",          5,   3,  9, Striker::BackHand,  3.5f, Level::Mid,      6.f, 14,  10,   70.f, false, false, P::PunchChamber,    P::PunchStrike,    P::PunchStrike,   false, 50.f,   0.f,    0.f,  0},
    {"FRONT KICK",     8,   4, 13, Striker::FrontFoot, 4.f,  Level::Mid,      9.f, 17,  12,  110.f, false, false, P::FrontChamber,    P::FrontStrike,    P::FrontFinish,   false, 30.f,   0.f,    0.f,  0},
    {"ROUNDHOUSE",    12,   4, 18, Striker::BackFoot,  5.f,  Level::Mid,     14.f,  0,  14,  150.f, true,  false, P::RoundChamber,    P::RoundStrike,    P::RoundFinish,   false, 45.f,   0.f,    0.f,  0},
    {"UPPERCUT",       6,   5, 16, Striker::FrontHand, 4.f,  Level::Mid,     11.f,  0,  12,   60.f, true,  false, P::UpperChamber,    P::UpperStrike,    P::UpperFinish,   false, 20.f,   0.f,    0.f,  0},
    {"LOW PUNCH",      4,   3,  8, Striker::FrontHand, 3.5f, Level::Mid,      5.f, 12,   9,   60.f, false, false, P::LowPunchChamber, P::LowPunchStrike, P::LowPunchStrike, true,  0.f,   0.f,    0.f,  0},
    {"LOW KICK",       6,   3, 11, Striker::FrontFoot, 4.f,  Level::Low,      7.f, 15,  10,   80.f, false, false, P::LowKickChamber,  P::LowKickStrike,  P::LowKickStrike, true,   0.f,   0.f,    0.f,  0},
    {"SWEEP",         10,   5, 20, Striker::BackFoot,  4.5f, Level::Low,     10.f,  0,  14,   60.f, true,  false, P::SweepChamber,    P::SweepStrike,    P::SweepFinish,   true,  25.f,   0.f,    0.f,  0},
    {"FLYING KICK",    7,  90,  0, Striker::FrontFoot, 4.5f, Level::Overhead,12.f,  0,  14,  140.f, true,  true,  P::FlyChamber,      P::FlyStrike,      P::FlyStrike,     false,  0.f, 115.f, -215.f, 10},
    {"BACKFLIP KICK",  4,   8,  0, Striker::FrontFoot, 4.5f, Level::Mid,      9.f,  0,  12,  120.f, true,  true,  P::FlipChamber,     P::FlipStrike,     P::FlipStrike,    false,  0.f, -90.f, -260.f,  6},
    {"AIR KICK",       4,  90,  0, Striker::FrontFoot, 4.5f, Level::Overhead, 9.f, 18,  12,  100.f, false, true,  P::Tuck,            P::AirKick,        P::AirKick,       false,  0.f,   0.f,    0.f,  5},
    {"DIVE KICK",      3,  90,  0, Striker::FrontFoot, 5.f,  Level::Overhead,10.f,  0,  16,  160.f, true,  true,  P::Tuck,            P::Dive,           P::Dive,          false,  0.f,   0.f,    0.f, 14},
};
// clang-format on

const char* const kControlNames[kControlCount] = {"HUMAN", "CPU EASY", "CPU NORMAL", "CPU HARD"};

bool isNeutral(Act a) { return a == Act::Stand || a == Act::Walk || a == Act::Guard || a == Act::Crouch; }

float wrapSpin(float s) {
    s = std::fmod(s, 360.f);
    if (s > 180.f) s -= 360.f;
    if (s < -180.f) s += 360.f;
    return s;
}

Vec strikeOf(const Skeleton& s, Striker st) {
    switch (st) {
        case Striker::FrontHand: return s.hand[kFront];
        case Striker::BackHand: return s.hand[kBack];
        case Striker::FrontFoot: return lerp(s.ankle[kFront], s.toe[kFront], 0.5f);
        case Striker::BackFoot: return lerp(s.ankle[kBack], s.toe[kBack], 0.35f);
    }
    return s.hip;
}

}  // namespace

const MoveInfo& moveInfo(Move m) { return kMoves[std::clamp(int(m), 0, kMoveCount - 1)]; }

const char* controlName(Control c) { return kControlNames[std::clamp(int(c), 0, kControlCount - 1)]; }

Move moveFor(int pad, int facing, bool airborne) {
    if (pad < 1 || pad > 9) pad = 5;
    const int dx = (pad - 1) % 3 - 1;
    const int dy = 1 - (pad - 1) / 3;
    if (airborne) return dy > 0 ? Move::DiveKick : Move::AirKick;
    const int fwd = dx * (facing < 0 ? -1 : 1);
    if (dy < 0) return fwd > 0 ? Move::FlyingKick : fwd < 0 ? Move::Backflip : Move::Uppercut;
    if (dy > 0) return fwd > 0 ? Move::Sweep : fwd < 0 ? Move::LowPunch : Move::LowKick;
    return fwd > 0 ? Move::FrontKick : fwd < 0 ? Move::Roundhouse : Move::Punch;
}

const Reach& moveReach(Move m) {
    static const std::array<Reach, kMoveCount> table = [] {
        std::array<Reach, kMoveCount> t{};
        for (int i = 1; i < kMoveCount; ++i) {
            const MoveInfo& mi = kMoves[i];
            Reach r;
            r.low = 1e9f;
            r.high = -1e9f;
            for (int k = 0; k <= 4; ++k) {
                const Pose p = lerp(pose(mi.strike), pose(mi.finish), float(k) / 4.f);
                const Skeleton s = solve(p, 0.f, 0.f, 1);
                const Vec v = strikeOf(s, mi.striker);
                r.ahead = std::max(r.ahead, v.x + mi.hitR);
                r.low = std::min(r.low, -v.y - mi.hitR);
                r.high = std::max(r.high, -v.y + mi.hitR);
            }
            r.ahead += mi.lunge * float(mi.startup + std::min(mi.active, 6)) * kTick;
            if (mi.jumpVx > 0.f) r.ahead += mi.jumpVx * 0.35f;
            t[size_t(i)] = r;
        }
        return t;
    }();
    return table[size_t(std::clamp(int(m), 0, kMoveCount - 1))];
}

Vec strikePoint(const Fighter& f) { return strikeOf(f.skel, moveInfo(f.move).striker); }

// ---------------------------------------------------------------- match flow

Match::Match(const MatchSetup& setup) : setup_(setup), rng_(setup.seed) {
    setup_.rounds = std::max(1, setup_.rounds);
    for (size_t i = 0; i < 2; ++i)
        if (setup_.control[i] != Control::Human)
            brains_[i] = std::make_unique<Brain>(setup_.control[i], hash64(setup_.seed, i + 1));
    firstRoom_ = setup_.room >= 0 ? setup_.room % kRoomCount : int(rng_.below(kRoomCount));
    startRound();
}

Match::~Match() = default;

void Match::setPhase(Phase p) {
    phase_ = p;
    phaseTime_ = 0.f;
}

void Match::startRound() {
    ++round_;
    room_ = setup_.room >= 0 ? setup_.room % kRoomCount : (firstRoom_ + round_ - 1) % kRoomCount;
    furniture_ = furnish(room_);
    for (int i = 0; i < 2; ++i) {
        Fighter& f = fighters_[size_t(i)];
        f = Fighter{};
        f.id = i;
        f.x = i == 0 ? 110.f : 210.f;
        f.y = kFloorY;
        f.facing = i == 0 ? 1 : -1;
        f.act = Act::Intro;
        f.from = pose(PoseId::Bow);
        refresh(f);
    }
    timeLeft_ = float(std::max(0, setup_.roundTime));
    roundWinner_ = -1;
    freeze_ = 0;
    setPhase(Phase::Intro);
    for (auto& h : history_) h.fill(Snapshot{});
    Event e;
    e.type = EventType::RoundStart;
    push(e);
}

void Match::skipIntro() {
    if (phase_ != Phase::Intro) return;
    setPhase(Phase::Fight);
    for (Fighter& f : fighters_) {
        setAct(f, Act::Stand);
        f.from = pose(PoseId::Stance);
        refresh(f);
    }
}

void Match::finishRound() {
    const Fighter& a = fighters_[0];
    const Fighter& b = fighters_[1];
    if (phase_ == Phase::TimeUp) {
        roundWinner_ = std::fabs(a.health - b.health) < 0.5f ? -1 : (a.health > b.health ? 0 : 1);
    }
    if (roundWinner_ >= 0) {
        ++wins_[size_t(roundWinner_)];
    } else {
        ++wins_[0];
        ++wins_[1];
    }
    if (roundWinner_ >= 0) {
        Fighter& w = fighters_[size_t(roundWinner_)];
        if (!w.airborne && !w.ko) {
            setAct(w, Act::Win);
            w.vx = 0.f;
        }
    }
    setPhase(Phase::RoundOver);
    Event e;
    e.type = EventType::RoundOver;
    e.fighter = roundWinner_;
    push(e);
}

int Match::matchWinner() const {
    if (wins_[0] == wins_[1]) return -1;
    return wins_[0] > wins_[1] ? 0 : 1;
}

std::vector<Event> Match::takeEvents() {
    std::vector<Event> out;
    out.swap(events_);
    return out;
}

const Snapshot& Match::seen(int fighter, int delay) const {
    delay = std::clamp(delay, 0, kHistory - 1);
    const int idx = ((ticks_ - delay) % kHistory + kHistory) % kHistory;
    return history_[size_t(fighter & 1)][size_t(idx)];
}

void Match::record() {
    const int idx = ticks_ % kHistory;
    for (size_t i = 0; i < 2; ++i) {
        const Fighter& f = fighters_[i];
        Snapshot& s = history_[i][size_t(idx)];
        s.act = f.act;
        s.move = f.move;
        s.frame = f.frame;
        s.x = f.x;
        s.y = f.y;
        s.vx = f.vx;
        s.vy = f.vy;
        s.top = f.skel.top;
        s.facing = f.facing;
        s.airborne = f.airborne;
        s.support = f.support;
        s.health = f.health;
    }
}

void Match::update(float dt) {
    acc_ += std::min(dt, 0.1f);
    while (acc_ >= kTick) {
        acc_ -= kTick;
        tick();
    }
}

void Match::tick() {
    ++ticks_;
    clock_ += kTick;
    for (Furniture& p : furniture_) p.wobble = std::max(0.f, p.wobble - kTick * 3.f);
    if (freeze_ > 0) {
        --freeze_;
        record();
        return;
    }
    phaseTime_ += kTick;
    switch (phase_) {
        case Phase::Intro:
            if (phaseTime_ >= kIntroTime) {
                setPhase(Phase::Fight);
                for (Fighter& f : fighters_) setAct(f, Act::Stand);
                Event e;
                e.type = EventType::Fight;
                push(e);
            }
            break;
        case Phase::Fight:
            if (timed()) {
                timeLeft_ -= kTick;
                if (timeLeft_ <= 0.f) {
                    timeLeft_ = 0.f;
                    setPhase(Phase::TimeUp);
                    Event e;
                    e.type = EventType::TimeUp;
                    push(e);
                }
            }
            break;
        case Phase::Ko:
            if (phaseTime_ >= kKoTime) finishRound();
            break;
        case Phase::TimeUp:
            if (phaseTime_ >= kTimeUpTime) finishRound();
            break;
        case Phase::RoundOver:
            if (phaseTime_ >= kRoundOverTime) {
                const int need = roundsToWin();
                if (wins_[0] >= need || wins_[1] >= need) {
                    setPhase(Phase::MatchOver);
                    Event e;
                    e.type = EventType::MatchOver;
                    e.fighter = matchWinner();
                    push(e);
                } else {
                    startRound();
                }
            }
            break;
        case Phase::MatchOver:
            break;
    }
    if (phase_ == Phase::Intro && phaseTime_ == 0.f) {  // a new round just began
        record();
        return;
    }

    std::array<Input, 2> in{};
    for (int i = 0; i < 2; ++i) {
        if (phase_ != Phase::Fight) continue;
        if (brains_[size_t(i)]) in[size_t(i)] = brains_[size_t(i)]->think(*this, i);
        else in[size_t(i)] = human_[size_t(i)];
    }
    for (int i = 0; i < 2; ++i) stepFighter(i, in[size_t(i)]);
    separate();
    if (phase_ == Phase::Fight) {
        resolveHits();
        const bool ko0 = fighters_[0].ko, ko1 = fighters_[1].ko;
        if (ko0 || ko1) {
            roundWinner_ = ko0 && ko1 ? -1 : (ko0 ? 1 : 0);
            setPhase(Phase::Ko);
            Event e;
            e.type = EventType::Ko;
            e.fighter = ko0 ? 0 : 1;
            e.x = fighters_[size_t(e.fighter)].x;
            e.y = fighters_[size_t(e.fighter)].y;
            push(e);
        }
    }
    for (Fighter& f : fighters_) refresh(f);
    record();
}

// ---------------------------------------------------------------- fighters

void Match::setAct(Fighter& f, Act a) {
    f.from = f.pose;
    f.from.spin = wrapSpin(f.from.spin);
    f.act = a;
    f.frame = 0;
}

void Match::face(Fighter& f, const Fighter& o) {
    const float dx = o.x - f.x;
    if (std::fabs(dx) > 2.f) f.facing = dx > 0.f ? 1 : -1;
}

void Match::stepFighter(int i, const Input& in) {
    Fighter& f = fighters_[size_t(i)];
    const Fighter& o = fighters_[size_t(1 - i)];
    f.prevIn = f.in;
    f.in = in;
    const int pad = in.pad(), prevPad = f.prevIn.pad();
    const bool strikeEdge = in.strike && !f.prevIn.strike;
    const bool dirEdge = pad != 0 && pad != prevPad;
    if (in.strike && (strikeEdge || dirEdge)) {
        f.bufPad = pad ? pad : 5;
        f.bufAge = 0;
    } else if (f.bufPad && ++f.bufAge > kBufferFrames) {
        f.bufPad = 0;
    }
    ++f.frame;
    if (f.flash > 0) --f.flash;

    switch (f.act) {
        case Act::Intro:
            break;
        case Act::Stand:
        case Act::Walk:
        case Act::Guard:
        case Act::Crouch:
            neutral(f, o, pad, prevPad);
            break;
        case Act::PreJump:
            if (in.dx != 0) f.jumpDx = in.dx;
            if (f.frame >= kPreJumpFrames) {
                f.airborne = true;
                f.support = -1;
                f.peakY = f.y;
                f.airAttack = false;
                f.vx = float(f.jumpDx) * kJumpVx;
                f.vy = -kJumpSpeed;
                setAct(f, Act::Air);
                Event e;
                e.type = EventType::Jump;
                e.fighter = f.id;
                e.x = f.x;
                e.y = f.y;
                push(e);
            }
            break;
        case Act::Air:
            if (f.bufPad && !f.airAttack) {
                const Move m = moveFor(f.bufPad, f.facing, true);
                f.bufPad = 0;
                startMove(f, m);
            }
            break;
        case Act::Land:
            if (f.frame >= f.stun) {
                setAct(f, Act::Stand);
                neutral(f, o, pad, prevPad);
            }
            break;
        case Act::Roll:
            f.vx = float(f.rollDir) * kRollSpeed;
            if (f.frame >= kRollFrames) {
                f.vx = 0.f;
                setAct(f, Act::Stand);
                face(f, o);
            }
            break;
        case Act::Attack:
            stepAttack(f);
            break;
        case Act::Hit:
        case Act::Block:
            if (f.act == Act::Block) f.crouchBlock = in.dy > 0;
            if (!f.airborne) f.vx *= kFriction;
            if (f.frame >= f.stun && !f.airborne) {
                setAct(f, Act::Stand);
                f.vx = 0.f;
            }
            break;
        case Act::Fall:
        case Act::Ko:
        case Act::Win:
            break;
        case Act::Down:
            if (f.frame >= kDownFrames) setAct(f, Act::GetUp);
            break;
        case Act::GetUp:
            if (f.frame >= kGetUpFrames) {
                setAct(f, Act::Stand);
                face(f, o);
            }
            break;
    }
    physics(f);
}

void Match::neutral(Fighter& f, const Fighter& o, int pad, int prevPad) {
    face(f, o);
    if (f.bufPad) {
        const Move m = moveFor(f.bufPad, f.facing, false);
        f.bufPad = 0;
        startMove(f, m);
        return;
    }
    const Input& in = f.in;
    f.vx = 0.f;
    if (in.dy < 0) {
        f.jumpDx = in.dx;
        setAct(f, Act::PreJump);
        return;
    }
    if ((pad == 1 || pad == 3) && pad != prevPad) {
        f.rollDir = in.dx;
        setAct(f, Act::Roll);
        Event e;
        e.type = EventType::Jump;  // a whoosh
        e.fighter = f.id;
        e.x = f.x;
        e.y = f.y;
        e.amount = -1.f;
        push(e);
        return;
    }
    Act next = Act::Stand;
    if (in.dy > 0) {
        next = Act::Crouch;
    } else if (in.center) {
        next = Act::Guard;
    } else if (in.dx != 0) {
        next = Act::Walk;
        f.vx = float(in.dx) * (in.dx == f.facing ? kWalkFwd : kWalkBack);
        f.walkPhase += std::fabs(f.vx) * kTick / 26.f;
    }
    if (next != f.act) setAct(f, next);
}

void Match::startMove(Fighter& f, Move m) {
    const MoveInfo& mi = moveInfo(m);
    setAct(f, Act::Attack);
    f.move = m;
    f.connected = false;
    if (mi.airborne && !f.airborne) {  // a jumping attack takes off now
        f.airborne = true;
        f.support = -1;
        f.peakY = f.y;
        f.vx = mi.jumpVx * float(f.facing);
        f.vy = mi.jumpVy;
        Event e;
        e.type = EventType::Jump;
        e.fighter = f.id;
        e.x = f.x;
        e.y = f.y;
        push(e);
    } else if (!f.airborne) {
        f.vx = 0.f;
    }
    if (f.airborne) f.airAttack = true;
    if (m == Move::DiveKick) {
        f.vx = kDiveVx * float(f.facing);
        f.vy = kDiveVy;
    }
    if (m == Move::Roundhouse || m == Move::FlyingKick || m == Move::DiveKick || m == Move::Uppercut ||
        m == Move::Backflip || m == Move::Sweep) {
        Event e;
        e.type = EventType::Kiai;
        e.fighter = f.id;
        e.move = m;
        push(e);
    }
}

void Match::stepAttack(Fighter& f) {
    const MoveInfo& mi = moveInfo(f.move);
    if (f.frame == mi.startup) {
        Event e;
        e.type = EventType::Swing;
        e.fighter = f.id;
        e.move = f.move;
        e.x = f.x;
        e.y = f.y;
        push(e);
    }
    if (f.airborne) return;  // air attacks end when they land
    f.vx = f.frame < mi.startup + mi.active ? mi.lunge * float(f.facing) : 0.f;
    if (f.frame >= mi.startup + mi.active + mi.recovery) {
        f.move = Move::None;
        f.vx = 0.f;
        setAct(f, mi.crouched && f.in.dy > 0 ? Act::Crouch : Act::Stand);
    }
}

void Match::physics(Fighter& f) {
    auto walls = [&f] {
        if (f.x < kWallL) {
            f.x = kWallL;
            if (f.vx < 0.f) f.vx = 0.f;
        } else if (f.x > kWallR) {
            f.x = kWallR;
            if (f.vx > 0.f) f.vx = 0.f;
        }
    };
    if (f.act == Act::Intro || f.act == Act::Win) return;
    if (f.airborne) {
        const bool dive = f.act == Act::Attack && f.move == Move::DiveKick;
        if (!dive) f.vy = std::min(f.vy + kGravity * kTick, kMaxFall);
        const float fromY = f.y;
        f.x += f.vx * kTick;
        f.y += f.vy * kTick;
        f.peakY = std::min(f.peakY, f.y);
        walls();
        int support = -1;
        float top = 0.f;
        if (f.vy > 0.f && findLanding(f.x, fromY, f.y, support, top)) {
            f.y = top;
            land(f, support);
        }
        return;
    }
    f.x += f.vx * kTick;
    walls();
    if (f.support >= 0) {
        const Furniture& p = furniture_[size_t(f.support)];
        if (p.broken || f.x < p.x0() - 1.f || f.x > p.x1() + 1.f) goAirborne(f);
    }
}

void Match::goAirborne(Fighter& f) {
    f.airborne = true;
    f.support = -1;
    f.vy = std::min(f.vy, 0.f);
    f.peakY = f.y;
    switch (f.act) {
        case Act::Hit:
        case Act::Block:
        case Act::Fall:
        case Act::Ko:
            break;
        case Act::Down:
        case Act::GetUp:
            setAct(f, Act::Fall);
            f.frame = 24;  // already lying flat
            break;
        case Act::Attack:
            if (moveInfo(f.move).airborne) break;
            [[fallthrough]];
        default:
            f.move = Move::None;
            f.airAttack = false;
            setAct(f, Act::Air);
            break;
    }
}

bool Match::findLanding(float x, float fromY, float toY, int& support, float& top) const {
    bool found = false;
    top = 1e9f;
    if (toY >= kFloorY) {
        found = true;
        support = -1;
        top = kFloorY;
    }
    for (size_t i = 0; i < furniture_.size(); ++i) {
        const Furniture& p = furniture_[i];
        if (p.broken) continue;
        const float t = p.topY();
        if (fromY <= t + 0.001f && toY >= t && x >= p.x0() && x <= p.x1() && t < top) {
            found = true;
            support = int(i);
            top = t;
        }
    }
    return found;
}

void Match::land(Fighter& f, int support) {
    const float drop = f.y - f.peakY;
    f.airborne = false;
    f.support = support;
    f.vy = 0.f;
    if (support >= 0) furniture_[size_t(support)].wobble = 1.f;
    Event e;
    e.type = EventType::Land;
    e.fighter = f.id;
    e.x = f.x;
    e.y = f.y;
    e.amount = drop;
    e.piece = support;
    switch (f.act) {
        case Act::Attack: {
            const MoveInfo& mi = moveInfo(f.move);
            e.heavy = f.move == Move::DiveKick;
            if (f.move == Move::DiveKick && support >= 0) damagePiece(support, f.x);
            f.move = Move::None;
            f.vx = 0.f;
            setAct(f, Act::Land);
            f.stun = mi.landLag;
            break;
        }
        case Act::Fall:
            f.vx = 0.f;
            e.type = EventType::Knockdown;
            e.heavy = true;
            if (support >= 0) damagePiece(support, f.x);
            setAct(f, f.ko ? Act::Ko : Act::Down);
            break;
        case Act::Ko:
            f.vx = 0.f;
            e.type = EventType::Knockdown;
            e.heavy = true;
            break;
        case Act::Hit:
        case Act::Block:
            f.vx *= 0.5f;
            break;
        default:
            f.vx = 0.f;
            f.move = Move::None;
            setAct(f, Act::Land);
            f.stun = kLandFrames;
            if (drop > 60.f && support >= 0) damagePiece(support, f.x);
            break;
    }
    push(e);
}

void Match::damagePiece(int piece, float x) {
    Furniture& p = furniture_[size_t(piece)];
    p.wobble = 1.f;
    if (p.broken || p.hp < 0) return;
    --p.hp;
    Event e;
    e.type = p.hp <= 0 ? EventType::Break : EventType::Crack;
    e.piece = piece;
    e.x = x;
    e.y = p.topY();
    push(e);
    if (p.hp > 0) return;
    p.broken = true;
    // Whatever stood on it goes down with it.
    for (size_t i = 0; i < furniture_.size(); ++i) {
        Furniture& q = furniture_[i];
        if (int(i) == piece || q.broken || q.base <= 0.f) continue;
        if (q.x + float(q.spec().w) > p.x && q.x < p.x + float(p.spec().w) && std::fabs(q.baseY() - p.topY()) < 1.f) {
            q.broken = true;
            q.hp = 0;
            Event b;
            b.type = EventType::Break;
            b.piece = int(i);
            b.x = q.x + float(q.spec().w) * 0.5f;
            b.y = q.topY();
            push(b);
        }
    }
}

void Match::separate() {
    Fighter& a = fighters_[0];
    Fighter& b = fighters_[1];
    auto solid = [](const Fighter& f) {
        return f.act != Act::Roll && f.act != Act::Down && f.act != Act::Ko && f.act != Act::Fall;
    };
    if (!solid(a) || !solid(b)) return;
    const float overlap = std::min(a.y, b.y) - std::max(a.skel.top, b.skel.top);
    if (overlap < 6.f) return;  // one is clear above the other
    const float dx = b.x - a.x;
    const float gap = kPushWidth - std::fabs(dx);
    if (gap <= 0.f) return;
    const float sign = dx > 0.f ? 1.f : (dx < 0.f ? -1.f : float(a.facing));
    a.x -= sign * gap * 0.5f;
    b.x += sign * gap * 0.5f;
    // Against a wall, the other one gives way.
    if (a.x < kWallL || a.x > kWallR) {
        a.x = std::clamp(a.x, kWallL, kWallR);
        b.x = a.x + sign * kPushWidth;
    }
    if (b.x < kWallL || b.x > kWallR) {
        b.x = std::clamp(b.x, kWallL, kWallR);
        a.x = b.x - sign * kPushWidth;
    }
}

bool Match::testHit(const Fighter& a, const Fighter& d, Vec& point) const {
    if (a.act != Act::Attack || a.connected) return false;
    const MoveInfo& mi = moveInfo(a.move);
    if (a.frame < mi.startup || a.frame >= mi.startup + mi.active) return false;
    switch (d.act) {
        case Act::Fall:
        case Act::Down:
        case Act::GetUp:
        case Act::Ko:
        case Act::Intro:
        case Act::Win:
            return false;
        default:
            break;
    }
    const Vec s = strikePoint(a);
    for (const Capsule& c : hurtCapsules(d.skel))
        if (segmentDistance(s, c.a, c.b) <= c.r + mi.hitR) {
            point = s;
            return true;
        }
    return false;
}

void Match::resolveHits() {
    // Both checks see the same moment, so two attacks can trade.
    Vec p[2];
    const bool hit0 = testHit(fighters_[0], fighters_[1], p[0]);
    const bool hit1 = testHit(fighters_[1], fighters_[0], p[1]);
    const Move m0 = fighters_[0].move, m1 = fighters_[1].move;
    if (hit0) applyHit(0, m0, p[0]);
    if (hit1) applyHit(1, m1, p[1]);
}

void Match::applyHit(int attacker, Move move, Vec point) {
    Fighter& a = fighters_[size_t(attacker)];
    Fighter& d = fighters_[size_t(1 - attacker)];
    const MoveInfo& mi = moveInfo(move);
    a.connected = true;
    const float dir = d.x > a.x ? 1.f : (d.x < a.x ? -1.f : float(a.facing));

    Level level = mi.level;
    const float elevation = d.y - a.y;  // > 0: the attacker is higher up
    if (elevation > kElevation) level = Level::Overhead;
    else if (elevation < -kElevation) level = Level::Low;
    const bool facingIt = (a.x - d.x) * float(d.facing) >= -4.f;
    const bool standGuard = d.act == Act::Guard || (d.act == Act::Block && !d.crouchBlock);
    const bool crouchGuard = d.act == Act::Crouch || (d.act == Act::Block && d.crouchBlock);
    const bool blocked = facingIt && !d.airborne &&
                         ((standGuard && level != Level::Low) || (crouchGuard && level != Level::Overhead));

    Event e;
    e.fighter = d.id;
    e.x = point.x;
    e.y = point.y;
    e.move = move;
    if (blocked) {
        d.crouchBlock = crouchGuard;
        d.move = Move::None;
        setAct(d, Act::Block);
        d.stun = mi.blockstun;
        d.vx = dir * mi.push * 0.6f;
        freeze_ = 5;
        e.type = EventType::Block;
        push(e);
        return;
    }

    float damage = mi.damage;
    if (move == Move::DiveKick) damage += std::clamp((d.y - a.peakY - 30.f) * 0.35f, 0.f, 14.f);
    const bool wasCrouching = d.act == Act::Crouch || (d.act == Act::Attack && moveInfo(d.move).crouched);
    d.health = std::max(0.f, d.health - damage);
    d.flash = 8;
    d.move = Move::None;
    if (d.health <= 0.f) d.ko = true;
    const bool down = mi.knockdown || d.airborne || d.ko;
    if (down) {
        setAct(d, Act::Fall);
        d.airborne = true;
        d.support = -1;
        d.peakY = d.y;
        d.facing = dir > 0.f ? -1 : 1;  // falls away from the blow
        d.vx = dir * std::max(mi.push, 90.f);
        d.vy = move == Move::Uppercut ? -250.f : -165.f;
    } else {
        setAct(d, Act::Hit);
        d.lowHit = wasCrouching;
        d.stun = mi.hitstun;
        d.vx = dir * mi.push;
    }
    freeze_ = down ? 10 : 7;
    e.type = EventType::Hit;
    e.amount = damage;
    e.heavy = down;
    push(e);
}

// ---------------------------------------------------------------- poses

Pose Match::attackPose(const Fighter& f) const {
    const MoveInfo& mi = moveInfo(f.move);
    const Pose& chamber = pose(mi.chamber);
    const Pose& strike = pose(mi.strike);
    const Pose& finish = pose(mi.finish);
    const int fr = f.frame;
    if (fr < mi.startup) {
        const float t = float(fr + 1) / float(mi.startup);
        if (t < 0.6f) return lerp(f.from, chamber, smooth01(t / 0.6f));
        return lerp(chamber, strike, (t - 0.6f) / 0.4f);
    }
    if (f.move == Move::Backflip && fr >= mi.startup + mi.active) {
        Pose p = pose(PoseId::Tuck);
        const float k = float(fr - mi.startup - mi.active) / 26.f;
        p.spin = -360.f * smooth01(k);
        return lerp(finish, p, clamp01(k * 5.f));
    }
    if (fr < mi.startup + mi.active) {
        const float t = mi.airborne ? clamp01(float(fr - mi.startup) / 5.f) : float(fr - mi.startup + 1) / float(mi.active);
        return lerp(strike, finish, t);
    }
    if (mi.recovery <= 0) return finish;
    const float t = float(fr - mi.startup - mi.active + 1) / float(mi.recovery);
    return lerp(finish, pose(mi.crouched ? PoseId::Crouch : PoseId::Stance), smooth01(t));
}

Pose Match::targetPose(const Fighter& f) const {
    constexpr float kTwoPi = 6.28318531f;
    switch (f.act) {
        case Act::Intro:
            if (f.frame < 40) return pose(PoseId::Bow);
            return lerp(pose(PoseId::Bow), pose(PoseId::Stance), smooth01(float(f.frame - 40) / 24.f));
        case Act::Stand: {
            const float b = 0.5f - 0.5f * std::cos(clock_ * 3.6f + float(f.id) * 1.7f);
            return lerp(pose(PoseId::Stance), pose(PoseId::StanceLow), b);
        }
        case Act::Walk:
            return lerp(pose(PoseId::Walk1), pose(PoseId::Walk2), 0.5f - 0.5f * std::cos(f.walkPhase * kTwoPi));
        case Act::Guard:
            return pose(PoseId::Guard);
        case Act::Crouch:
            return pose(PoseId::Crouch);
        case Act::PreJump:
        case Act::Land:
            return pose(PoseId::PreJump);
        case Act::Air:
            return pose(PoseId::Tuck);
        case Act::Roll: {
            Pose p = pose(PoseId::Tuck);
            const float dir = f.rollDir == f.facing ? 1.f : -1.f;
            p.spin = dir * 360.f * float(f.frame) / float(kRollFrames);
            return p;
        }
        case Act::Attack:
            return attackPose(f);
        case Act::Hit:
            return pose(f.lowHit ? PoseId::HitCrouch : PoseId::Hit);
        case Act::Block:
            return pose(f.crouchBlock ? PoseId::Crouch : PoseId::Guard);
        case Act::Fall:
            return lerp(pose(PoseId::Fall), pose(PoseId::Lying), smooth01(float(f.frame) / 30.f));
        case Act::Down:
        case Act::Ko:
            return pose(PoseId::Lying);
        case Act::GetUp:
            if (f.frame < 12) return lerp(pose(PoseId::Lying), pose(PoseId::Crouch), smooth01(float(f.frame) / 12.f));
            return lerp(pose(PoseId::Crouch), pose(PoseId::Stance), smooth01(float(f.frame - 12) / 12.f));
        case Act::Win:
            return lerp(pose(PoseId::Stance), pose(PoseId::Win), smooth01(float(f.frame) / 14.f));
    }
    return pose(PoseId::Stance);
}

void Match::refresh(Fighter& f) {
    Pose p = targetPose(f);
    const bool blends = f.act != Act::Attack && f.act != Act::Roll && f.act != Act::Intro && f.act != Act::Fall;
    if (blends && f.frame < kBlend) p = lerp(f.from, p, float(f.frame + 1) / float(kBlend + 1));
    f.pose = p;
    f.skel = solve(f.pose, f.x, f.y, f.facing);
}

}  // namespace kara
