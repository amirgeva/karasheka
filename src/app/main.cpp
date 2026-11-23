#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "app/game.h"

namespace {

void usage() {
    std::printf(
        "Karasheka - retro karate in the apartment.\n\n"
        "  --size WxH             window (or capture) size (default 1280x720)\n"
        "  --screenshot FILE.bmp  play a CPU fight offscreen, save one frame and exit\n"
        "  --scene NAME           what to capture: menu | intro | fight | ko | over | help |\n"
        "                         pause | cover | poses (default fight)\n"
        "  --time SECONDS         fight time before a fight / cover capture (default 8)\n"
        "  --seed N               match seed (captures default to 12345; --record defaults to\n"
        "                         the best of 120 auditioned seeds)\n"
        "  --record FILE|-        play the showcase clip, writing raw RGB24 frames\n"
        "                         (- = stdout; see tools/make_media.sh)\n"
        "  --fps N                frame rate for --record (default 30)\n");
}

}  // namespace

int main(int argc, char* argv[]) {
    kara::Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const char* next = i + 1 < argc ? argv[i + 1] : nullptr;
        if (a == "--size" && next) {
            int w = 0, h = 0;
            if (std::sscanf(next, "%dx%d", &w, &h) == 2 && w >= 160 && h >= 90) {
                opt.width = w;
                opt.height = h;
            }
            ++i;
        } else if (a == "--screenshot" && next) {
            opt.screenshot = next;
            ++i;
        } else if (a == "--scene" && next) {
            opt.scene = next;
            ++i;
        } else if (a == "--time" && next) {
            opt.time = float(std::atof(next));
            ++i;
        } else if (a == "--seed" && next) {
            opt.seed = std::strtoull(next, nullptr, 10);
            ++i;
        } else if (a == "--record" && next) {
            opt.record = next;
            ++i;
        } else if (a == "--fps" && next) {
            opt.fps = std::atoi(next);
            ++i;
        } else if (a == "--help" || a == "-h") {
            usage();
            return 0;
        }
    }
    kara::Game game;
    return game.run(opt);
}
