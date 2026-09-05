#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "core/client_state.h"
#include "render/composer.h"
#include "widgets/registry.h"

using namespace ts3ss;

namespace {

Timestamp t0() { return Timestamp{} + std::chrono::hours(1); }

ClientState connectedState() {
    ClientState state;
    state.connected   = true;
    state.serverName  = "Server";
    state.channelName = "Lobby";
    state.channelClientCount = 3;
    // Placed far enough in the past that no "just changed" window is still open.
    state.connectionChangedAt = t0() - std::chrono::minutes(5);
    state.channelChangedAt    = t0() - std::chrono::minutes(5);
    state.selfFlagsChangedAt  = t0() - std::chrono::minutes(5);
    return state;
}

}  // namespace

TEST_CASE("quiet connected state releases the screen") {
    Composer composer;
    CHECK(composer.compose(connectedState(), t0()).empty());
}

// The bug that produced ADR 0007: being muted is a state, not an event. Reporting it
// permanently held the display for as long as the user stayed muted.
TEST_CASE("being muted does not hold the screen") {
    auto state       = connectedState();
    state.inputMuted = true;

    Composer composer;
    CHECK(composer.compose(state, t0()).empty());
}

TEST_CASE("toggling mute claims the screen briefly") {
    auto state               = connectedState();
    state.inputMuted         = true;
    state.selfFlagsChangedAt = t0();

    Composer     composer;
    const Frame just = composer.compose(state, t0());
    REQUIRE_FALSE(just.empty());
    CHECK(just.icon == Icon::Muted);
    CHECK(just.lines.front() == "Mikro aus");

    // ... and lets go again once the window has passed.
    CHECK(composer.compose(state, t0() + std::chrono::seconds(30)).empty());
}

TEST_CASE("deaf is reported instead of, not alongside, mic mute") {
    auto state               = connectedState();
    state.inputMuted         = true;
    state.outputMuted        = true;
    state.selfFlagsChangedAt = t0();

    const Frame frame = Composer{}.compose(state, t0());
    REQUIRE_FALSE(frame.empty());
    CHECK(frame.lines.front() == "Ton aus");
}

TEST_CASE("a talker claims the screen") {
    auto state = connectedState();
    state.talkers.push_back({"Anna", false, t0()});

    const Frame frame = Composer{}.compose(state, t0());
    REQUIRE_FALSE(frame.empty());
    CHECK(frame.icon == Icon::Talking);
    CHECK(frame.lines.front() == "Anna");
}

TEST_CASE("persistent state rides along once the screen is claimed") {
    auto state = connectedState();
    state.talkers.push_back({"Anna", false, t0()});

    const Frame frame = Composer{}.compose(state, t0());
    // Channel info contributes nothing on its own, but appears next to the talker.
    const bool hasChannel = std::any_of(frame.lines.begin(), frame.lines.end(),
                                        [](const std::string& l) { return l.rfind("Lobby", 0) == 0; });
    CHECK(hasChannel);
}

TEST_CASE("talking into a muted mic outranks everything") {
    auto state              = connectedState();
    state.talkingWhileMuted = true;
    state.talkers.push_back({"Anna", false, t0()});
    state.lastPoke = {"Bob", "hey", t0()};

    const Frame frame = Composer{}.compose(state, t0());
    REQUIRE_FALSE(frame.empty());
    CHECK(frame.lines.front() == "MIKRO AUS!");
}

TEST_CASE("a poke expires") {
    auto state     = connectedState();
    state.lastPoke = {"Bob", "", t0()};

    Composer composer;
    CHECK_FALSE(composer.compose(state, t0()).empty());
    CHECK(composer.compose(state, t0() + std::chrono::seconds(60)).empty());
}

TEST_CASE("someone joining the channel is announced once") {
    auto state     = connectedState();
    state.lastJoin = {"Carl", "", t0()};

    Composer composer;
    const Frame frame = composer.compose(state, t0());
    REQUIRE_FALSE(frame.empty());
    CHECK(frame.lines.front() == "Carl");

    CHECK(composer.compose(state, t0() + std::chrono::seconds(60)).empty());
}

TEST_CASE("a chat message is announced") {
    auto state        = connectedState();
    state.lastMessage = {"Dora", "hallo zusammen", t0()};

    const Frame frame = Composer{}.compose(state, t0());
    REQUIRE_FALSE(frame.empty());
    CHECK(frame.lines.front() == "Dora:");
}

TEST_CASE("disconnected and quiet shows nothing") {
    ClientState state;  // never connected
    CHECK(Composer{}.compose(state, t0()).empty());
}

TEST_CASE("frames never exceed the line budget") {
    auto state = connectedState();
    for (const char* name : {"Anna", "Bob", "Carl", "Dora", "Emil"})
        state.talkers.push_back({name, false, t0()});

    const Frame frame = Composer{}.compose(state, t0());
    CHECK(frame.lines.size() <= 3);
}

// There is no scrolling on this display, so anything too long is simply not drawn.
TEST_CASE("fitText respects the width") {
    CHECK(fitText("kurz", 12) == "kurz");
    CHECK(fitText("weit zu langer text", 8).size() == 8);
    CHECK(fitText("", 12).empty());
    CHECK(fitText("abc", 0).empty());
}

TEST_CASE("long names are shortened, not dropped") {
    auto state = connectedState();
    state.talkers.push_back({"EinSehrLangerNickname", false, t0()});

    const Frame frame = Composer{}.compose(state, t0());
    REQUIRE_FALSE(frame.empty());
    CHECK(frame.lines.front().size() <= 12);
    CHECK(frame.lines.front().rfind("EinSehr", 0) == 0);
}

TEST_CASE("all widgets are registered") {
    const auto& all = WidgetRegistry::instance().all();
    CHECK(all.size() >= 8);

    for (const char* id : {"talkers", "mute_status", "talking_while_muted", "channel_info",
                           "channel_join", "connection", "poke", "chat_message"}) {
        CHECK(WidgetRegistry::instance().find(id) != nullptr);
    }
}
