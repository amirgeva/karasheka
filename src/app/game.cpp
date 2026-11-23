#include "app/game.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "gfx/art.h"

namespace kara {
namespace {

struct Layout {
    SDL_Scancode pad[9];  // keypad order: 1 2 3 4 5 6 7 8 9
    SDL_Scancode strike[2];
};

const Layout kLayouts[kLayoutCount] = {
    {{SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_Q,
      SDL_SCANCODE_W, SDL_SCANCODE_E},
     {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_LCTRL}},
    {{SDL_SCANCODE_N, SDL_SCANCODE_M, SDL_SCANCODE_COMMA, SDL_SCANCODE_H, SDL_SCANCODE_J, SDL_SCANCODE_K,
      SDL_SCANCODE_Y, SDL_SCANCODE_U, SDL_SCANCODE_I},
     {SDL_SCANCODE_SPACE, SDL_SCANCODE_RSHIFT}},
    {{SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_3, SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_5,
      SDL_SCANCODE_KP_6, SDL_SCANCODE_KP_7, SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_9},
     {SDL_SCANCODE_KP_0, SDL_SCANCODE_KP_ENTER}},
};

bool isEnter(SDL_Keycode k) { return k == SDLK_RETURN || k == SDLK_KP_ENTER; }
bool isConfirm(SDL_Keycode k) { return isEnter(k) || k == SDLK_SPACE; }
bool isUp(SDL_Keycode k) { return k == SDLK_UP || k == SDLK_w || k == SDLK_KP_8; }
bool isDown(SDL_Keycode k) { return k == SDLK_DOWN || k == SDLK_s || k == SDLK_KP_2; }
bool isLeft(SDL_Keycode k) { return k == SDLK_LEFT || k == SDLK_a || k == SDLK_KP_4; }
bool isRight(SDL_Keycode k) { return k == SDLK_RIGHT || k == SDLK_d || k == SDLK_KP_6; }

uint64_t freshSeed() {
    return (uint64_t(SDL_GetPerformanceCounter()) * 0x9E3779B97F4A7C15ull) ^ (uint64_t(SDL_GetTicks()) << 32);
}

// Scales the frame to w x h with crisp pixels: each output pixel averages the
// frame pixels its footprint covers, so non-integer scales stay sharp.
// `cover` fills the whole output (cropping); otherwise it letterboxes.
std::vector<uint8_t> scaleRGB(const Image& src, int w, int h, bool cover, bool bgra) {
    std::vector<uint8_t> out(size_t(w) * size_t(h) * (bgra ? 4 : 3), 0);
    const float sx = float(w) / float(src.width()), sy = float(h) / float(src.height());
    const float s = cover ? std::max(sx, sy) : std::min(sx, sy);
    const float ox = (float(w) - float(src.width()) * s) * 0.5f, oy = (float(h) - float(src.height()) * s) * 0.5f;
    auto spans = [&](int X, float o, int n, int& i0, float& w0, int& i1, float& w1) {
        const float a = (float(X) - o) / s, b = (float(X + 1) - o) / s;
        i0 = int(std::floor(a));
        i1 = i0 + 1;
        const float edge = float(i1);
        if (b <= edge) {
            w0 = 1.f;
            w1 = 0.f;
        } else {
            w0 = (edge - a) / (b - a);
            w1 = 1.f - w0;
        }
        if (i0 < 0 || i0 >= n) w0 = 0.f;
        if (i1 < 0 || i1 >= n) w1 = 0.f;
    };
    for (int Y = 0; Y < h; ++Y) {
        int y0, y1;
        float wy0, wy1;
        spans(Y, oy, src.height(), y0, wy0, y1, wy1);
        for (int X = 0; X < w; ++X) {
            int x0, x1;
            float wx0, wx1;
            spans(X, ox, src.width(), x0, wx0, x1, wx1);
            float r = 0.f, g = 0.f, b = 0.f;
            auto add = [&](int x, int y, float wgt) {
                if (wgt <= 0.f) return;
                const uint32_t c = src.get(x, y);
                r += float((c >> 16) & 255u) * wgt;
                g += float((c >> 8) & 255u) * wgt;
                b += float(c & 255u) * wgt;
            };
            add(x0, y0, wx0 * wy0);
            add(x1, y0, wx1 * wy0);
            add(x0, y1, wx0 * wy1);
            add(x1, y1, wx1 * wy1);
            uint8_t* o = &out[(size_t(Y) * size_t(w) + size_t(X)) * (bgra ? 4 : 3)];
            const uint8_t R = uint8_t(std::lround(std::min(255.f, r))), G = uint8_t(std::lround(std::min(255.f, g))),
                          B = uint8_t(std::lround(std::min(255.f, b)));
            if (bgra) {
                o[0] = B;
                o[1] = G;
                o[2] = R;
                o[3] = 255;
            } else {
                o[0] = R;
                o[1] = G;
                o[2] = B;
            }
        }
    }
    return out;
}

bool saveBmp(const Image& img, int w, int h, bool cover, const std::string& path) {
    std::vector<uint8_t> px = scaleRGB(img, w, h, cover, true);
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(px.data(), w, h, 32, w * 4, SDL_PIXELFORMAT_ARGB8888);
    const int saved = surf ? SDL_SaveBMP(surf, path.c_str()) : -1;
    if (surf) SDL_FreeSurface(surf);
    if (saved != 0) std::fprintf(stderr, "could not save %s: %s\n", path.c_str(), SDL_GetError());
    return saved == 0;
}

}  // namespace

Game::~Game() { shutdown(); }

// ---------------------------------------------------------------- lifecycle

int Game::run(const Options& opt) {
    if (!init(opt)) {
        shutdown();
        return 1;
    }
    if (offscreen_) {
        const int rc = opt.record.empty() ? runScreenshot(opt) : runRecord(opt);
        shutdown();
        return rc;
    }
    const double freq = double(SDL_GetPerformanceFrequency());
    Uint64 last = SDL_GetPerformanceCounter();
    while (running_) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) handleEvent(e);
        const Uint64 now = SDL_GetPerformanceCounter();
        const float dt = std::min(0.05f, float(double(now - last) / freq));
        last = now;
        update(dt);
        drawFrame();
        present();
        if (!vsync_) {
            const double spent = double(SDL_GetPerformanceCounter() - now) / freq;
            if (spent < 1.0 / 120.0) SDL_Delay(Uint32((1.0 / 120.0 - spent) * 1000.0));
        }
    }
    persist();
    shutdown();
    return 0;
}

