#pragma once

#include <string>
#include <vector>

namespace ts3ss {

// GameSense event icon ids from doc/api/event-icons.md. Only the ones this project
// has a use for; the full list runs to 43 and is mostly game stats.
//
// The icon occupies the 32 leftmost pixels of the display, text gets the rest.
enum class Icon : int {
    None       = 0,
    Clock      = 15,
    Muted      = 19,
    Talking    = 20,
    Connect    = 21,
    Disconnect = 22,
};

// What one screen update looks like.
//
// Note there is exactly ONE icon per frame, not one per line - GameSense puts
// "icon-id" on the frame object. The composer decides which contributing widget
// gets it (see docs/widgets.md).
struct Frame {
    std::vector<std::string> lines;
    Icon                     icon = Icon::None;

    // An empty frame is not "draw nothing" but "give the screen back to GG".
    // See docs/decisions/0006-event-driven-screen-ownership.md.
    bool empty() const { return lines.empty(); }

    friend bool operator==(const Frame& a, const Frame& b) {
        return a.icon == b.icon && a.lines == b.lines;
    }
    friend bool operator!=(const Frame& a, const Frame& b) { return !(a == b); }
};

}  // namespace ts3ss
