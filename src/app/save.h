#pragma once

namespace kara {

enum class Mode : int { OnePlayer, TwoPlayers, Watch };
constexpr int kModeCount = 3;

constexpr int kLayoutCount = 3;  // QWE/ASD/ZXC, YUI/HJK/NM, numpad
constexpr int kLayoutAny = 3;    // one player: all three layouts at once

constexpr int kRoundChoices = 3;
constexpr int kRoundOptions[kRoundChoices] = {1, 3, 5};
constexpr int kTimeChoices = 4;
constexpr int kTimeOptions[kTimeChoices] = {30, 60, 99, 0};  // 0 = no limit

// Match setup chosen in the menu, kept between sessions in the per-user
// preferences folder that SDL picks for each platform (e.g.
// %APPDATA%\Karasheka\Karasheka on Windows).
struct Settings {
    int mode = int(Mode::OnePlayer);
    int cpu = 1;               // 0 easy, 1 normal, 2 hard
    int layout[2] = {kLayoutAny, 2};
    int roundChoice = 1;       // index into kRoundOptions
    int timeChoice = 1;        // index into kTimeOptions
    int room = -1;             // -1 = a new room every round
    bool muted = false;
};

Settings loadSettings();
void storeSettings(const Settings& s);
void sanitize(Settings& s);

}  // namespace kara
