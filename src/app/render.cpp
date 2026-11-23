// Composes each 320 x 180 frame in software (world, HUD, menus) and puts it
// on the screen.

#include <algorithm>
#include <cmath>
#include <string>

#include "app/game.h"
#include "gfx/art.h"
#include "gfx/font.h"

#ifndef KARA_VERSION
#define KARA_VERSION "DEV"
#endif

namespace kara {
namespace {

constexpr uint32_t kInk = opaque(0x1E1619);
constexpr uint32_t kGold = opaque(0xFFD66B);
constexpr uint32_t kCream = opaque(0xF4EBDA);
constexpr uint32_t kGrey = opaque(0xA89C8E);

const char* const kModeNames[kModeCount] = {"1 PLAYER", "2 PLAYERS", "WATCH CPU"};
const char* const kCpuNames[3] = {"EASY", "NORMAL", "HARD"};
const char* const kLayoutNames[kLayoutCount + 1] = {"QWE/ASD/ZXC  LSHIFT LCTRL", "YUI/HJK/NM,  SPACE L",
                                                    "NUMPAD  RSHIFT RCTRL", "ANY OF THE THREE"};

Skeleton offset(Skeleton s, float dx, float dy) {
    Vec* all[] = {&s.hip, &s.shoulder, &s.neck, &s.head, &s.elbow[0], &s.elbow[1], &s.hand[0], &s.hand[1],
                  &s.knee[0], &s.knee[1], &s.ankle[0], &s.ankle[1], &s.toe[0], &s.toe[1]};
    for (Vec* v : all) {
        v->x += dx;
        v->y += dy;
    }
    s.top += dy;
    return s;
}

// A dark rounded panel with a light rim.
void panel(Image& img, int x, int y, int w, int h) {
    img.fill(x + 1, y, w - 2, h, argb(16, 10, 12, 215));
    img.fill(x, y + 1, 1, h - 2, argb(16, 10, 12, 215));
    img.fill(x + w - 1, y + 1, 1, h - 2, argb(16, 10, 12, 215));
    img.fill(x + 1, y, w - 2, 1, opaque(0x8A6A48));
    img.fill(x + 1, y + h - 1, w - 2, 1, opaque(0x5A4430));
}

std::string roundsText(int choice) {
    const int n = kRoundOptions[std::clamp(choice, 0, kRoundChoices - 1)];
    return n == 1 ? std::string("1 ROUND") : "BEST OF " + std::to_string(n);
}

std::string timeText(int choice) {
    const int t = kTimeOptions[std::clamp(choice, 0, kTimeChoices - 1)];
    return t > 0 ? std::to_string(t) + " SECONDS" : std::string("NO LIMIT");
}

}  // namespace

std::string Game::fighterName(int i) const { return i == 0 ? "RED" : "GOLD"; }

std::string Game::controlTag(const Match& m, int i) const {
    if (m.isCpu(i)) return std::string("CPU ") + kCpuNames[std::clamp(int(m.control(i)) - 1, 0, 2)];
    if (!m.isCpu(1 - i)) return i == 0 ? "P1" : "P2";
    return "YOU";
}

const Image& Game::roomImage(int room) {
    Image& img = rooms_[size_t(room)];
    if (img.empty()) {
        img = Image(kWorldW, kWorldH);
        art::paintRoom(img, room);
    }
    return img;
}

void Game::drawFrame() {
    frame_.clear(opaque(0x000000));
    const Match* m = scene_ == Scene::Menu ? demo_.get() : match_.get();
    if (m) drawWorld(*m);
    if (cover_) {
        drawCover();
        return;
    }
    if (scene_ == Scene::Menu) {
        drawMenu();
    } else if (match_) {
        drawHud(*match_);
        drawCaption(*match_);
        if (match_->phase() == Phase::MatchOver) drawMatchOver(*match_);
        if (paused_) drawPause();
    }
    if (help_) drawHelp();
    if (flash_ > 0.f) frame_.fill(0, 0, kWorldW, kWorldH, argb(255, 255, 255, int(flash_ / 0.18f * 200.f)));
}

void Game::present() {
    if (!renderer_ || !frameTex_) return;
    SDL_UpdateTexture(frameTex_, nullptr, frame_.data(), kWorldW * 4);
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(renderer_, &w, &h);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    const float s = std::min(float(w) / float(kWorldW), float(h) / float(kWorldH));
    const SDL_FRect dst{std::floor((float(w) - float(kWorldW) * s) * 0.5f),
                        std::floor((float(h) - float(kWorldH) * s) * 0.5f), float(kWorldW) * s, float(kWorldH) * s};
    const bool whole = std::fabs(s - std::round(s)) < 0.001f;
    if (!whole) {
        // Sharp scaling: blow the frame up by a whole number with no
        // filtering, then filter that down to the window.
        const int k = std::max(1, int(std::ceil(s)));
        if (k != scaleK_ || !scaleTex_) {
            if (scaleTex_) SDL_DestroyTexture(scaleTex_);
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
            scaleTex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, kWorldW * k,
                                          kWorldH * k);
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
            scaleK_ = k;
        }
        if (scaleTex_ && SDL_SetRenderTarget(renderer_, scaleTex_) == 0) {
            SDL_RenderCopy(renderer_, frameTex_, nullptr, nullptr);
            SDL_SetRenderTarget(renderer_, nullptr);
            SDL_RenderCopyF(renderer_, scaleTex_, nullptr, &dst);
            SDL_RenderPresent(renderer_);
            return;
        }
    }
    SDL_RenderCopyF(renderer_, frameTex_, nullptr, &dst);
    SDL_RenderPresent(renderer_);
}

