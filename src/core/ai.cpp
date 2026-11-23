#include "core/ai.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace kara {
namespace {

bool controllable(Act a) { return a == Act::Stand || a == Act::Walk || a == Act::Guard || a == Act::Crouch; }

Input walk(int dx) {
    Input in;
    in.dx = dx;
    return in;
}

// The guard key, with up for a high guard or down for a low one.
Input guard(Level level) {
    Input in;
    in.guard = true;
    in.dy = level == Level::High ? -1 : (level == Level::Low ? 1 : 0);
    return in;
}

Input jump(int dx) {
    Input in;
    in.dx = dx;
    in.dy = -1;
    return in;
}

int sign(float v) { return v > 0.f ? 1 : (v < 0.f ? -1 : 0); }

// Seconds after take-off at which a jump comes back down through height h
// above where it started, or -1 if it never gets that high.
float descendTime(float h) {
    const float disc = kJumpSpeed * kJumpSpeed - 2.f * kGravity * h;
    if (disc < 0.f) return -1.f;
    return (kJumpSpeed + std::sqrt(disc)) / kGravity;
}

}  // namespace

Brain::Brain(Control level, uint64_t seed) : rng_(seed) {
    static const Skill kSkills[3] = {
        // Easy is meant to lose to a beginner: slow, timid, sloppy with range,
        // and mostly plain punches and kicks rather than the knockdowns and
        // attacks from the air a beginner can't yet read.
        // delay think defend read antiAir aggression slop punish climb air flashy
        {24, 20, 0.15f, 0.50f, 0.10f, 0.34f, 16.f, 0.05f, 0.05f, 0.30f, 0.30f},  // easy
        {12, 9, 0.50f, 0.75f, 0.40f, 0.55f, 6.f, 0.40f, 0.40f, 0.85f, 1.00f},    // normal
        {7, 5, 0.82f, 0.92f, 0.72f, 0.68f, 2.f, 0.80f, 0.45f, 1.00f, 1.00f},     // hard
    };
    skill_ = kSkills[std::clamp(int(level) - 1, 0, 2)];
}

Input Brain::emit(const Input& in) {
    last_ = in;
    return in;
}

// The input that performs move `m` for a fighter facing `facing`.
Input Brain::attack(Move m, int facing) {
    Input in;
    in.strike = true;
    switch (m) {
        case Move::Punch: in.center = true; break;
        case Move::FrontKick: in.dx = facing; break;
        case Move::Roundhouse: in.dx = -facing; break;
        case Move::Uppercut: in.dy = -1; break;
        case Move::LowPunch: in.dx = -facing; in.dy = 1; break;
        case Move::LowKick: in.dy = 1; break;
        case Move::Sweep: in.dx = facing; in.dy = 1; break;
        case Move::FlyingKick: in.dx = facing; in.dy = -1; break;
        case Move::Backflip: in.dx = -facing; in.dy = -1; break;
        case Move::DiveKick: in.dy = 1; break;
        default: break;
    }
    return in;
}

bool Brain::inRange(Move mv, float dist, float slop) const {
    const float judged = dist + slop;
    const float body = 6.f;  // the opponent's half width
    if (mv == Move::FlyingKick) return judged > 34.f && judged - body < moveReach(mv).ahead;
    return judged - body < moveReach(mv).ahead;
}

