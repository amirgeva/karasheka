#include "app/save.h"

#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/room.h"

namespace kara {
namespace {

std::string settingsPath() {
    char* base = SDL_GetPrefPath("Karasheka", "Karasheka");
    if (!base) return {};
    std::string path = std::string(base) + "settings.ini";
    SDL_free(base);
    return path;
}

}  // namespace

void sanitize(Settings& s) {
    s.mode = std::clamp(s.mode, 0, kModeCount - 1);
    s.cpu = std::clamp(s.cpu, 0, 2);
    s.layout[0] = std::clamp(s.layout[0], 0, kLayoutAny);
    s.layout[1] = std::clamp(s.layout[1], 0, kLayoutCount - 1);
    if (s.mode == int(Mode::TwoPlayers)) {
        // Two players need two different layouts.
        if (s.layout[0] == kLayoutAny) s.layout[0] = 0;
        if (s.layout[0] == s.layout[1]) s.layout[1] = (s.layout[0] + 2) % kLayoutCount;
    }
    s.roundChoice = std::clamp(s.roundChoice, 0, kRoundChoices - 1);
    s.timeChoice = std::clamp(s.timeChoice, 0, kTimeChoices - 1);
    s.room = std::clamp(s.room, -1, kRoomCount - 1);
}

Settings loadSettings() {
    Settings s;
    const std::string path = settingsPath();
    if (path.empty()) return s;
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return s;
    char line[256];
    while (std::fgets(line, sizeof line, f)) {
        char* eq = std::strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const int value = std::atoi(eq + 1);
        if (!std::strcmp(line, "mode")) s.mode = value;
        else if (!std::strcmp(line, "cpu")) s.cpu = value;
        else if (!std::strcmp(line, "keys1")) s.layout[0] = value;
        else if (!std::strcmp(line, "keys2")) s.layout[1] = value;
        else if (!std::strcmp(line, "rounds")) s.roundChoice = value;
        else if (!std::strcmp(line, "time")) s.timeChoice = value;
        else if (!std::strcmp(line, "room")) s.room = value;
        else if (!std::strcmp(line, "muted")) s.muted = value != 0;
    }
    std::fclose(f);
    sanitize(s);
    return s;
}

void storeSettings(const Settings& s) {
    const std::string path = settingsPath();
    if (path.empty()) return;
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) {
        SDL_Log("Could not write %s", path.c_str());
        return;
    }
    std::fprintf(f, "mode=%d\ncpu=%d\nkeys1=%d\nkeys2=%d\nrounds=%d\ntime=%d\nroom=%d\nmuted=%d\n", s.mode, s.cpu,
                 s.layout[0], s.layout[1], s.roundChoice, s.timeChoice, s.room, s.muted ? 1 : 0);
    std::fclose(f);
}

}  // namespace kara