// ---------------------------------------------------------------- world

void Game::drawWorld(const Match& m) {
    Image& img = frame_;
    int ox = 0, oy = 0;
    if (shake_ > 0.3f) {
        ox = int(std::lround(frand(-1.f, 1.f) * shake_));
        oy = int(std::lround(frand(-1.f, 1.f) * shake_ * 0.6f));
    }
    img.blit(roomImage(m.room()), ox, oy);

    const auto& pieces = m.furniture();
    for (const Furniture& p : pieces) {
        if (!p.broken) continue;
        const Image& d = debris_[size_t(p.kind)];
        img.blit(d, int(std::lround(p.x + float(p.spec().w - (d.width() - 2)) * 0.5f)) - 1 + ox,
                 int(kFloorY) - d.height() + 2 + oy, p.flip);
    }
    for (const Furniture& p : pieces) {
        if (p.broken) continue;
        const Image& s = pieces_[size_t(p.kind)];
        const int wob = p.wobble > 0.05f ? int(std::lround(std::sin(time_ * 60.f) * p.wobble * 1.4f)) : 0;
        img.blit(s, int(std::lround(p.x)) - 1 + wob + ox, int(std::lround(p.baseY())) - s.height() + 1 + oy, p.flip);
    }

    // Shadows on whatever surface is below each fighter.
    for (int i = 0; i < 2; ++i) {
        const Fighter& f = m.fighter(i);
        float surface = kFloorY;
        for (const Furniture& p : pieces)
            if (!p.broken && f.x >= p.x0() && f.x <= p.x1() && p.topY() >= f.y - 1.f) surface = std::min(surface, p.topY());
        const float height = surface - f.y;
        const float rx = std::max(4.f, 10.f - height * 0.08f);
        img.ellipse(f.x + float(ox), surface + float(oy), rx, 2.f, argb(20, 10, 20, 90));
    }

    // The attacker is drawn last, so its strike shows.
    int order[2] = {0, 1};
    if (m.fighter(0).act == Act::Attack && m.fighter(1).act != Act::Attack) std::swap(order[0], order[1]);
    for (int i : order) {
        const Fighter& f = m.fighter(i);
        const Skeleton s = offset(f.skel, float(ox), float(oy));
        art::drawFighter(img, s, i, f.flash > 4 && !cover_, m.clock() + float(i));
    }

    for (const Burst& b : bursts_) {
        const float k = b.t / b.life;
        const float r = b.size * (0.6f + 0.6f * k);
        const int a = int(255.f * (1.f - k));
        const int cx = int(std::lround(b.x)) + ox, cy = int(std::lround(b.y)) + oy;
        for (int ray = 0; ray < 8; ++ray) {
            const float ang = float(ray) * 0.7854f + 0.2f;
            const float len = ray % 2 ? r * 0.6f : r;
            img.line(cx, cy, cx + int(std::cos(ang) * len), cy + int(std::sin(ang) * len), withAlpha(b.color, a));
        }
        img.disc(float(cx) + 0.5f, float(cy) + 0.5f, r * 0.35f, withAlpha(opaque(0xFFFFFF), a));
    }
    for (const Particle& p : particles_) {
        const float k = p.life / p.maxLife;
        const int x = int(std::floor(p.x)) + ox, y = int(std::floor(p.y)) + oy;
        if (p.kind == 1) {
            const float r = 1.5f + 3.f * k;
            img.disc(p.x + float(ox), p.y + float(oy), r, withAlpha(p.color, int(float(alphaOf(p.color)) * (1.f - k))));
        } else if (p.kind == 2) {
            img.blend(x, y, withAlpha(p.color, int(255.f * (1.f - k))));
            img.blend(x - int(p.vx * 0.012f), y - int(p.vy * 0.012f), withAlpha(p.color, int(140.f * (1.f - k))));
        } else {
            img.fill(x, y, 2, 1, withAlpha(p.color, int(255.f * std::min(1.f, (1.f - k) * 3.f))));
        }
    }
}