Input Brain::think(const Match& m, int self) {
    const Fighter& me = m.fighter(self);
    const Snapshot& op = m.seen(1 - self, skill_.delay);
    if (!op.airborne) {
        seenJump_ = -1;
        antiAirPlan_ = false;
    }

    // A strike needs a fresh key press, so an attack straight after another
    // first lets go of the key for a tick.
    auto press = [&](const Input& in, int hold) {
        if (in.strike && last_.strike) {
            pending_ = in;
            hasPending_ = true;
            out_ = Input{};
            hold_ = 0;
            return emit(out_);
        }
        out_ = in;
        hold_ = hold;
        return emit(out_);
    };

    if (hasPending_) {
        hasPending_ = false;
        out_ = pending_;
        hold_ = 2;
        return emit(out_);
    }
    if (me.airborne && !wasAirborne_) airPlan_ = rng_.uniform() < skill_.air;
    wasAirborne_ = me.airborne;
    if (me.airborne) {
        // A jump is one press: decide afresh after landing.
        out_ = Input{};
        hold_ = 0;
        cooldown_ = 0;
        if (me.act == Act::Air) return emit(airborne(m, me, op));
    }
    if (me.act == Act::Roll) {
        // So is a roll. Still holding down-forward when it ends would be a
        // fresh press and roll again, over and over into the wall.
        out_ = Input{};
        hold_ = 0;
        cooldown_ = 0;
        return emit(out_);
    }
    if (hold_ > 0) {
        --hold_;
        return emit(out_);
    }
    if (out_.strike) out_ = Input{};  // let go after an attack

    if (!controllable(me.act)) {
        // Keep guarding through block stun; otherwise wait.
        if (me.act == Act::Block) return emit(guard(me.guard));
        return emit(Input{});
    }

    Input want;
    if (threatened(m, me, op, want)) return press(want, want.strike ? 2 : std::max(4, hold_));
    if (--cooldown_ > 0) return emit(out_);
    cooldown_ = skill_.think + rng_.range(0, skill_.think / 2);
    if (decide(m, me, op, want)) return press(want, want.strike ? 2 : 0);
    return emit(out_);
}

Input Brain::airborne(const Match& m, const Fighter& me, const Snapshot& op) {
    (void)m;
    if (me.airAttack || !airPlan_) return Input{};
    const float ahead = (op.x - me.x) * float(me.facing);
    const float above = op.y - me.y;  // > 0: we are higher
    auto strike = [&](Move mv) {
        Input in = attack(mv, me.facing);
        if (me.airFrames + moveInfo(mv).startup < kMinStrikeFrames) return Input{};  // not allowed yet
        if (last_.strike) return Input{};  // release first; try again next tick
        return in;
    };
    // The dive kick drops along a steep line; go when that line meets the
    // opponent's upper body.
    if (above > 16.f && me.vy > -90.f && ahead > 0.f) {
        const float t = std::max(0.f, above - 30.f) / kDiveVy;
        const float need = 15.f + kDiveVx * t;
        if (std::fabs(ahead - need) < 7.f + skill_.slop) return strike(Move::DiveKick);
    }
    if (ahead > 0.f && ahead < 30.f && above > -20.f && above < 34.f && me.vy > -140.f) return strike(Move::AirKick);
    return Input{};
}

