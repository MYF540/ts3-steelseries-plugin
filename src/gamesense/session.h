#pragma once

#include <memory>
#include <string>

#include "render/frame.h"

namespace ts3ss {

class HttpClient;

struct SessionConfig {
    // Game and event names may only contain A-Z, 0-9, hyphen and underscore.
    std::string game            = "TS3_OLED";
    std::string gameDisplayName = "TeamSpeak 3";
    std::string developer       = "ts3-steelseries-plugin";

    // Generic "screened" matches any device with a display. Resolution-specific values
    // only exist for older hardware - "screened-128x64" does not exist at all and would
    // fail silently. See docs/gamesense-notes.md.
    std::string deviceType = "screened";

    int deinitializeTimerMs = 15000;
    int maxLines            = 3;
};

// Owns the conversation with SteelSeries GG and the screen-ownership state machine
// from docs/decisions/0006-event-driven-screen-ownership.md:
//
//     RELEASED --show(non-empty)--> OWNED --release()--> RELEASED
//
// Single-threaded: only the worker thread may touch an instance.
class Session {
public:
    explicit Session(SessionConfig config);
    ~Session();

    Session(const Session&)            = delete;
    Session& operator=(const Session&) = delete;

    // Locates GG and opens the HTTP connection. Safe to call repeatedly; a no-op while
    // already connected. False simply means GG is not up yet.
    bool connect();
    void disconnect();
    bool connected() const { return http_ != nullptr; }

    // Registers if needed and pushes the frame. Registration happens lazily here rather
    // than in connect(), so a plugin that never has anything to say never claims the
    // screen at all.
    bool show(const Frame& frame);

    // Keeps GG from deinitializing us. Only meaningful while the screen is owned.
    bool heartbeat();

    // Hands the display back to GG immediately.
    void release();

    bool owned() const { return owned_; }

private:
    bool sendMetadata();
    bool bindAllEvents();
    bool bindEventForIcon(Icon icon);

    SessionConfig               config_;
    std::unique_ptr<HttpClient> http_;
    bool                        owned_ = false;
};

}  // namespace ts3ss