// ---------------------------------------------------------------- HUD

void Game::drawHud(const Match& m) {
    Image& img = frame_;
    const int barW = 128, barY = 5, barH = 7;
    for (int i = 0; i < 2; ++i) {
        const Fighter& f = m.fighter(i);
        const int x = i == 0 ? 10 : kWorldW - 10 - barW;
        img.fill(x - 1, barY - 1, barW + 2, barH + 2, kInk);
        img.fill(x, barY, barW, barH, opaque(0x3A1418));
        const float frac = std::clamp(f.health / kMaxHealth, 0.f, 1.f);
        const float trail = std::clamp(trail_[size_t(i)] / kMaxHealth, 0.f, 1.f);
        const int hw = int(std::lround(float(barW) * frac)), tw = int(std::lround(float(barW) * trail));
        // Each bar is anchored at its outer end, so damage eats it from the middle.
        const int hx = i == 0 ? x : x + barW - hw, tx = i == 0 ? x : x + barW - tw;
        img.fill(tx, barY, tw, barH, opaque(0xD9302E));
        uint32_t c = frac > 0.3f ? opaque(0xF2D640) : ((int(time_ * 6.f) & 1) ? opaque(0xF28C30) : opaque(0xF2D640));
        img.fill(hx, barY, hw, barH, c);
        img.fill(hx, barY, hw, 2, shade(c, 1.15f));
        img.fill(hx, barY + barH - 1, hw, 1, shade(c, 0.75f));
        // Name, who controls it, and round wins.
        const uint32_t pc = art::playerColor(i);
        const std::string tag = controlTag(m, i);
        if (i == 0) {
            const int w = img.textOutlined(fighterName(i), x, 15, pc, kInk);
            img.textOutlined(tag, x + w + 6, 15, kGrey, kInk);
        } else {
            const int w = img.textOutlined(fighterName(i), x + barW, 15, pc, kInk, 1, 1);
            img.textOutlined(tag, x + barW - w - 6, 15, kGrey, kInk, 1, 1);
        }
        for (int k = 0; k < m.roundsToWin(); ++k) {
            const int px = i == 0 ? x + barW - 5 - k * 7 : x + 1 + k * 7;
            img.fill(px - 1, 15, 6, 6, kInk);
            img.fill(px, 16, 4, 4, k < m.wins(i) ? opaque(0xFFD66B) : opaque(0x4A3A30));
        }
    }
    // The clock.
    std::string clock = "--";
    uint32_t cc = kCream;
    if (m.timed()) {
        const int t = int(std::ceil(m.timeLeft()));
        clock = (t < 10 ? "0" : "") + std::to_string(t);
        if (t <= 10 && m.phase() == Phase::Fight && (int(time_ * 4.f) & 1)) cc = opaque(0xFF6A5A);
    }
    img.textOutlined(clock, kWorldW / 2, 3, cc, kInk, 2, 0);
    if (settings_.muted) img.textOutlined("SOUND OFF", kWorldW / 2, 172, kGrey, kInk, 1, 0);
}

