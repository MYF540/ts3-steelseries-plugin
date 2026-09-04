#include "gamesense/session.h"

#include <nlohmann/json.hpp>

#include <array>

#include "gamesense/core_props.h"
#include "gamesense/http.h"
#include "util/log.h"

namespace ts3ss {
namespace {

using nlohmann::json;

// Every icon needs its own bound event.
//
// GameSense puts "icon-id" in the HANDLER, which is fixed at bind time - there is no
// way to change the icon in a game_event. So instead of rebinding on every icon change
// (slow, and it churns GG's state), one event per icon is bound up front and show()
// picks the matching one.
constexpr std::array<Icon, 6> kIcons = {
    Icon::None, Icon::Clock, Icon::Muted, Icon::Talking, Icon::Connect, Icon::Disconnect,
};

const char* eventNameFor(Icon icon) {
    switch (icon) {
        case Icon::None:       return "STATUS_PLAIN";
        case Icon::Clock:      return "STATUS_CLOCK";
        case Icon::Muted:      return "STATUS_MUTED";
        case Icon::Talking:    return "STATUS_TALKING";
        case Icon::Connect:    return "STATUS_CONNECT";
        case Icon::Disconnect: return "STATUS_DISCONNECT";
    }
    return "STATUS_PLAIN";
}

// Frame context keys: l0, l1, l2 ... referenced by the handler, filled by game_event.
std::string lineKey(int index) { return "l" + std::to_string(index); }

}  // namespace

Session::Session(SessionConfig config) : config_(std::move(config)) {}

Session::~Session() {
    // Best effort: hand the screen back rather than leaving our last frame frozen on it.
    if (owned_)
        release();
}

bool Session::connect() {
    if (http_)
        return true;

    // Re-read every time: GG picks a new port on each start, so a cached address goes
    // stale the moment GG restarts.
    const auto address = findGameSenseAddress();
    if (!address)
        return false;

    auto client = std::make_unique<HttpClient>(*address);
    if (!client->valid())
        return false;

    http_  = std::move(client);
    owned_ = false;  // a fresh connection owns nothing yet
    return true;
}

void Session::disconnect() {
    http_.reset();
    owned_ = false;
}

bool Session::sendMetadata() {
    const json body = {
        {"game", config_.game},
        {"game_display_name", config_.gameDisplayName},
        {"developer", config_.developer},
        {"deinitialize_timer_length_ms", config_.deinitializeTimerMs},
    };

    const auto response = http_->post("game_metadata", body.dump());
    if (!response.ok()) {
        TS3SS_ERROR << "game_metadata rejected (status " << response.status << "): "
                    << response.body;
        return false;
    }
    return true;
}

bool Session::bindEventForIcon(Icon icon) {
    json lines = json::array();
    for (int i = 0; i < config_.maxLines; ++i) {
        lines.push_back({
            {"has-text", true},
            {"context-frame-key", lineKey(i)},
            // First line bold so the most important item stands out at a glance.
            {"bold", i == 0},
        });
    }

    json frameData = {{"lines", lines}};
    if (icon != Icon::None)
        frameData["icon-id"] = static_cast<int>(icon);

    const json body = {
        {"game", config_.game},
        {"event", eventNameFor(icon)},
        // Mandatory here: the display is driven purely by frame context, never by a
        // numeric value. Without this GG expects a "value" and caches aggressively.
        {"value_optional", true},
        {"handlers", json::array({json{
                         {"device-type", config_.deviceType},
                         {"zone", "one"},
                         {"mode", "screen"},
                         {"datas", json::array({frameData})},
                     }})},
    };

    const auto response = http_->post("bind_game_event", body.dump());
    if (!response.ok()) {
        TS3SS_ERROR << "bind_game_event " << eventNameFor(icon) << " rejected (status "
                    << response.status << "): " << response.body;
        return false;
    }
    return true;
}

bool Session::bindAllEvents() {
    for (Icon icon : kIcons) {
        if (!bindEventForIcon(icon))
            return false;
    }
    return true;
}

bool Session::show(const Frame& frame) {
    if (!http_ || frame.empty())
        return false;

    if (!owned_) {
        if (!sendMetadata() || !bindAllEvents()) {
            // GG is reachable but refused us. Drop the connection so the worker's
            // backoff applies instead of hammering a server that says no.
            disconnect();
            return false;
        }
        owned_ = true;
        TS3SS_INFO << "Screen claimed";
    }

    // Every key the handler references must be present, otherwise GG keeps whatever
    // stood there before. Unused lines are sent as empty strings on purpose.
    json contextFrame = json::object();
    for (int i = 0; i < config_.maxLines; ++i) {
        const auto index    = static_cast<size_t>(i);
        contextFrame[lineKey(i)] = index < frame.lines.size() ? frame.lines[index] : std::string{};
    }

    const json body = {
        {"game", config_.game},
        {"event", eventNameFor(frame.icon)},
        {"data", {{"frame", contextFrame}}},
    };

    const auto response = http_->post("game_event", body.dump());
    if (!response.ok()) {
        TS3SS_WARN << "game_event rejected (status " << response.status << "): " << response.body;
        return false;
    }
    return true;
}

bool Session::heartbeat() {
    if (!http_ || !owned_)
        return false;

    const json body     = {{"game", config_.game}};
    const auto response = http_->post("game_heartbeat", body.dump());
    if (!response.ok()) {
        TS3SS_WARN << "game_heartbeat failed (status " << response.status << ")";
        // GG most likely deinitialized us. Forget ownership so the next frame
        // re-registers from scratch.
        owned_ = false;
        return false;
    }
    return true;
}

void Session::release() {
    if (!http_ || !owned_)
        return;

    const json body = {{"game", config_.game}};
    http_->post("remove_game", body.dump());

    owned_ = false;
    TS3SS_INFO << "Screen released";
}

}  // namespace ts3ss