bool Game::init(const Options& opt) {
    offscreen_ = !opt.screenshot.empty() || !opt.record.empty();
    persist_ = !offscreen_;
    for (int i = 0; i < kPieceCount; ++i) {
        pieces_[size_t(i)] = art::pieceSprite(Piece(i));
        debris_[size_t(i)] = art::debrisSprite(Piece(i));
    }
    if (offscreen_) {
        // Everything is drawn in software, so captures need no window at all.
        settings_ = Settings{};
        startDemo(opt.seed ? opt.seed : 12345u);
        return true;
    }

    SDL_SetHint("SDL_WINDOWS_DPI_AWARENESS", "permonitorv2");
    SDL_SetHint("SDL_WINDOWS_DPI_SCALING", "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    sdlReady_ = true;
    window_ = SDL_CreateWindow("Karasheka", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, opt.width, opt.height,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create window: %s", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(window_, kWorldW * 2, kWorldH * 2);
    renderer_ = SDL_CreateRenderer(window_, -1,
                                   SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!renderer_) renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);
    if (!renderer_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create renderer: %s", SDL_GetError());
        return false;
    }
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(renderer_, &info) == 0) vsync_ = (info.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    frameTex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, kWorldW, kWorldH);
    if (!frameTex_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create texture: %s", SDL_GetError());
        return false;
    }
    setWindowIcon();

    settings_ = loadSettings();
    audio_.init();
    audio_.setMuted(settings_.muted);
    startDemo(opt.seed ? opt.seed : freshSeed());
    scene_ = Scene::Menu;
    return true;
}