void Game::drawCaption(const Match& m) {
    Image& img = frame_;
    const float t = m.phaseTime();
    auto big = [&](const std::string& s, int scale, uint32_t c, int y) {
        img.textOutlined(s, kWorldW / 2, y, c, kInk, scale, 0);
    };
    switch (m.phase()) {
        case Phase::Intro:
            if (t < kFightAt) {
                big("ROUND " + std::to_string(m.round()), 3, kCream, 56);
                img.textOutlined(roomName(m.room()), kWorldW / 2, 82, kGold, kInk, 1, 0);
            } else {
                big("FIGHT!", 4, (int(t * 10.f) & 1) ? opaque(0xFFD66B) : opaque(0xFF7A3A), 52);
            }
            break;
        case Phase::Fight:
            if (t < 0.4f) big("FIGHT!", 4, opaque(0xFFD66B), 52);  // lingers a moment
            break;
        case Phase::Ko:
            if (t > 0.1f) big("K.O.", 5, (int(t * 8.f) & 1) && t < 1.2f ? opaque(0xFFFFFF) : opaque(0xE8382E), 46);
            break;
        case Phase::TimeUp:
            big("TIME", 4, kCream, 52);
            break;
        case Phase::RoundOver: {
            const int w = m.roundWinner();
            if (w >= 0) {
                big(fighterName(w) + " WINS", 3, art::playerColor(w), 50);
                if (m.fighter(w).health >= kMaxHealth) big("PERFECT", 2, kGold, 78);
            } else {
                big("DRAW", 3, kCream, 50);
            }
            break;
        }
        case Phase::MatchOver:
            break;
    }
}

void Game::drawMatchOver(const Match& m) {
    Image& img = frame_;
    const float appear = std::min(1.f, m.phaseTime() / 0.3f);
    img.fill(0, 0, kWorldW, kWorldH, argb(0, 0, 0, int(110.f * appear)));
    panel(img, 50, 26, 220, 76);
    const int w = m.matchWinner();
    if (w >= 0) {
        img.textOutlined(fighterName(w) + " WINS", kWorldW / 2, 34, art::playerColor(w), kInk, 2, 0);
        img.textOutlined("THE MATCH", kWorldW / 2, 52, kCream, kInk, 1, 0);
    } else {
        img.textOutlined("DRAW", kWorldW / 2, 34, kCream, kInk, 2, 0);
        img.textOutlined("NOBODY TAKES IT", kWorldW / 2, 52, kCream, kInk, 1, 0);
    }
    img.textOutlined(std::to_string(m.wins(0)) + " - " + std::to_string(m.wins(1)), kWorldW / 2, 66, kGold, kInk, 1, 0);
    if (m.phaseTime() > 0.6f && (int(time_ * 2.5f) & 1) == 0)
        img.textOutlined("ENTER REMATCH   ESC MENU", kWorldW / 2, 86, kCream, kInk, 1, 0);
}

// ---------------------------------------------------------------- menus

