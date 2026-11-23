#pragma once

#include <SDL.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "app/audio.h"
#include "app/save.h"
#include "core/fight.h"
#include "gfx/image.h"

namespace kara {

struct Options {
    int width = 1280;
    int height = 720;
    std::string screenshot;       // render one frame to this .bmp and exit
    std::string scene = "fight";  // menu | fight | ko | over | help | pause | poses | cover
    uint64_t seed = 0;            // 0 = random (screenshots default to 12345)
    float time = 8.f;             // seconds of fight before a "fight" screenshot
    std::string record;           // play the showcase clip, raw RGB24 frames to this file ("-" = stdout)
    int fps = 30;                 // frame rate for --record
};

class Game {
public:
    ~Game();
    int run(const Options& opt);

private:
    enum class Scene { Menu, Play };
    enum class Item { Start, Mode, Cpu, Keys1, Keys2, Rounds, Time, Room, Quit };

    struct Particle {
        float x, y, vx, vy, life, maxLife, gravity;
        uint32_t color;
        int kind;  // 0 chip (bounces on the floor), 1 dust puff, 2 spark
    };
    struct Burst {  // the star of an impact
        float x, y, t, life, size;
        uint32_t color;
    };

    // Lifecycle (game.cpp).
    bool init(const Options& opt);
    void shutdown();
    int runScreenshot(const Options& opt);
    int runRecord(const Options& opt);
    uint64_t pickShowcase(float seconds, float* length) const;

    // Flow.
    MatchSetup setupFromSettings(uint64_t seed) const;
    void startMatch(uint64_t seed);
    void startDemo(uint64_t seed);
    void goToMenu();
    void handleEvent(const SDL_Event& e);
    void onKeyDown(SDL_Keycode key, bool repeat);
    void menuKey(SDL_Keycode key, bool repeat);
    void playKey(SDL_Keycode key, bool repeat);
    std::vector<Item> menuItems() const;
    void changeItem(Item item, int dir);
    void toggleMute();
    void persist();
    Input readLayout(int layout) const;
    void feedInputs();
    bool anyHuman() const;

    // Simulation and effects.
    void update(float dt);
    void consumeEvents(Match& m, bool sound);
    void updateEffects(float dt);
    void clearEffects();
    void impact(float x, float y, float size, uint32_t color, int sparks);
    void dust(float x, float y, int count);
    void splinters(float x, float y, float w, int count, uint32_t color);
    float frand();
    float frand(float lo, float hi) { return lo + (hi - lo) * frand(); }

    // Rendering (render.cpp).
    void drawFrame();
    void present();
    const Image& roomImage(int room);
    void drawWorld(const Match& m);
    void drawHud(const Match& m);
    void drawCaption(const Match& m);
    void drawMatchOver(const Match& m);
    void drawMenu();
    void drawPause();
    void drawHelp();
    void drawCover();
    Image drawPoses();
    std::string fighterName(int i) const;
    std::string controlTag(const Match& m, int i) const;
    void setWindowIcon();

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* frameTex_ = nullptr;  // the 320x180 frame, nearest filtering
    SDL_Texture* scaleTex_ = nullptr;  // integer upscale of it, filtered down to the window
    int scaleK_ = 0;
    bool running_ = true;
    bool sdlReady_ = false;
    bool offscreen_ = false;
    bool vsync_ = false;
    bool persist_ = true;
    float time_ = 0.f;
    uint32_t rngState_ = 0x9E3779B9u;

    Audio audio_;
    Settings settings_;
    Scene scene_ = Scene::Menu;
    std::unique_ptr<Match> match_;  // the real match
    std::unique_ptr<Match> demo_;   // CPU-only match playing behind the menu
    float demoWait_ = 0.f;
    bool paused_ = false;
    bool help_ = false;
    int pauseSel_ = 0;
    int menuSel_ = 0;
    bool cover_ = false;
    float coverView_ = 320.f;   // cover: width of the frame that stays in the picture
    float coverFocus_ = 160.f;  // cover: frame column kept in the middle

    Image frame_{kWorldW, kWorldH};
    std::array<Image, kRoomCount> rooms_;
    std::array<Image, kPieceCount> pieces_;
    std::array<Image, kPieceCount> debris_;

    // Effects.
    std::vector<Particle> particles_;
    std::vector<Burst> bursts_;
    std::array<float, 2> trail_{{kMaxHealth, kMaxHealth}};  // health bar damage trail
    float shake_ = 0.f;
    float slowmo_ = 0.f;
    float flash_ = 0.f;
};

}  // namespace kara