void Game::shutdown() {
    audio_.shutdown();
    if (scaleTex_) SDL_DestroyTexture(scaleTex_);
    scaleTex_ = nullptr;
    if (frameTex_) SDL_DestroyTexture(frameTex_);
    frameTex_ = nullptr;
    if (renderer_) SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;
    if (window_) SDL_DestroyWindow(window_);
    window_ = nullptr;
    if (sdlReady_) SDL_Quit();
    sdlReady_ = false;
}

void Game::persist() {
    if (persist_) storeSettings(settings_);
}

// Plays a CPU match up to the requested moment and saves one frame as a .bmp.
// Used for README screenshots and as an end-to-end smoke test.
int Game::runScreenshot(const Options& opt) {
    const float step = 1.f / 60.f;
    const uint64_t seed = opt.seed ? opt.seed : 12345u;
    const std::string& s = opt.scene;
    if (s == "poses") return saveBmp(drawPoses(), opt.width, opt.height, false, opt.screenshot) ? 0 : 1;
    if (s == "menu") {
        startDemo(seed);
        for (int i = 0; i < 60 * 9; ++i) update(step);
    } else {
        settings_.mode = int(Mode::Watch);
        settings_.cpu = 2;
        settings_.roundChoice = s == "over" ? 0 : 1;
        startMatch(seed);
        auto done = [&]() {
            const Match& m = *match_;
            if (s == "ko") return m.phase() == Phase::Ko && m.phaseTime() > 0.25f;
            if (s == "over") return m.phase() == Phase::MatchOver && m.phaseTime() > 0.5f;
            if (s == "intro") return m.phase() == Phase::Intro && m.phaseTime() > 0.6f;
            if (s == "cover") return m.phase() == Phase::Fight && m.clock() > opt.time && !bursts_.empty();
            return m.phase() == Phase::Fight && m.clock() > opt.time;
        };
        for (int i = 0; i < 60 * 600 && !done(); ++i) update(step);
        if (s == "pause") paused_ = true;
        if (s == "help") help_ = true;
        if (s == "cover") cover_ = true;
        std::fprintf(stderr, "screenshot: round %d, %.1f s, health %.0f / %.0f\n", match_->round(),
                     double(match_->clock()), double(match_->fighter(0).health), double(match_->fighter(1).health));
    }
    drawFrame();
    return saveBmp(frame_, opt.width, opt.height, cover_, opt.screenshot) ? 0 : 1;
}

// Matches are deterministic, so seeds can be auditioned headlessly: the one
// whose opening packs in the most (a dive kick off the furniture, something
// breaking, a flying kick, a sweep, a knockout) is the one the clip replays.
uint64_t Game::pickShowcase(float seconds, float* length) const {
    uint64_t best = 1;
    float bestScore = -1e9f, bestLength = seconds;
    for (uint64_t seed = 1; seed <= 120; ++seed) {
        MatchSetup setup = setupFromSettings(seed);
        Match m(setup);
        float dive = -1.f, brk = -1.f, fly = -1.f, sweep = -1.f, ko = -1.f, perch = -1.f;
        int hits = 0;
        while (m.clock() < seconds && m.phase() != Phase::RoundOver) {
            m.update(1.f / 30.f);
            for (const Event& e : m.takeEvents()) {
                auto first = [&](float& at) {
                    if (at < 0.f) at = m.clock();
                };
                if (e.type == EventType::Hit) {
                    ++hits;
                    if (e.move == Move::DiveKick) first(dive);
                    if (e.move == Move::FlyingKick) first(fly);
                    if (e.move == Move::Sweep) first(sweep);
                }
                if (e.type == EventType::Break) first(brk);
                if (e.type == EventType::Ko) first(ko);
                if (e.type == EventType::Land && e.piece >= 0) first(perch);
            }
        }
        auto has = [](float at) { return at >= 0.f ? 1.f : 0.f; };
        const float end = ko >= 0.f ? ko + 4.f : seconds;
        const float score = 6.f * has(dive) + 5.f * has(brk) + 2.f * has(fly) + 2.f * has(sweep) + 3.f * has(perch) +
                            4.f * has(ko) + 0.15f * float(std::min(hits, 20)) - 0.05f * end;
        if (score > bestScore) {
            bestScore = score;
            best = seed;
            bestLength = std::min(seconds, end);
        }
    }
    if (length) *length = bestLength;
    std::fprintf(stderr, "showcase: seed %llu, %.1f s\n", (unsigned long long)best, double(bestLength));
    return best;
}