void Game::drawMenu() {
    Image& img = frame_;
    img.fill(0, 0, kWorldW, kWorldH, argb(8, 6, 14, 120));
    art::drawLogo(img, kWorldW / 2, 8, 3);
    img.textOutlined("KARATE IN THE APARTMENT", kWorldW / 2, 35, kCream, kInk, 1, 0);

    const std::vector<Item> items = menuItems();
    menuSel_ = std::clamp(menuSel_, 0, int(items.size()) - 1);
    const int rowH = 11;
    int y = 52;
    const int boxX = 24, boxW = kWorldW - 48;
    panel(img, boxX, y - 5, boxW, rowH * int(items.size()) + 7);
    const int split = 118;
    const bool two = settings_.mode == int(Mode::TwoPlayers);
    for (size_t i = 0; i < items.size(); ++i, y += rowH) {
        const Item item = items[i];
        const bool sel = int(i) == menuSel_;
        if (sel) img.fill(boxX + 2, y - 2, boxW - 4, rowH - 1, argb(255, 214, 140, 46));
        const uint32_t c = sel ? kGold : kCream;
        std::string label, value;
        uint32_t lc = c;
        switch (item) {
            case Item::Start:
                img.text(sel ? "> FIGHT! <" : "FIGHT!", kWorldW / 2, y, c, 1, 0);
                continue;
            case Item::Quit:
                img.text(sel ? "> QUIT <" : "QUIT", kWorldW / 2, y, c, 1, 0);
                continue;
            case Item::Mode:
                label = "MODE";
                value = kModeNames[settings_.mode];
                break;
            case Item::Cpu:
                label = "CPU";
                value = kCpuNames[settings_.cpu];
                break;
            case Item::Keys1:
                label = two ? "RED KEYS" : "KEYS";
                lc = two ? art::playerColor(0) : c;
                value = kLayoutNames[std::clamp(settings_.layout[0], 0, kLayoutAny)];
                break;
            case Item::Keys2:
                label = "GOLD KEYS";
                lc = art::playerColor(1);
                value = kLayoutNames[std::clamp(settings_.layout[1], 0, kLayoutCount - 1)];
                break;
            case Item::Rounds:
                label = "ROUNDS";
                value = roundsText(settings_.roundChoice);
                break;
            case Item::Time:
                label = "TIME";
                value = timeText(settings_.timeChoice);
                break;
            case Item::Room:
                label = "ROOM";
                value = settings_.room < 0 ? std::string("TOUR THE FLAT") : std::string(roomName(settings_.room));
                break;
        }
        img.text(label, split - 6, y, lc, 1, 1);
        img.text(sel ? std::string(KARA_ARROW_LEFT " ") + value + " " KARA_ARROW_RIGHT : value, split + 6, y, c);
    }
    img.textOutlined(KARA_ARROW_UP KARA_ARROW_DOWN " SELECT  " KARA_ARROW_LEFT KARA_ARROW_RIGHT " CHANGE  ENTER  F1 KEYS",
                     kWorldW / 2, 166, kGrey, kInk, 1, 0);
    img.text("V" KARA_VERSION, 3, 3, argb(200, 190, 175, 160));
    if (settings_.muted) img.text("SOUND OFF", kWorldW - 3, 3, argb(200, 190, 175, 160), 1, 1);
}

void Game::drawPause() {
    Image& img = frame_;
    img.fill(0, 0, kWorldW, kWorldH, argb(0, 0, 0, 120));
    panel(img, 90, 52, 140, 70);
    img.textOutlined("PAUSED", kWorldW / 2, 58, kGold, kInk, 2, 0);
    const char* items[3] = {"RESUME", "HOW TO FIGHT", "QUIT TO MENU"};
    for (int i = 0; i < 3; ++i) {
        const bool sel = i == pauseSel_;
        const std::string label = sel ? std::string("> ") + items[i] + " <" : items[i];
        img.text(label, kWorldW / 2, 80 + 12 * i, sel ? kGold : kCream, 1, 0);
    }
}

