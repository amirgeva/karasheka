#pragma once

// Computer opponents. A Brain plays through the same nine-key input a human
// uses, sees its opponent a few frames late (more at lower levels) and picks
// moves by range, by what the opponent is doing, and by the furniture around.

#include <cstdint>

#include "core/fight.h"

namespace kara {

class Brain {
public:
    Brain(Control level, uint64_t seed);
    Input think(const Match& m, int self);

private:
    struct Skill {
        int delay;         // frames of perception lag
        int think;         // frames between decisions
        float defend;      // chance to guard an attack it sees coming
        float read;        // chance that guard is at the right level
        float antiAir;     // chance to meet a jump-in with an uppercut
        float aggression;  // chance to attack when in range
        float slop;        // error in its range judgement, px
        float punish;      // chance to punish a whiffed move
        float climb;       // chance to go for the furniture
        float air;         // chance that a jump is meant to attack
        float flashy;      // how much it favours the knockdowns (flying kick, sweep, roundhouse)
    };

    Input attack(Move m, int facing);
    Input emit(const Input& in);
    Input airborne(const Match& m, const Fighter& me, const Snapshot& op);
    bool threatened(const Match& m, const Fighter& me, const Snapshot& op, Input& out);
    bool decide(const Match& m, const Fighter& me, const Snapshot& op, Input& out);
    int pickPiece(const Match& m, const Fighter& me, const Snapshot& op);
    bool inRange(Move mv, float dist, float slop) const;

    Skill skill_;
    Rng rng_;
    Input out_;            // held input
    Input last_;           // what was emitted on the previous tick
    Input pending_;        // an attack waiting for the strike key to be released first
    bool hasPending_ = false;
    int hold_ = 0;         // ticks to keep holding out_ unchanged
    int cooldown_ = 0;     // ticks until the next decision
    int seenAttack_ = -1;  // the opponent attack it already reacted to (tick it started)
    int seenJump_ = -1;    // the opponent jump it already judged
    bool antiAirPlan_ = false;
    bool airPlan_ = false;      // this jump is meant to attack
    bool wasAirborne_ = false;
    int goal_ = -1;        // furniture it is heading for
    int goalTicks_ = 0;
    int perch_ = 0;        // ticks spent up on furniture
};

}  // namespace kara
