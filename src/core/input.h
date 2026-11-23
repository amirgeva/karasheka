#pragma once

namespace kara {

// One fighter's controls for one tick: nine keys laid out like a numeric
// keypad (7 8 9 / 4 5 6 / 1 2 3) plus the strike modifier. Holding two keys
// combines them, so 4 + 8 works like 7.
struct Input {
    int dx = 0;           // -1 left, 0, +1 right (screen directions)
    int dy = 0;           // -1 up, 0, +1 down
    bool center = false;  // the middle key (5) is held
    bool strike = false;  // the strike modifier is held

    // Keypad number of what is held: 1-9, with 5 only when the middle key is
    // held on its own, and 0 when no direction key is held at all.
    int pad() const {
        if (dx == 0 && dy == 0) return center ? 5 : 0;
        return 5 + dx - 3 * dy;
    }

    static Input fromPad(int pad, bool strike) {
        Input in;
        in.strike = strike;
        if (pad < 1 || pad > 9) return in;
        in.dx = (pad - 1) % 3 - 1;
        in.dy = 1 - (pad - 1) / 3;
        in.center = pad == 5;
        return in;
    }

    bool operator==(const Input& o) const {
        return dx == o.dx && dy == o.dy && center == o.center && strike == o.strike;
    }
    bool operator!=(const Input& o) const { return !(*this == o); }
};

}  // namespace kara