// F1: the keys, then the nine directions on their own and with strike.
void Game::drawHelp() {
    Image& img = frame_;
    img.fill(0, 0, kWorldW, kWorldH, opaque(0x0A070C));
    img.textOutlined("HOW TO FIGHT", kWorldW / 2, 4, kGold, kInk, 1, 0);
    const int kx[3] = {8, 104, 196};
    img.text("DIRECTIONS", kx[0], 15, kGrey);
    img.text("STRIKE", kx[1], 15, kGold);
    img.text("GUARD", kx[2], 15, kGold);
    static const char* const kKeys[3][3] = {{"QWE/ASD/ZXC", "LEFT SHIFT", "LEFT CTRL"},
                                            {"YUI/HJK/NM,", "SPACE", "L"},
                                            {"NUMPAD", "RIGHT SHIFT/0", "RIGHT CTRL/ENTER"}};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) img.text(kKeys[r][c], kx[c], 24 + 9 * r, kCream);
    img.text("ONE PLAYER: MIX THEM, E.G. NUMPAD + LEFT SHIFT/CTRL", 8, 52, kGrey);

    static const char* const kMove[9] = {"ROLL BACK", "CROUCH", "ROLL FWD", "WALK BACK", "MID GUARD",
                                         "WALK FWD",  "JUMP BACK", "JUMP UP", "JUMP FWD"};
    static const char* const kStrike[9] = {"LOW PUNCH",  "LOW KICK", "SWEEP",    "ROUNDHOUSE", "PUNCH",
                                           "FRONT KICK", "BACKFLIP", "UPPERCUT", "FLYING KICK"};
    auto grid = [&](const char* title, const char* const* names, int y0) {
        img.text(title, 8, y0 + 9, kGold);
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col) {
                const int pad = (2 - row) * 3 + col + 1;  // 7 8 9 on top
                const int x = 72 + col * 82, y = y0 + row * 10;
                img.fill(x - 2, y - 2, 80, 10, (row + col) % 2 ? argb(255, 255, 255, 18) : argb(255, 255, 255, 34));
                img.text(names[pad - 1], x, y, kCream);
            }
    };
    grid("MOVE", kMove, 63);
    grid("+ STRIKE", kStrike, 96);
    const int ty = 128;
    img.text("+ GUARD", 8, ty, kGold);
    img.text("UP: HIGH    ALONE OR 5: MID    DOWN: LOW", 62, ty, kCream);
    img.text("HIGH: ROUNDHOUSE, UPPERCUT, ANY KICK FROM THE AIR", 8, ty + 10, kGrey);
    img.text("MID: PUNCHES, FRONT KICK    LOW: LOW KICK, SWEEP", 8, ty + 19, kGrey);
    img.text("AIR: STRIKE = AIR KICK, DOWN + STRIKE = DIVE KICK", 8, ty + 28, kGrey);
    img.textOutlined("ESC PAUSE  F2 SOUND  F11 FULLSCREEN  F1 CLOSE", kWorldW / 2, 171, kGold, kInk, 1, 0);
}

void Game::drawCover() {
    Image& img = frame_;
    for (int y = 0; y < 80; ++y) img.fill(0, y, kWorldW, 1, argb(10, 6, 8, int(170.f * (1.f - float(y) / 80.f))));
    // The logo spans about 80% of what stays in the picture.
    const int cell = std::clamp(int(std::lround(coverView_ * 0.8f / 62.f)), 2, 4);
    const int cx = int(std::lround(coverFocus_));
    art::drawLogo(img, cx, 17, cell);
    img.textOutlined("KARATE IN THE APARTMENT", cx, 17 + 7 * cell + 8, kCream, kInk, 1, 0);
}

// A sheet of every pose, for tuning the skeleton (--scene poses).
Image Game::drawPoses() {
    const int cols = 8, cellW = 80, cellH = 84;
    const int rows = (kPoseCount + cols - 1) / cols;
    Image img(cols * cellW, rows * cellH, opaque(0x6E6A78));
    for (int i = 0; i < kPoseCount; ++i) {
        const int cx = (i % cols) * cellW, cy = (i / cols) * cellH;
        img.fill(cx, cy, cellW - 1, cellH - 1, opaque(0x8C8898));
        const float floorY = float(cy + cellH - 14);
        img.fill(cx, int(floorY), cellW - 1, 1, opaque(0x4A4658));
        const Skeleton s = solve(pose(PoseId(i)), float(cx + cellW / 2 - 6), floorY, 1);
        art::drawFighter(img, s, i % 2, false, 0.f);
        img.text(poseName(PoseId(i)), cx + 2, cy + cellH - 10, opaque(0xFFFFFF));
    }
    return img;
}

}  // namespace kara