bool Brain::threatened(const Match& m, const Fighter& me, const Snapshot& op, Input& out) {
    const float dist = std::fabs(op.x - me.x);
    // An attack coming out within reach: guard (or duck) it.
    if (op.act == Act::Attack && op.move != Move::None) {
        const MoveInfo& mi = moveInfo(op.move);
        const int started = m.ticks() - skill_.delay - op.frame;
        if (started != seenAttack_ && op.frame < mi.startup + std::min(mi.active, 10)) {
            seenAttack_ = started;
            const Reach& r = moveReach(op.move);
            if (dist < r.ahead + 12.f && rng_.uniform() < skill_.defend) {
                Level level = mi.level;
                const float elevation = me.y - op.y;  // > 0: the attacker is higher
                if (elevation > kElevation) level = Level::High;
                else if (elevation < -kElevation) level = Level::Low;
                if (rng_.uniform() >= skill_.read) level = Level((int(level) + 1 + int(rng_.below(2))) % 3);  // misread
                out = guard(level);
                hold_ = std::clamp(mi.startup + std::min(mi.active, 10) - op.frame + 8, 4, 26);
                return true;
            }
        }
    }
    // A jump coming in: decide once per jump whether to meet it.
    if (op.airborne && (op.act == Act::Air || op.act == Act::Attack)) {
        if (seenJump_ < 0) {
            seenJump_ = m.ticks();
            antiAirPlan_ = rng_.uniform() < skill_.antiAir;
        }
        const bool coming = (op.x - me.x) * op.vx < 0.f || dist < 22.f;
        // Where the jumper will be (it is seen `delay` ticks late) when an
        // uppercut started now is out.
        const float t = float(moveInfo(Move::Uppercut).startup + 2 + skill_.delay) * kTick;
        const float px = op.x + op.vx * t;
        const float py = op.y + op.vy * t + 0.5f * kGravity * t * t;
        const float pdist = std::fabs(px - me.x), above = me.y - py;
        if (antiAirPlan_ && coming && pdist < 24.f && above > 4.f && above < 56.f) {
            antiAirPlan_ = false;
            out = attack(Move::Uppercut, me.facing);
            return true;
        }
        if (!antiAirPlan_ && coming && dist < 46.f && op.act == Act::Attack && rng_.uniform() < skill_.defend * 0.25f) {
            out = guard(Level::High);  // attacks from the air land high
            hold_ = 14;
            return true;
        }
    }
    // A move that missed and is still recovering: punish it.
    const bool recovering =
        (op.act == Act::Attack && op.move != Move::None &&
         op.frame >= moveInfo(op.move).startup + moveInfo(op.move).active && !moveInfo(op.move).airborne) ||
        (op.act == Act::Land && op.frame < 6);
    if (recovering && op.frame <= 2 && rng_.uniform() < skill_.punish) {
        // Only with a move that lands before they recover.
        int left = 6;  // a landing
        if (op.act == Act::Attack) {
            const MoveInfo& mi = moveInfo(op.move);
            left = mi.startup + mi.active + mi.recovery;
        }
        left -= op.frame + skill_.delay;
        for (Move mv : {Move::Punch, Move::LowPunch, Move::FrontKick, Move::Sweep}) {
            if (moveInfo(mv).startup < left && inRange(mv, dist, 0.f)) {
                out = attack(mv, me.facing);
                return true;
            }
        }
    }
    return false;
}

int Brain::pickPiece(const Match& m, const Fighter& me, const Snapshot& op) {
    const auto& pieces = m.furniture();
    int best = -1;
    float bestScore = -1e9f;
    for (size_t i = 0; i < pieces.size(); ++i) {
        const Furniture& p = pieces[i];
        if (p.broken || int(i) == me.support) continue;
        const float h = me.y - p.topY();
        if (h < 8.f || h > 38.f) continue;
        const float cx = (p.x0() + p.x1()) * 0.5f;
        if (p.x1() - p.x0() < 10.f) continue;
        const float score = -std::fabs(cx - me.x) * 0.6f - std::fabs(cx - op.x) * 0.4f + h * 0.4f + rng_.uniform(0.f, 30.f);
        if (score > bestScore) {
            bestScore = score;
            best = int(i);
        }
    }
    return best;
}