// Plays the showcase clip at a fixed frame rate and writes every frame as raw
// RGB24 (pipe it into ffmpeg; see tools/make_media.sh): a moment on the menu,
// then a CPU fight.
int Game::runRecord(const Options& opt) {
    settings_.mode = int(Mode::Watch);
    settings_.cpu = 2;
    settings_.roundChoice = 1;
    settings_.timeChoice = 1;
    float length = 30.f;
    const uint64_t seed = opt.seed ? opt.seed : pickShowcase(32.f, &length);

    FILE* out = nullptr;
    if (opt.record == "-") {
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        out = stdout;
    } else {
        out = std::fopen(opt.record.c_str(), "wb");
    }
    if (!out) {
        std::fprintf(stderr, "cannot open %s for writing\n", opt.record.c_str());
        return 1;
    }
    const int fps = std::clamp(opt.fps, 10, 60);
    const float dt = 1.f / float(fps);
    int frames = 0;
    bool ok = true;
    auto frame = [&] {
        update(dt);
        drawFrame();
        const std::vector<uint8_t> px = scaleRGB(frame_, opt.width, opt.height, false, false);
        if (std::fwrite(px.data(), 1, px.size(), out) != px.size()) ok = false;
        ++frames;
    };
    startDemo(seed + 1000u);
    scene_ = Scene::Menu;
    for (int i = 0; i < fps * 3; ++i) update(dt);
    for (int i = 0; i < int(2.5f * float(fps)) && ok; ++i) frame();
    startMatch(seed);
    while (ok && match_->clock() < length + kIntroTime) frame();
    std::fflush(out);
    if (out != stdout) std::fclose(out);
    std::fprintf(stderr, "recorded %d frames (%.1f s at %d fps, %dx%d)%s\n", frames, double(frames) / fps, fps,
                 opt.width, opt.height, ok ? "" : " - WRITE ERRORS");
    return ok ? 0 : 1;
}

// ---------------------------------------------------------------- flow

MatchSetup Game::setupFromSettings(uint64_t seed) const {
    MatchSetup s;
    const Control cpu = Control(1 + std::clamp(settings_.cpu, 0, 2));
    switch (Mode(settings_.mode)) {
        case Mode::OnePlayer: s.control = {{Control::Human, cpu}}; break;
        case Mode::TwoPlayers: s.control = {{Control::Human, Control::Human}}; break;
        case Mode::Watch: s.control = {{cpu, cpu}}; break;
    }
    s.rounds = kRoundOptions[std::clamp(settings_.roundChoice, 0, kRoundChoices - 1)];
    s.roundTime = kTimeOptions[std::clamp(settings_.timeChoice, 0, kTimeChoices - 1)];
    s.room = settings_.room;
    s.seed = seed;
    return s;
}

void Game::startMatch(uint64_t seed) {
    match_ = std::make_unique<Match>(setupFromSettings(seed));
    scene_ = Scene::Play;
    paused_ = false;
    clearEffects();
}

void Game::startDemo(uint64_t seed) {
    MatchSetup s;
    s.control = {{Control::CpuHard, Control::CpuNormal}};
    s.rounds = 99;
    s.roundTime = 60;
    s.seed = seed;
    demo_ = std::make_unique<Match>(s);
    demo_->skipIntro();
    demoWait_ = 0.f;
}

