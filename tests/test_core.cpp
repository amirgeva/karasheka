// Headless checks of the skeleton, the moves, guarding, furniture, the
// computer opponents and whole matches.
//
// Usage: kara_tests [matches]   (CPU matches to play per pairing, default 6)

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
        CHECK(d.f(1).health == kMaxHealth, "guard blocks a punch (health %.0f)", double(d.f(1).health));
    }
    {
        Duel d(28.f);
        d.attack(3, pad(5), 40);
        CHECK(d.f(1).health < kMaxHealth, "guard does not stop a sweep (health %.0f)", double(d.f(1).health));
        CHECK(d.f(1).act == Act::Fall || d.f(1).act == Act::Down, "a sweep floors you (act %d)", int(d.f(1).act));
    }
    {
        Duel d(28.f);
        d.attack(3, pad(2));
        CHECK(d.f(1).health == kMaxHealth, "crouching blocks a sweep (health %.0f)", double(d.f(1).health));
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
        Duel d(26.f);
        d.attack(4, Input{});
        CHECK(d.f(1).health < kMaxHealth, "roundhouse hits a standing fighter (health %.0f)", double(d.f(1).health));
    }
    {
        Duel d(26.f);
        d.attack(6, pad(2));
        CHECK(d.f(1).health == kMaxHealth, "crouching blocks a front kick (health %.0f)", double(d.f(1).health));
    }
    {
        Duel d(30.f);
        d.attack(2, Input{});
        CHECK(d.f(1).health < kMaxHealth, "low kick hits (health %.0f)", double(d.f(1).health));
    }
    {
        // A jump-in air kick against a crouching guard: an overhead.
        Duel d(58.f);
        d.run(4, Input{}, pad(2));
        d.run(1, pad(9), pad(2));
        int t = 0;
        while (d.f(0).act != Act::Air && t++ < 10) d.run(1, pad(9), pad(2));
        bool struck = false;
        for (int i = 0; i < 60 && d.f(0).airborne; ++i) {
            const float ahead = (d.f(1).x - d.f(0).x);
            if (!struck && ahead < 28.f && d.f(0).vy > -120.f) {
                d.run(1, pad(6, true), pad(2));
                struck = true;
            } else {
                d.run(1, Input{}, pad(2));
            }
        }
        CHECK(struck, "air kick was pressed");
        CHECK(d.f(1).health < kMaxHealth, "air kick beats a crouching guard (health %.0f)", double(d.f(1).health));
    }
    {
        Duel d(40.f);
        d.run(3, pad(3));
        d.run(30, pad(3));
        CHECK(d.f(0).x > d.f(1).x, "rolling forward passes the opponent (%.0f vs %.0f)", double(d.f(0).x),
              double(d.f(1).x));
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
    for (int t = 0; t < 60 * 60 * 6; ++t) {
        m.tick();
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
    std::printf("    blocks %d, landings on furniture %d, furniture broken %d\n", st.blocks, st.perches, st.breaks);
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
    testFurniture();
    testMatches(matches);
    if (g_failures) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