bool Brain::decide(const Match& m, const Fighter& me, const Snapshot& op, Input& out) {
    const int fwd = me.facing;
    const float dx = op.x - me.x;
    const float dist = std::fabs(dx);
    const float above = op.y - me.y;  // > 0: we are higher
    const bool perched = me.support >= 0 && kFloorY - me.y > 8.f;
    const float r = rng_.uniform();

    // Up on furniture: wait for the opponent to wander into diving range.
    if (perched) {
        perch_ += skill_.think;
        if (above > 10.f && dist > 12.f && dist < 26.f + 0.55f * (above + 44.f) && r < 0.75f) {
            out = jump(sign(dx));
            return true;
        }
        if (perch_ > 160 || (above < 6.f && dist < 40.f)) {
            perch_ = 0;
            out = jump(sign(dx));
            return true;
        }
        out = r < 0.5f ? guard(Level::Low) : Input{};  // attacks from below land low
        return true;
    }
    perch_ = 0;

    // The opponent is up on furniture: don't stand underneath.
    if (above < -kElevation) {
        if (dist < 22.f) out = walk(-sign(dx));
        else if (dist < 44.f && r < 0.25f) out = jump(sign(dx));
        else if (dist > 50.f) out = walk(sign(dx));
        else out = r < 0.6f ? guard(Level::High) : Input{};
        return true;
    }

    // The opponent is down or getting up: keep a little distance.
    if (op.act == Act::Down || op.act == Act::GetUp || op.act == Act::Fall) {
        if (dist < 28.f) out = walk(-sign(dx));
        else if (dist > 44.f) out = walk(sign(dx));
        else out = guard(Level::Mid);
        return true;
    }

    // Heading for a piece of furniture.
    if (goal_ < 0 && dist > 46.f && r < skill_.climb * 0.35f) {
        goal_ = pickPiece(m, me, op);
        goalTicks_ = 0;
    }
    if (goal_ >= 0) {
        const auto& pieces = m.furniture();
        goalTicks_ += skill_.think;
        if (goal_ >= int(pieces.size()) || pieces[size_t(goal_)].broken || goalTicks_ > 300 || me.support == goal_ ||
            dist < 30.f) {
            goal_ = -1;
        } else {
            const Furniture& p = pieces[size_t(goal_)];
            const float h = me.y - p.topY();
            const float t = descendTime(h + 3.f);
            if (t < 0.f) {
                goal_ = -1;
            } else {
                const float cx = (p.x0() + p.x1()) * 0.5f;
                for (int dir : {0, sign(cx - me.x)}) {
                    const float lx = me.x + float(dir) * kJumpVx * t;
                    if (lx > p.x0() + 3.f && lx < p.x1() - 3.f) {
                        out = jump(dir);
                        return true;
                    }
                }
                out = walk(sign(cx - me.x));
                return true;
            }
        }
    }

    // Attack when something is in range.
    if (r < skill_.aggression) {
        struct Option {
            Move move;
            float weight;
        };
        const bool opGuard = op.act == Act::Guard || op.act == Act::Block;
        const bool opCrouch = op.act == Act::Crouch;
        const float slop = rng_.uniform(-skill_.slop, skill_.slop);
        Option opts[] = {
            {Move::Punch, opCrouch ? 1.f : 3.f},
            {Move::FrontKick, opCrouch ? 1.f : 3.f},
            {Move::LowPunch, 2.f},
            {Move::LowKick, 2.f},
            {Move::Sweep, 2.f},
            {Move::Roundhouse, opCrouch ? 0.f : 2.f},  // a crouch ducks it
            {Move::Uppercut, op.airborne ? 6.f : (opCrouch ? 0.f : 0.7f)},
            {Move::FlyingKick, 0.7f},
        };
        float total = 0.f;
        for (Option& o : opts) {
            if (!inRange(o.move, dist, slop)) o.weight = 0.f;
            const bool flashy = o.move == Move::Sweep || o.move == Move::Roundhouse || o.move == Move::FlyingKick;
            if (flashy && rng_.uniform() >= skill_.flashy) o.weight = 0.f;
            if (opGuard) o.weight *= moveInfo(o.move).level == op.guard ? 0.15f : 2.f;  // go around the guard
            total += o.weight;
        }
        if (total > 0.f) {
            float pick = rng_.uniform() * total;
            for (const Option& o : opts) {
                if (o.weight <= 0.f) continue;
                pick -= o.weight;
                if (pick <= 0.f) {
                    out = attack(o.move, fwd);
                    return true;
                }
            }
        }
    }

    // Footsies.
    const float preferred = 26.f + rng_.uniform(0.f, 12.f);
    const float r2 = rng_.uniform();
    if (dist > preferred + 8.f) {
        if (dist > 56.f && r2 < 0.06f) out = jump(sign(dx));
        else out = walk(sign(dx));
    } else if (dist < 18.f) {
        out = r2 < 0.15f ? Input::fromPad(fwd > 0 ? 3 : 1, false) : walk(-sign(dx));  // roll through, or back off
    } else if (r2 < 0.45f) {
        out = guard(Level(std::min(2, int(r2 / 0.15f))));  // a guess: mid, low or high
    } else if (r2 < 0.7f) {
        out = walk(sign(dx));
    } else if (r2 < 0.85f) {
        out = walk(-sign(dx));
    } else {
        out = Input{};
    }
    return true;
}

}  // namespace kara
