// Headless checks of the skeleton, the moves, guarding, furniture, the
// computer opponents and whole matches.
//
// Usage: kara_tests [matches]   (CPU matches to play per pairing, default 6)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "core/fight.h"

using namespace kara;

namespace {

int g_failures = 0;

#define CHECK(cond, ...)                                                  \
    do {                                                                  \
        if (!(cond)) {                                                    \
            ++g_failures;                                                 \
            std::printf("  FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond); \
            std::printf(__VA_ARGS__);                                     \
            std::printf("\n");                                            \
        }                                                                 \
    } while (0)

Input pad(int p, bool strike = false) { return Input::fromPad(p, strike); }

// Two human fighters, intro skipped, standing `gap` apart on the floor of
// the given room with RED on the left.
struct Duel {
    Match m;
    explicit Duel(float gap = 30.f, int room = 0) : m(setup(room)) {
        m.skipIntro();
        place(0, 160.f - gap * 0.5f);
        place(1, 160.f + gap * 0.5f);
    }
    static MatchSetup setup(int room) {
        MatchSetup s;
        s.control = {{Control::Human, Control::Human}};
        s.room = room;
        s.roundTime = 0;
        s.seed = 7;
        return s;
    }
    void place(int i, float x, float y = kFloorY, int support = -1) {
        Fighter& f = m.fighterForTest(i);
        f.x = x;
        f.y = y;
        f.support = support;
        f.airborne = false;
        f.facing = i == 0 ? 1 : -1;
        m.refresh(f);
    }
    // Runs n ticks with these inputs held.
    void run(int n, Input a, Input b = Input{}) {
        for (int i = 0; i < n; ++i) {
            m.setInput(0, a);
            m.setInput(1, b);
            m.tick();
        }
    }
    // RED presses an attack (strike edge) while BLUE holds `hold`, then both
    // keep going for n ticks.
    void attack(int p, Input hold, int n = 50) {
        run(4, Input{}, hold);  // settle into the stance (a guard, a crouch)
        run(1, pad(p, true), hold);
        run(n, Input{}, hold);
    }
    const Fighter& f(int i) const { return m.fighter(i); }
    std::vector<Event> events() { return m.takeEvents(); }
};

int countEvents(const std::vector<Event>& ev, EventType t) {
    int n = 0;
    for (const Event& e : ev) n += e.type == t;
    return n;
}

void testInput() {
    std::printf("controls\n");
    for (int p = 1; p <= 9; ++p) CHECK(pad(p).pad() == p, "pad %d round trip gives %d", p, pad(p).pad());
    Input chord;
    chord.dx = -1;
    chord.dy = -1;
    CHECK(chord.pad() == 7, "4+8 should act as 7");
    Input g;
    g.guard = true;
    CHECK(guardLevel(g) == Level::Mid && g.pad() == 0, "guard alone: the middle guard, no direction");
    g.dy = -1;
    CHECK(guardLevel(g) == Level::High, "guard + up: high");
    g.dy = 1;
    CHECK(guardLevel(g) == Level::Low, "guard + down: low");
    CHECK(Input{}.pad() == 0, "nothing held");
    CHECK(moveFor(6, 1, false) == Move::FrontKick && moveFor(4, -1, false) == Move::FrontKick, "toward = front kick");
    CHECK(moveFor(4, 1, false) == Move::Roundhouse && moveFor(6, -1, false) == Move::Roundhouse, "away = roundhouse");
    CHECK(moveFor(5, 1, false) == Move::Punch && moveFor(0, 1, false) == Move::Punch, "strike alone = punch");
    CHECK(moveFor(8, 1, false) == Move::Uppercut, "up = uppercut");
    CHECK(moveFor(9, 1, false) == Move::FlyingKick && moveFor(7, 1, false) == Move::Backflip, "jump attacks");
    CHECK(moveFor(3, 1, false) == Move::Sweep && moveFor(1, 1, false) == Move::LowPunch &&
              moveFor(2, 1, false) == Move::LowKick,
          "low attacks");
    CHECK(moveFor(2, 1, true) == Move::DiveKick && moveFor(6, 1, true) == Move::AirKick, "air attacks");
}

void testSkeleton() {
    std::printf("skeleton\n");
    const Skeleton s = solve(pose(PoseId::Stance), 100.f, kFloorY, 1);
    const float feet = std::max(s.ankle[0].y, s.ankle[1].y) + kSole;
    CHECK(std::fabs(feet - kFloorY) < 0.5f, "stance soles at %.1f, floor %.1f", double(feet), double(kFloorY));
    const float height = kFloorY - s.top;
    CHECK(height > 44.f && height < 56.f, "standing height %.1f", double(height));
    const Skeleton m = solve(pose(PoseId::Stance), 100.f, kFloorY, -1);
    CHECK(std::fabs((m.head.x - 100.f) + (s.head.x - 100.f)) < 0.01f, "facing left mirrors");
    const float crouch = kFloorY - solve(pose(PoseId::Crouch), 0.f, kFloorY, 1).top;
    const Reach& rh = moveReach(Move::Roundhouse);
    CHECK(crouch < rh.low, "crouch (%.1f tall) ducks under the roundhouse (from %.1f)", double(crouch), double(rh.low));
    CHECK(moveReach(Move::Sweep).high < 18.f, "sweep stays low (%.1f)", double(moveReach(Move::Sweep).high));
    for (int i = 1; i < kMoveCount; ++i) {
        const Reach& r = moveReach(Move(i));
        std::printf("    %-14s reach %5.1f  height %5.1f..%5.1f\n", moveInfo(Move(i)).name, double(r.ahead),
                    double(r.low), double(r.high));
    }
    const Skeleton lying = solve(pose(PoseId::Lying), 0.f, kFloorY, 1);
    CHECK(kFloorY - lying.top < 16.f, "lying flat (%.1f tall)", double(kFloorY - lying.top));
}

// The guard key, with up or down for the high or low guard.
Input guardIn(Level level) {
    Input in;
    in.guard = true;
    in.dy = level == Level::High ? -1 : (level == Level::Low ? 1 : 0);
    return in;
}

const char* levelName(Level l) { return l == Level::High ? "high" : (l == Level::Low ? "low" : "mid"); }

// Keys held for one tick, written as keypad digits plus S (strike) and G (guard).
Input keys(const char* k) {
    bool key[10] = {};
    Input in;
    for (; *k; ++k) {
        if (*k == 'S') in.strike = true;
        else if (*k == 'G') in.guard = true;
        else if (*k >= '1' && *k <= '9') key[*k - '0'] = true;
    }
    in.dx = int(key[3] || key[6] || key[9]) - int(key[1] || key[4] || key[7]);
    in.dy = int(key[1] || key[2] || key[3]) - int(key[7] || key[8] || key[9]);
    in.center = key[5];
    return in;
}

struct Step {
    const char* keys;
    int ticks;
};

// Plays RED's key steps (BLUE far away and idle), then keeps holding the
// last keys; returns the attack RED is committed to once the chord window
// has passed, or Move::None.
Move chordMove(std::initializer_list<Step> steps) {
    Duel d(90.f);
    Move committed = Move::None;
    const char* last = "";
    auto tick = [&](const char* k) {
        d.run(1, keys(k));
        const Fighter& f = d.f(0);
        if (committed == Move::None && f.act == Act::Attack && f.frame == kChordFrames + 1) committed = f.move;
    };
    for (const Step& st : steps) {
        for (int i = 0; i < st.ticks; ++i) tick(st.keys);
        last = st.keys;
    }
    for (int i = 0; i < 40; ++i) tick(last);
    return committed;
}

// Ticks from RED's first input to the hit landing on BLUE (-1: it never
// does). script[i] is RED's input on tick i; after it RED holds `then`.
int timeToHit(Duel& d, const std::vector<Input>& script, Input then, Input blue = Input{}, int limit = 90) {
    for (int i = 0; i < limit; ++i) {
        d.run(1, i < int(script.size()) ? script[size_t(i)] : then, blue);
        if (d.f(1).health < kMaxHealth) return i;
    }
    return -1;
}

void testHits() {
    std::printf("hits and guards\n");
    {
        Duel d(26.f);
        d.attack(5, Input{});
        CHECK(d.f(1).health < kMaxHealth, "punch at 26 px hits a standing fighter (health %.0f)", double(d.f(1).health));
    }
    {
        Duel d(26.f);
        d.attack(5, pad(5));
        CHECK(d.f(1).health == kMaxHealth, "5 on its own guards mid and blocks a punch (health %.0f)",
              double(d.f(1).health));
    }
    {
        Duel d(26.f);
        d.attack(4, pad(2));
        const auto ev = d.events();
        CHECK(d.f(1).health == kMaxHealth && countEvents(ev, EventType::Block) == 0,
              "crouching ducks a roundhouse (health %.0f, blocks %d)", double(d.f(1).health),
              countEvents(ev, EventType::Block));
    }
    {
        Duel d(30.f);
        d.attack(2, pad(2));
        CHECK(d.f(1).health < kMaxHealth, "crouching alone does not block a low kick (health %.0f)",
              double(d.f(1).health));
    }
    {
        Duel d(28.f);
        d.attack(3, Input{}, 40);
        CHECK(d.f(1).act == Act::Fall || d.f(1).act == Act::Down, "a sweep floors you (act %d)", int(d.f(1).act));
    }

    // Each strike is blocked by exactly one guard: its own level.
    const struct {
        int pad;
        Move move;
    } strikes[] = {{5, Move::Punch},   {6, Move::FrontKick}, {4, Move::Roundhouse}, {8, Move::Uppercut},
                   {1, Move::LowPunch}, {2, Move::LowKick},   {3, Move::Sweep}};
    for (const auto& c : strikes) {
        const char* name = moveInfo(c.move).name;
        float gap = -1.f;
        for (float g = 14.f; g <= 44.f && gap < 0.f; g += 2.f) {
            Duel d(g);
            d.attack(c.pad, Input{});
            if (d.f(1).health < kMaxHealth) gap = g;
        }
        if (c.move == Move::Uppercut && gap < 0.f) continue;  // an anti-air: may pass over a standing head
        CHECK(gap > 0.f, "%s lands on a standing fighter at some distance", name);
        if (gap < 0.f) continue;
        for (int l = 0; l < 3; ++l) {
            Duel d(gap);
            d.attack(c.pad, guardIn(Level(l)));
            const bool blocked = d.f(1).health == kMaxHealth;
            const bool should = Level(l) == moveInfo(c.move).level;
            CHECK(blocked == should, "%s at %.0f px against the %s guard: %s", name, double(gap), levelName(Level(l)),
                  blocked ? "blocked" : "hit");
        }
    }

    // A jump-in air kick lands high: the high guard stops it, the low one does not.
    for (int l = 0; l < 3; ++l) {
        Duel d(58.f);
        const Input g = guardIn(Level(l));
        d.run(4, Input{}, g);
        d.run(1, pad(9), g);
        int t = 0;
        while (d.f(0).act != Act::Air && t++ < 10) d.run(1, pad(9), g);
        bool struck = false;
        for (int i = 0; i < 60 && d.f(0).airborne; ++i) {
            const float ahead = (d.f(1).x - d.f(0).x);
            if (!struck && ahead < 30.f && d.f(0).vy > -120.f) {
                d.run(1, pad(6, true), g);
                struck = true;
            } else {
                d.run(1, Input{}, g);
            }
        }
        const bool blocked = d.f(1).health == kMaxHealth;
        CHECK(struck, "air kick was pressed");
        CHECK(blocked == (Level(l) == Level::High), "jump-in air kick against the %s guard: %s", levelName(Level(l)),
              blocked ? "blocked" : "hit");
    }
    {
        Duel d(40.f);
        d.run(3, pad(3));
        d.run(30, pad(3));
        CHECK(d.f(0).x > d.f(1).x, "rolling forward passes the opponent (%.0f vs %.0f)", double(d.f(0).x),
              double(d.f(1).x));
    }
}

// Keys meant together but pressed a few ticks apart, in either order.
void testChords() {
    std::printf("chords\n");
    auto expect = [](Move got, Move want, const char* what, int gap) {
        CHECK(got == want, "%s, %d ticks apart: got %s, want %s", what, gap, moveInfo(got).name, moveInfo(want).name);
    };
    for (int g = 0; g <= kChordFrames; ++g) {
        auto first = [g](const char* a, const char* b) {
            return g == 0 ? chordMove({{b, 1}}) : chordMove({{a, g}, {b, 1}});
        };
        expect(first("6", "6S"), Move::FrontKick, "6 then strike", g);
        expect(first("S", "6S"), Move::FrontKick, "strike then 6", g);
        expect(first("8", "8S"), Move::Uppercut, "8 then strike", g);
        expect(first("S", "8S"), Move::Uppercut, "strike then 8", g);
        expect(first("9", "9S"), Move::FlyingKick, "9 then strike", g);
        expect(first("S", "9S"), Move::FlyingKick, "strike then 9", g);
        expect(first("3", "3S"), Move::Sweep, "3 then strike", g);
        expect(first("1", "1S"), Move::LowPunch, "1 then strike", g);
        expect(first("S", "2S"), Move::LowKick, "strike then 2", g);
        // Diagonals built from two keys: hold one, then the other with strike.
        expect(g == 0 ? chordMove({{"6", 3}, {"68S", 1}}) : chordMove({{"6", 3}, {"68", g}, {"68S", 1}}),
               Move::FlyingKick, "6, 8 then strike", g);
        expect(g == 0 ? chordMove({{"4", 3}, {"48S", 1}}) : chordMove({{"4", 3}, {"48", g}, {"48S", 1}}),
               Move::Backflip, "4, 8 then strike", g);
        expect(g == 0 ? chordMove({{"2", 3}, {"26S", 1}}) : chordMove({{"2", 3}, {"26", g}, {"26S", 1}}), Move::Sweep,
               "2, 6 then strike", g);
        expect(g == 0 ? chordMove({{"2", 3}, {"24S", 1}}) : chordMove({{"2", 3}, {"24", g}, {"24S", 1}}),
               Move::LowPunch, "2, 4 then strike", g);
    }
    // Letting go of one key of a chord early is not a new direction.
    expect(chordMove({{"26S", 2}, {"6S", 4}, {"", 1}}), Move::Sweep, "2+6+strike, then 2 let go early", 2);
    expect(chordMove({{"8S", 2}, {"S", 4}, {"", 1}}), Move::Uppercut, "8+strike, then 8 let go early", 2);
    {
        // A strike waiting for the air delay is dropped if the fighter lands
        // first, rather than coming out on the ground.
        Duel d(90.f);
        Fighter& f = d.m.fighterForTest(0);
        f.y = kFloorY - 3.f;
        f.airborne = true;
        f.support = -1;
        f.peakY = f.y;
        f.vx = f.vy = 0.f;
        f.act = Act::Air;
        f.frame = 0;
        f.airFrames = 0;
        f.airAttack = false;
        d.m.refresh(f);
        bool attacked = false;
        d.run(1, keys("2S"));
        for (int i = 0; i < 30; ++i) {
            d.run(1, Input{});
            attacked = attacked || d.f(0).act == Act::Attack;
        }
        CHECK(!attacked, "a dive pressed just before a short drop lands does not turn into a low kick");
    }
    // Too far apart: two separate actions.
    expect(chordMove({{"S", kChordFrames + 2}, {"6S", 1}}), Move::Punch, "strike, then 6 much later", kChordFrames + 2);
    expect(chordMove({{"8", kChordFrames + 2}, {"8S", 1}}), Move::AirKick, "8, then strike much later",
           kChordFrames + 2);
    {
        // A strike chorded into a roll starts where the roll began.
        Duel d(90.f);
        const float x0 = d.f(0).x;
        d.run(3, keys("2"));
        d.run(2, keys("26"));
        d.run(1, keys("26S"));
        CHECK(d.f(0).act == Act::Attack && d.f(0).move == Move::Sweep && std::fabs(d.f(0).x - x0) < 0.5f,
              "2, 6, strike sweeps from the spot (act %d, moved %.1f px)", int(d.f(0).act), double(d.f(0).x - x0));
    }
    // The guard key chords the same way.
    for (int g = 0; g <= kChordFrames; ++g) {
        auto guardAfter = [g](const char* a, const char* b, Level want, const char* what) {
            Duel d(90.f);
            const float x0 = d.f(0).x;
            if (g > 0) d.run(g, keys(a));
            d.run(3, keys(b));
            const Fighter& f = d.f(0);
            CHECK(f.act == Act::Guard && f.guard == want && !f.airborne && std::fabs(f.x - x0) < 0.5f,
                  "%s, %d ticks apart: act %d, %s guard, airborne %d, moved %.1f", what, g, int(f.act),
                  levelName(f.guard), int(f.airborne), double(f.x - x0));
        };
        guardAfter("8", "8G", Level::High, "8 then guard");
        guardAfter("G", "8G", Level::High, "guard then 8");
        guardAfter("2", "2G", Level::Low, "2 then guard");
        guardAfter("3", "3G", Level::Low, "3 then guard");
        guardAfter("G", "5G", Level::Mid, "guard then 5");
    }
}

// Every strike that can reach a fighter on the floor takes at least
// kMinStrikeFrames to land, and a guard pressed in that time stops it.
void testStrikeTiming() {
    std::printf("strike timing\n");
    auto atLeast = [](int t, const char* what, float gap) {
        if (t >= 0)
            CHECK(t >= kMinStrikeFrames, "%s at %.0f px lands after %d ticks (min %d)", what, double(gap), t,
                  kMinStrikeFrames);
    };
    const struct {
        int pad;
        Move move;
    } ground[] = {{5, Move::Punch},    {6, Move::FrontKick}, {4, Move::Roundhouse}, {8, Move::Uppercut},
                  {1, Move::LowPunch}, {2, Move::LowKick},   {3, Move::Sweep},      {9, Move::FlyingKick},
                  {7, Move::Backflip}};
    for (const auto& c : ground) {
        int fastest = 1000;
        for (float g = 6.f; g <= 90.f; g += 2.f) {
            Duel d(g);
            d.run(4, Input{});
            const int t = timeToHit(d, {pad(c.pad, true)}, Input{});
            atLeast(t, moveInfo(c.move).name, g);
            if (t < 0) continue;
            fastest = std::min(fastest, t);
            // The right guard, pressed as late as the tick it lands, blocks it.
            Duel b(g);
            b.run(4, Input{});
            for (int i = 0; i < 60; ++i) b.run(1, i == 0 ? pad(c.pad, true) : Input{}, i >= t ? guardIn(moveInfo(c.move).level) : Input{});
            CHECK(b.f(1).health == kMaxHealth, "%s at %.0f px: the %s guard pressed on tick %d blocks it",
                  moveInfo(c.move).name, double(g), levelName(moveInfo(c.move).level), t);
        }
        if (fastest < 1000) std::printf("    %-14s lands %d ticks (%d ms) after the press at the earliest\n",
                                        moveInfo(c.move).name, fastest, fastest * 1000 / 60);
    }
    // Jump-ins: up (or up-forward) to jump, then strike as early as possible.
    for (const char* jumpKeys : {"8", "9"}) {
        for (const char* strikeKeys : {"S", "6S", "2S"}) {
            for (float g = 8.f; g <= 80.f; g += 2.f) {
                Duel d(g);
                d.run(4, Input{});
                std::vector<Input> script(size_t(kChordFrames + 1), keys(jumpKeys));
                const int t = timeToHit(d, script, keys(strikeKeys));
                char what[32];
                std::snprintf(what, sizeof what, "jump %s, then %s", jumpKeys, strikeKeys);
                atLeast(t, what, g);
            }
        }
    }
    // Stepping off something high and diving at once.
    for (float h = 20.f; h <= 60.f; h += 10.f)
        for (float g = 4.f; g <= 40.f; g += 4.f) {
            Duel d(g);
            Fighter& f = d.m.fighterForTest(0);
            f.y = kFloorY - h;
            f.airborne = true;
            f.support = -1;
            f.peakY = f.y;
            f.vx = f.vy = 0.f;
            f.act = Act::Air;
            f.frame = 0;
            f.airFrames = 0;
            f.airAttack = false;
            d.m.refresh(f);
            const int t = timeToHit(d, {}, keys("2S"));
            atLeast(t, "a dive straight off a ledge", g);
        }
}

void testFurniture() {
    std::printf("furniture\n");
    // Kitchen: the table is piece 3.
    {
        Duel d(120.f, 1);
        const Furniture& table = d.m.furniture()[3];
        CHECK(table.kind == Piece::Table, "kitchen piece 3 is the table");
        d.place(0, (table.x0() + table.x1()) * 0.5f);
        d.run(1, pad(8));
        d.run(70, Input{});
        CHECK(d.f(0).support == 3 && std::fabs(d.f(0).y - table.topY()) < 0.01f,
              "jumping in front of the table lands on it (support %d, y %.1f)", d.f(0).support, double(d.f(0).y));
        d.run(60, pad(6));
        CHECK(d.f(0).support == -1 && d.f(0).y == kFloorY, "walking off its edge drops to the floor (support %d)",
              d.f(0).support);
    }
    {
        // A dive from the table hurts more than one from a floor jump.
        auto dive = [](bool fromTable) {
            Duel d(120.f, 1);
            const Furniture& table = d.m.furniture()[3];
            const float x = table.x1() - 6.f;
            if (fromTable) d.place(0, x, table.topY(), 3);
            else d.place(0, x);
            d.place(1, x + 56.f);
            d.run(1, pad(9));
            for (int i = 0; i < 80 && d.f(1).health == kMaxHealth; ++i) {
                const Fighter& a = d.f(0);
                const float ahead = d.f(1).x - a.x;
                const float above = d.f(1).y - a.y;
                const bool go = a.act == Act::Air && a.vy > -40.f && ahead < 15.f + 150.f * std::max(0.f, above - 30.f) / 290.f + 4.f;
                d.run(1, go ? pad(2, true) : Input{});
            }
            return kMaxHealth - d.f(1).health;
        };
        const float high = dive(true), low = dive(false);
        CHECK(high > 0.f && low > 0.f, "both dives connect (%.1f, %.1f)", double(high), double(low));
        CHECK(high > low + 3.f, "the dive from the table hits harder (%.1f vs %.1f)", double(high), double(low));
    }
    {
        // Knocked down onto the stool (1 hp): it breaks and the body ends on the floor.
        Duel d(120.f, 1);
        const Furniture& stool = d.m.furniture()[5];
        CHECK(stool.kind == Piece::Stool, "kitchen piece 5 is the stool");
        Fighter& f = d.m.fighterForTest(1);
        f.x = (stool.x0() + stool.x1()) * 0.5f;
        f.y = stool.topY() - 30.f;
        f.airborne = true;
        f.vx = 0.f;
        f.vy = 0.f;
        f.peakY = f.y;
        f.act = Act::Fall;
        d.run(90, Input{});
        const auto ev = d.events();
        CHECK(d.m.furniture()[5].broken, "the stool broke");
        CHECK(countEvents(ev, EventType::Break) == 1, "one break event (%d)", countEvents(ev, EventType::Break));
        CHECK(d.f(1).y == kFloorY, "the body fell through to the floor (y %.1f)", double(d.f(1).y));
    }
}

struct Stats {
    std::map<std::string, int> hits;
    int blocks = 0, perches = 0, breaks = 0, kos = 0, rounds = 0, timeouts = 0;
    int rollChain = 0;  // most rolls one fighter strung back to back
    long swings = 0, fightTicks = 0, perchTicks = 0, closeTicks = 0, guardTicks = 0, airTicks = 0;
    std::map<std::string, int> tries;
};

// Plays one whole match; returns the winner (-1 draw, -2 stuck).
int playMatch(Control a, Control b, uint64_t seed, Stats& st, float* seconds = nullptr) {
    MatchSetup s;
    s.control = {{a, b}};
    s.rounds = 3;
    s.roundTime = 60;
    s.seed = seed;
    Match m(s);
    std::array<Act, 2> prevAct{{Act::Intro, Act::Intro}};
    std::array<int, 2> rollEnd{{-100, -100}}, chain{{0, 0}};
    for (int t = 0; t < 60 * 60 * 6; ++t) {
        m.tick();
        for (int i = 0; i < 2; ++i) {
            const Act act = m.fighter(i).act;
            if (prevAct[size_t(i)] == Act::Roll && act != Act::Roll) rollEnd[size_t(i)] = t;
            if (act == Act::Roll && prevAct[size_t(i)] != Act::Roll) {
                chain[size_t(i)] = t - rollEnd[size_t(i)] <= 2 ? chain[size_t(i)] + 1 : 1;
                st.rollChain = std::max(st.rollChain, chain[size_t(i)]);
            }
            prevAct[size_t(i)] = act;
        }
        if (m.phase() == Phase::Fight) {
            ++st.fightTicks;
            for (int i = 0; i < 2; ++i) {
                const Fighter& f = m.fighter(i);
                st.perchTicks += f.support >= 0 && !f.airborne;
                st.guardTicks += f.act == Act::Guard || f.act == Act::Crouch || f.act == Act::Block;
                st.airTicks += f.airborne;
            }
            st.closeTicks += std::fabs(m.fighter(0).x - m.fighter(1).x) < 40.f;
        }
        for (const Event& e : m.takeEvents()) {
            switch (e.type) {
                case EventType::Swing: ++st.swings; ++st.tries[moveInfo(e.move).name]; break;
                case EventType::Hit: ++st.hits[moveInfo(e.move).name]; break;
                case EventType::Block: ++st.blocks; break;
                case EventType::Land: st.perches += e.piece >= 0 ? 1 : 0; break;
                case EventType::Break: ++st.breaks; break;
                case EventType::Ko: ++st.kos; break;
                case EventType::TimeUp: ++st.timeouts; break;
                case EventType::RoundOver: ++st.rounds; break;
                default: break;
            }
        }
        if (m.phase() == Phase::MatchOver) {
            if (seconds) *seconds = m.clock();
            return m.matchWinner();
        }
    }
    return -2;
}

Stats statsFor(Control a, Control b, int count, uint64_t seed0) {
    Stats st;
    int stuck = 0;
    float total = 0.f;
    for (int i = 0; i < count; ++i) {
        float sec = 0.f;
        const int w = playMatch(a, b, seed0 + uint64_t(i), st, &sec);
        total += sec;
        stuck += w == -2;
    }
    CHECK(stuck == 0, "%d matches never finished", stuck);
    std::printf("  %s vs %s\n", controlName(a), controlName(b));
    std::printf("    %d matches, %d rounds, %.0f s average, %d KOs, %d time-outs\n", count, st.rounds,
                double(total / float(count)), st.kos, st.timeouts);
    std::printf("    blocks %d, landings on furniture %d, furniture broken %d, longest roll chain %d\n", st.blocks,
                st.perches, st.breaks, st.rollChain);
    // A fresh decision may roll straight back out of a corner, but a roll
    // must not keep repeating itself (it once chained 5-21 times).
    CHECK(st.rollChain <= 2, "a CPU strung %d rolls back to back", st.rollChain);
    const double ft = double(std::max(1L, st.fightTicks));
    std::printf("    swings %ld; per fighter: perched %.0f%%, guarding %.0f%%, airborne %.0f%%; close %.0f%% of the time\n",
                st.swings, 50.0 * double(st.perchTicks) / ft, 50.0 * double(st.guardTicks) / ft,
                50.0 * double(st.airTicks) / ft, 100.0 * double(st.closeTicks) / ft);
    std::printf("    tries:");
    for (const auto& kv : st.tries) std::printf(" %s %d,", kv.first.c_str(), kv.second);
    std::printf("\n    hits:");
    for (const auto& kv : st.hits) std::printf(" %s %d,", kv.first.c_str(), kv.second);
    std::printf("\n");
    return st;
}

void testMatches(int count) {
    std::printf("CPU matches\n");
    statsFor(Control::CpuHard, Control::CpuHard, count, 300u);
    statsFor(Control::CpuEasy, Control::CpuEasy, count, 500u);
    const Stats st = statsFor(Control::CpuHard, Control::CpuNormal, count, 100u);
    CHECK(st.kos > 0, "some rounds end in a knockout");
    CHECK(st.perches > 0, "the CPUs use the furniture");
    CHECK(st.hits.size() >= 6, "the CPUs land a variety of moves (%d kinds)", int(st.hits.size()));

    // Same seed, same match.
    Stats a, b;
    float ta = 0.f, tb = 0.f;
    const int wa = playMatch(Control::CpuHard, Control::CpuHard, 4242u, a, &ta);
    const int wb = playMatch(Control::CpuHard, Control::CpuHard, 4242u, b, &tb);
    CHECK(wa == wb && ta == tb && a.blocks == b.blocks, "matches are deterministic");

    // Harder computers win more.
    int hardWins = 0, easyWins = 0;
    for (int i = 0; i < count; ++i) {
        Stats s2;
        const int w = playMatch(i % 2 ? Control::CpuHard : Control::CpuEasy, i % 2 ? Control::CpuEasy : Control::CpuHard,
                                900u + uint64_t(i), s2);
        const int hard = i % 2 ? 0 : 1;
        if (w == hard) ++hardWins;
        else if (w >= 0) ++easyWins;
    }
    std::printf("    hard vs easy: hard %d, easy %d\n", hardWins, easyWins);
    CHECK(hardWins > easyWins, "hard beats easy more often (%d vs %d)", hardWins, easyWins);
}

// A stand-in for a beginner at the keyboard, playing through the same keys:
// slow to react (about 370 ms), presses simple attacks when close, rarely
// guards and then often at the wrong height, jumps now and then, and pays no
// attention to the furniture.
class Novice {
public:
    explicit Novice(uint64_t seed) : rng_(seed) {}

    Input think(const Match& m, int self) {
        const Fighter& me = m.fighter(self);
        const Snapshot& op = m.seen(1 - self, kReaction);
        const float dx = op.x - me.x, dist = std::fabs(dx);
        const int toward = dx >= 0.f ? 1 : -1;
        // An attack seen coming: sometimes a guard, a coin toss which one.
        if (op.act == Act::Attack && op.move != Move::None) {
            const int started = m.ticks() - kReaction - op.frame;
            if (started != seenAttack_) {
                seenAttack_ = started;
                if (dist < 50.f && rng_.uniform() < 0.3f) {
                    const Level l = rng_.uniform() < 0.5f ? moveInfo(op.move).level : Level(rng_.below(3));
                    set(guardIn(l), rng_.range(15, 30));
                }
            }
        }
        if (hold_ > 0) {
            --hold_;
            return held_;
        }
        held_ = Input{};
        if (me.airborne) {
            if (dist < 34.f && rng_.uniform() < 0.08f) set(pad(5, true), 2);
            return held_;
        }
        if (--wait_ > 0) return held_;
        wait_ = rng_.range(10, 22);
        const float r = rng_.uniform();
        if (dist > 38.f) {
            if (r < 0.06f) set(keys(toward > 0 ? "9" : "7"), 3);
            else set(pad(toward > 0 ? 6 : 4), wait_);
        } else if (r < 0.55f) {
            const float a = rng_.uniform();
            const int fwd = me.facing > 0 ? 6 : 4, back = me.facing > 0 ? 4 : 6;
            const int p = a < 0.3f ? 5 : a < 0.55f ? fwd : a < 0.7f ? 2 : a < 0.85f ? back : (fwd == 6 ? 3 : 1);
            set(pad(p, true), 2);
        } else if (r < 0.7f) {
            set(guardIn(Level(rng_.below(3))), rng_.range(15, 30));
        } else if (r < 0.85f) {
            set(pad(toward > 0 ? 4 : 6), 10);
        }
        return held_;
    }

private:
    static constexpr int kReaction = 22;
    void set(Input in, int ticks) {
        held_ = in;
        hold_ = ticks;
    }
    Rng rng_;
    Input held_;
    int hold_ = 0, wait_ = 0, seenAttack_ = -1;
};

// A whole match of the novice (RED) against a computer: the winner, and the
// novice's health lead summed over the rounds.
int playNovice(Control cpu, uint64_t seed, float& noviceLead) {
    MatchSetup s;
    s.control = {{Control::Human, cpu}};
    s.rounds = 3;
    s.roundTime = 60;
    s.seed = seed;
    Match m(s);
    Novice n(hash64(seed, 99));
    noviceLead = 0.f;
    for (int t = 0; t < 60 * 60 * 6; ++t) {
        m.setInput(0, n.think(m, 0));
        m.tick();
        for (const Event& e : m.takeEvents())
            if (e.type == EventType::RoundOver) noviceLead += m.fighter(0).health - m.fighter(1).health;
        if (m.phase() == Phase::MatchOver) return m.matchWinner();
    }
    return -2;
}

// How each computer level fares against a beginner: Easy should lose to
// one, Hard should beat one, and each level should be harder than the last.
void testLadder(int count) {
    std::printf("against a beginner\n");
    const int n = count * 4;
    int noviceWins[3] = {};
    float lead[3] = {};
    for (int level = 0; level < 3; ++level) {
        const Control cpu = Control(int(Control::CpuEasy) + level);
        int novice = 0, comp = 0, draws = 0;
        float margin = 0.f;
        for (int i = 0; i < n; ++i) {
            float h = 0.f;
            const int w = playNovice(cpu, 7000u + uint64_t(i), h);
            margin += h;
            if (w == 0) ++novice;
            else if (w == 1) ++comp;
            else ++draws;
        }
        noviceWins[level] = novice;
        lead[level] = margin / float(n * 2);
        std::printf("    %-10s beginner wins %2d of %d, computer %2d, draws %d; beginner's health lead per round %+.0f\n",
                    controlName(cpu), novice, n, comp, draws, double(lead[level]));
    }
    CHECK(noviceWins[0] * 10 >= n * 8, "a beginner beats Easy in most matches (%d of %d)", noviceWins[0], n);
    CHECK(noviceWins[2] * 10 <= n * 2, "Hard beats a beginner in most matches (beginner wins %d of %d)", noviceWins[2], n);
    CHECK(lead[0] > lead[1] && lead[1] > lead[2], "each level is harder than the last (leads %+.0f, %+.0f, %+.0f)",
          double(lead[0]), double(lead[1]), double(lead[2]));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 3 && std::string(argv[1]) == "probe") {  // kara_tests probe SEED SECONDS: health after a CPU fight
        MatchSetup s;
        s.control = {{Control::CpuHard, Control::CpuHard}};
        s.seed = std::strtoull(argv[2], nullptr, 10);
        Match m(s);
        const float until = float(std::atof(argv[3]));
        bool viaUpdate = argc > 4;
        while (m.clock() < until) {
            if (viaUpdate) m.update(1.f / 60.f);
            else m.tick();
            m.takeEvents();
        }
        std::printf("seed %s: round %d, health %.0f / %.0f\n", argv[2], m.round(), double(m.fighter(0).health),
                    double(m.fighter(1).health));
        return 0;
    }
    const int matches = argc > 1 ? std::max(1, std::atoi(argv[1])) : 6;
    testInput();
    testSkeleton();
    testHits();
    testChords();
    testStrikeTiming();
    testFurniture();
    testMatches(matches);
    testLadder(matches);
    if (g_failures) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