void Game::goToMenu() {
    scene_ = Scene::Menu;
    paused_ = false;
    match_.reset();
    clearEffects();
    persist();
}

void Game::toggleMute() {
    settings_.muted = !settings_.muted;
    audio_.setMuted(settings_.muted);
    persist();
}

bool Game::anyHuman() const { return match_ && (!match_->isCpu(0) || !match_->isCpu(1)); }

// ---------------------------------------------------------------- input

Input Game::readLayout(int layout) const {
    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    bool key[10] = {};
    bool strike = false;
    for (int l = 0; l < kLayoutCount; ++l) {
        if (layout != kLayoutAny && layout != l) continue;
        for (int k = 0; k < 9; ++k) key[k + 1] = key[k + 1] || ks[kLayouts[l].pad[k]] != 0;
        strike = strike || ks[kLayouts[l].strike[0]] || ks[kLayouts[l].strike[1]];
    }
    Input in;
    in.dx = int(key[3] || key[6] || key[9]) - int(key[1] || key[4] || key[7]);
    in.dy = int(key[1] || key[2] || key[3]) - int(key[7] || key[8] || key[9]);
    in.center = key[5];
    in.strike = strike;
    return in;
}

void Game::feedInputs() {
    if (!match_ || offscreen_) return;
    const bool two = settings_.mode == int(Mode::TwoPlayers);
    match_->setInput(0, readLayout(two ? settings_.layout[0] : kLayoutAny));
    match_->setInput(1, readLayout(settings_.layout[1]));
}

void Game::handleEvent(const SDL_Event& e) {
    switch (e.type) {
        case SDL_QUIT:
            running_ = false;
            break;
        case SDL_KEYDOWN:
            onKeyDown(e.key.keysym.sym, e.key.repeat != 0);
            break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST && scene_ == Scene::Play && anyHuman() && match_ &&
                match_->phase() != Phase::MatchOver) {
                paused_ = true;
                pauseSel_ = 0;
            }
            break;
        default:
            break;
    }
}

void Game::onKeyDown(SDL_Keycode key, bool repeat) {
    if (repeat && !(isUp(key) || isDown(key) || isLeft(key) || isRight(key))) return;
    if (key == SDLK_F2) {
        toggleMute();
        return;
    }
    if (key == SDLK_F11 || (key == SDLK_RETURN && (SDL_GetModState() & KMOD_ALT))) {
        const bool full = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
        SDL_SetWindowFullscreen(window_, full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
        return;
    }
    if (key == SDLK_F1) {
        help_ = !help_;
        audio_.play(Sfx::Tick);
        return;
    }
    if (help_) {  // the legend swallows other keys until it is closed
        if (key == SDLK_ESCAPE || isEnter(key)) help_ = false;
        return;
    }
    if (scene_ == Scene::Menu) menuKey(key, repeat);
    else playKey(key, repeat);
}

std::vector<Game::Item> Game::menuItems() const {
    std::vector<Item> items = {Item::Start, Item::Mode};
    const Mode mode = Mode(settings_.mode);
    if (mode != Mode::TwoPlayers) items.push_back(Item::Cpu);
    if (mode != Mode::Watch) items.push_back(Item::Keys1);
    if (mode == Mode::TwoPlayers) items.push_back(Item::Keys2);
    items.push_back(Item::Rounds);
    items.push_back(Item::Time);
    items.push_back(Item::Room);
    items.push_back(Item::Quit);
    return items;
}

void Game::changeItem(Item item, int dir) {
    auto cycle = [dir](int v, int n) { return ((v + dir) % n + n) % n; };
    const bool two = settings_.mode == int(Mode::TwoPlayers);
    switch (item) {
        case Item::Mode:
            settings_.mode = cycle(settings_.mode, kModeCount);
            if (settings_.mode == int(Mode::OnePlayer)) settings_.layout[0] = kLayoutAny;
            break;
        case Item::Cpu:
            settings_.cpu = cycle(settings_.cpu, 3);
            break;
        case Item::Keys1:
        case Item::Keys2: {
            const int who = item == Item::Keys1 ? 0 : 1;
            const int n = two ? kLayoutCount : kLayoutCount + 1;  // "any" only for one player
            int& v = settings_.layout[who];
            v = cycle(v, n);
            if (two && v == settings_.layout[1 - who]) v = cycle(v, n);
            break;
        }
        case Item::Rounds:
            settings_.roundChoice = cycle(settings_.roundChoice, kRoundChoices);
            break;
        case Item::Time:
            settings_.timeChoice = cycle(settings_.timeChoice, kTimeChoices);
            break;
        case Item::Room:
            settings_.room = cycle(settings_.room + 1, kRoomCount + 1) - 1;
            break;
        default:
            return;
    }
    sanitize(settings_);
    audio_.play(Sfx::Tick);
    persist();
}

void Game::menuKey(SDL_Keycode key, bool repeat) {
    const std::vector<Item> items = menuItems();
    menuSel_ = std::clamp(menuSel_, 0, int(items.size()) - 1);
    const Item item = items[size_t(menuSel_)];
    if (isUp(key)) {
        menuSel_ = (menuSel_ + int(items.size()) - 1) % int(items.size());
        audio_.play(Sfx::Tick);
    } else if (isDown(key)) {
        menuSel_ = (menuSel_ + 1) % int(items.size());
        audio_.play(Sfx::Tick);
    } else if (isLeft(key)) {
        changeItem(item, -1);
    } else if (isRight(key)) {
        changeItem(item, 1);
    } else if (key == SDLK_ESCAPE && !repeat) {
        running_ = false;
    } else if (isConfirm(key) && !repeat) {
        if (item == Item::Start) startMatch(freshSeed());
        else if (item == Item::Quit) running_ = false;
        else changeItem(item, 1);
    }
}

void Game::playKey(SDL_Keycode key, bool repeat) {
    if (!match_ || repeat) return;
    if (paused_) {
        if (isUp(key) || key == SDLK_UP) {
            pauseSel_ = (pauseSel_ + 2) % 3;
            audio_.play(Sfx::Tick);
        } else if (isDown(key) || key == SDLK_DOWN) {
            pauseSel_ = (pauseSel_ + 1) % 3;
            audio_.play(Sfx::Tick);
        } else if (key == SDLK_ESCAPE) {
            paused_ = false;
        } else if (isEnter(key)) {
            if (pauseSel_ == 0) paused_ = false;
            else if (pauseSel_ == 1) help_ = true;
            else goToMenu();
        }
        return;
    }
    if (match_->phase() == Phase::MatchOver) {
        if (key == SDLK_ESCAPE) goToMenu();
        else if (isEnter(key) && match_->phaseTime() > 0.6f) startMatch(freshSeed());
        return;
    }
    if (key == SDLK_ESCAPE) {
        paused_ = true;
        pauseSel_ = 0;
    }
}

// ---------------------------------------------------------------- simulation

float Game::frand() {
    rngState_ ^= rngState_ << 13;
    rngState_ ^= rngState_ >> 17;
    rngState_ ^= rngState_ << 5;
    return float(rngState_ >> 8) / float(1u << 24);
}

void Game::update(float dt) {
    time_ += dt;
    if (scene_ == Scene::Menu) {
        demo_->update(dt);
        const Phase ph = demo_->phase();
        if (ph == Phase::MatchOver) {
            demoWait_ += dt;
            if (demoWait_ > 2.f) {
                startDemo(offscreen_ ? demo_->setup().seed + 1 : freshSeed());
                clearEffects();
            }
        }
        consumeEvents(*demo_, false);
        updateEffects(dt);
        return;
    }
    if (!match_) return;
    const bool frozen = paused_ || help_;
    if (!frozen) {
        feedInputs();
        match_->update(slowmo_ > 0.f ? dt * 0.35f : dt);
    }
    consumeEvents(*match_, true);
    updateEffects(frozen ? 0.f : dt);
}

void Game::consumeEvents(Match& m, bool sound) {
    auto play = [&](Sfx s, float volume = 1.f, float pitch = 1.f) {
        if (sound) audio_.play(s, volume, pitch);
    };
    for (const Event& e : m.takeEvents()) {
        switch (e.type) {
            case EventType::RoundStart:
                trail_ = {{kMaxHealth, kMaxHealth}};
                particles_.clear();
                bursts_.clear();
                break;
            case EventType::Fight:
                play(Sfx::Bell, 0.8f);
                break;
            case EventType::Swing: {
                const MoveInfo& mi = moveInfo(e.move);
                const bool heavy = mi.knockdown;
                play(heavy ? Sfx::SwishHeavy : Sfx::Swish, 0.55f, frand(0.9f, 1.15f));
                break;
            }
            case EventType::Kiai:
                if (frand() < 0.75f) play(e.fighter == 0 ? Sfx::Kiai0 : Sfx::Kiai1, 0.5f, frand(0.94f, 1.06f));
                break;
            case EventType::Jump:
                play(e.amount < 0.f ? Sfx::Roll : Sfx::Jump, 0.4f, frand(0.9f, 1.1f));
                break;
            case EventType::Land:
                if (e.amount > 6.f) {
                    play(Sfx::Land, std::min(1.f, 0.3f + e.amount / 60.f));
                    if (e.amount > 24.f) dust(e.x, e.y, 3 + int(e.amount / 12.f));
                }
                break;
            case EventType::Hit: {
                const bool heavy = e.heavy || e.amount >= 12.f;
                play(heavy ? Sfx::HeavyHit : Sfx::Hit, 0.9f, frand(0.92f, 1.08f));
                impact(e.x, e.y, heavy ? 9.f : 6.f, opaque(0xFFF2B0), heavy ? 14 : 8);
                shake_ = std::max(shake_, heavy ? 3.f : 1.5f);
                break;
            }
            case EventType::Block:
                play(Sfx::Block, 0.8f, frand(0.95f, 1.08f));
                impact(e.x, e.y, 4.f, opaque(0xB8E0FF), 5);
                break;
            case EventType::Knockdown:
                play(Sfx::Thud, 0.9f);
                dust(e.x, e.y, 12);
                shake_ = std::max(shake_, 2.5f);
                break;
            case EventType::Crack: {
                play(Sfx::Crack, 0.8f, frand(0.9f, 1.1f));
                const Furniture& p = m.furniture()[size_t(e.piece)];
                splinters(e.x, e.y, 8.f, 6, p.kind == Piece::Tv || p.kind == Piece::Monitor ? opaque(0x8AA8B0)
                                                                                           : opaque(0xA06C3C));
                break;
            }
            case EventType::Break: {
                const Furniture& p = m.furniture()[size_t(e.piece)];
                const bool screen = p.kind == Piece::Tv || p.kind == Piece::Monitor;
                play(screen ? Sfx::Glass : Sfx::Crash, 1.f);
                if (screen) play(Sfx::Crack, 0.6f, 0.7f);
                const float w = float(p.spec().w);
                splinters(p.x + w * 0.5f, p.topY() + 4.f, w, 26, screen ? opaque(0x3A3634) : opaque(0xA06C3C));
                if (screen) splinters(p.x + w * 0.5f, p.topY() + 6.f, w * 0.6f, 14, opaque(0xB8F0E8));
                dust(p.x + w * 0.5f, kFloorY, 8);
                shake_ = std::max(shake_, 3.5f);
                break;
            }
            case EventType::Ko:
                play(Sfx::Gong, 1.f);
                if (sound) {
                    slowmo_ = 1.3f;
                    flash_ = 0.18f;
                }
                shake_ = std::max(shake_, 4.f);
                break;
            case EventType::TimeUp:
                play(Sfx::Bell, 0.8f, 0.8f);
                break;
            case EventType::RoundOver:
                if (e.fighter >= 0) play(Sfx::Fanfare, 0.7f);
                break;
            case EventType::MatchOver:
                break;
        }
    }
    for (int i = 0; i < 2; ++i) {
        float& t = trail_[size_t(i)];
        const float h = m.fighter(i).health;
        if (t < h) t = h;
    }
}

void Game::impact(float x, float y, float size, uint32_t color, int sparks) {
    bursts_.push_back(Burst{x, y, 0.f, 0.16f, size, color});
    for (int i = 0; i < sparks; ++i) {
        const float a = frand(0.f, 6.2832f), sp = frand(60.f, 170.f);
        particles_.push_back(Particle{x, y, std::cos(a) * sp, std::sin(a) * sp - 30.f, 0.f, frand(0.12f, 0.3f), 300.f,
                                      i % 3 ? color : opaque(0xFFFFFF), 2});
    }
}

void Game::dust(float x, float y, int count) {
    for (int i = 0; i < count; ++i)
        particles_.push_back(Particle{x + frand(-8.f, 8.f), y - frand(0.f, 3.f), frand(-30.f, 30.f), frand(-18.f, -4.f),
                                      0.f, frand(0.4f, 0.8f), -6.f, argb(206, 196, 180, 170), 1});
}

void Game::splinters(float x, float y, float w, int count, uint32_t color) {
    for (int i = 0; i < count; ++i)
        particles_.push_back(Particle{x + frand(-w, w) * 0.5f, y + frand(-3.f, 3.f), frand(-90.f, 90.f),
                                      frand(-160.f, -40.f), 0.f, frand(0.8f, 1.6f), 520.f,
                                      i % 3 ? color : shade(color, 1.3f), 0});
}

void Game::clearEffects() {
    particles_.clear();
    bursts_.clear();
    trail_ = {{kMaxHealth, kMaxHealth}};
    shake_ = slowmo_ = flash_ = 0.f;
}

void Game::updateEffects(float dt) {
    if (dt <= 0.f) return;
    slowmo_ = std::max(0.f, slowmo_ - dt);
    flash_ = std::max(0.f, flash_ - dt);
    shake_ = std::max(0.f, shake_ - dt * (8.f + shake_ * 4.f));
    for (Particle& p : particles_) {
        p.life += dt;
        p.vy += p.gravity * dt;
        if (p.kind == 1) p.vx *= 1.f - 2.f * dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        if (p.kind == 0 && p.y > kFloorY + 4.f) {  // chips bounce and settle on the floor
            p.y = kFloorY + 4.f;
            p.vy *= -0.35f;
            p.vx *= 0.6f;
        }
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                                    [](const Particle& p) { return p.life >= p.maxLife; }),
                     particles_.end());
    if (particles_.size() > 1500) particles_.erase(particles_.begin(), particles_.begin() + long(particles_.size() - 1500));
    for (Burst& b : bursts_) b.t += dt;
    bursts_.erase(std::remove_if(bursts_.begin(), bursts_.end(), [](const Burst& b) { return b.t >= b.life; }),
                  bursts_.end());
    // The health bars' damage trail catches up after a moment.
    const Match* m = scene_ == Scene::Menu ? demo_.get() : match_.get();
    if (m) {
        for (int i = 0; i < 2; ++i) {
            float& t = trail_[size_t(i)];
            const float h = m->fighter(i).health;
            if (t > h && !m->frozen()) t = std::max(h, t - 40.f * dt);
        }
    }
}

void Game::setWindowIcon() {
    Canvas c(64, 64);
    art::icon(Pen(c), true);
    std::vector<uint8_t> px = c.toRGBA8();
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(px.data(), 64, 64, 32, 64 * 4, SDL_PIXELFORMAT_RGBA32);
    if (s) {
        SDL_SetWindowIcon(window_, s);
        SDL_FreeSurface(s);
    }
}

}  // namespace kara
