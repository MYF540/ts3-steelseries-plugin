#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "config/config.h"
#include "core/client_state.h"
#include "render/composer.h"
#include "util/i18n.h"
#include "widgets/registry.h"

using namespace ts3ss;

namespace {

// Pinned explicitly: the default is Language::Auto, which follows the Windows UI
// language and would make these assertions depend on the machine they run on.
const struct PinLanguage {
    PinLanguage() { setLanguage(Language::German); }
} kPinLanguage;

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
    CHECK(just.lines.front() == tr(Str::MicOff));

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
    CHECK(frame.lines.front() == tr(Str::SoundOff));
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

// "3/7" says something "7" does not: how many of the people present could answer you.
// Muted or deafened members are there but unreachable.
TEST_CASE("the channel shows active of total") {
    auto state               = connectedState();
    state.channelClientCount = 7;
    state.channelActiveCount = 3;
    state.talkers.push_back({"Anna", false, t0()});

    const Frame frame = Composer{}.compose(state, t0());
    const bool  found = std::any_of(frame.lines.begin(), frame.lines.end(),
                                   [](const std::string& l) { return l == "Lobby 3/7"; });
    CHECK(found);
}

TEST_CASE("talking into a muted mic outranks everything") {
    auto state              = connectedState();
    state.talkingWhileMuted = true;
    state.talkers.push_back({"Anna", false, t0()});
    state.lastPoke = {"Bob", "hey", t0()};

    const Frame frame = Composer{}.compose(state, t0());
    REQUIRE_FALSE(frame.empty());
    CHECK(frame.lines.front() == tr(Str::MutedAlert));
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

// --- Configuration ---------------------------------------------------------

namespace {

std::shared_ptr<const Config> defaultConfig() {
    Config config;
    reconcileWithRegistry(config);  // fills in every registered widget
    return std::make_shared<const Config>(config);
}

}  // namespace

TEST_CASE("the ping threshold is configurable") {
    auto state    = connectedState();
    state.pingMs  = 100;

    Config config = *defaultConfig();

    config.pingWarnMs = 150;  // 100 ms is fine
    CHECK(Composer(std::make_shared<const Config>(config)).compose(state, t0()).empty());

    config.pingWarnMs = 80;  // now 100 ms is not
    const Frame frame = Composer(std::make_shared<const Config>(config)).compose(state, t0());
    REQUIRE_FALSE(frame.empty());
    CHECK(frame.lines.front() == tr(Str::PingHigh));
}

TEST_CASE("the packet loss threshold is configurable") {
    auto state       = connectedState();
    state.packetLoss = 1.5;

    Config config = *defaultConfig();

    config.packetLossWarn = 2.0;
    CHECK(Composer(std::make_shared<const Config>(config)).compose(state, t0()).empty());

    config.packetLossWarn = 1.0;
    const Frame frame = Composer(std::make_shared<const Config>(config)).compose(state, t0());
    REQUIRE_FALSE(frame.empty());
    CHECK(frame.lines.front() == tr(Str::PacketLoss));
}

// An unmeasured ping is not a good ping - staying silent is the honest answer.
TEST_CASE("an unmeasured connection stays silent") {
    auto state = connectedState();  // pingMs == -1, packetLoss == -1
    CHECK(Composer(defaultConfig()).compose(state, t0()).empty());
}

TEST_CASE("a disabled widget contributes nothing") {
    auto state = connectedState();
    state.talkers.push_back({"Anna", false, t0()});

    Config config = *defaultConfig();
    for (auto& widget : config.widgets) {
        if (widget.id == "talkers")
            widget.enabled = false;
    }

    CHECK(Composer(std::make_shared<const Config>(config)).compose(state, t0()).empty());
}

TEST_CASE("durations are clamped to 1-60 s on load") {
    Config config;
    config.widgets.push_back({"poke", true, std::chrono::milliseconds(1)});
    config.widgets.push_back({"talkers", true, std::chrono::hours(5)});
    reconcileWithRegistry(config);

    for (const auto& widget : config.widgets) {
        CHECK(widget.duration >= Config::kMinDuration);
        CHECK(widget.duration <= Config::kMaxDuration);
    }
}

// A config from a newer build must not take the display down.
TEST_CASE("unknown widget ids are dropped, missing ones appended") {
    Config config;
    config.widgets.push_back({"widget_from_the_future", true, std::chrono::seconds(5)});
    reconcileWithRegistry(config);

    const bool ghost = std::any_of(config.widgets.begin(), config.widgets.end(),
                                   [](const WidgetConfig& w) {
                                       return w.id == "widget_from_the_future";
                                   });
    CHECK_FALSE(ghost);
    CHECK(config.widgets.size() == WidgetRegistry::instance().all().size());
}

// --- Translation -----------------------------------------------------------

TEST_CASE("switching language changes the output") {
    auto state               = connectedState();
    state.inputMuted         = true;
    state.selfFlagsChangedAt = t0();

    setLanguage(Language::English);
    CHECK(Composer{}.compose(state, t0()).lines.front() == "Mic off");

    setLanguage(Language::German);
    CHECK(Composer{}.compose(state, t0()).lines.front() == "Mikro aus");
}

TEST_CASE("every string has both translations") {
    for (int i = 0; i < static_cast<int>(Str::Count); ++i) {
        setLanguage(Language::German);
        const std::string de = tr(static_cast<Str>(i));
        setLanguage(Language::English);
        const std::string en = tr(static_cast<Str>(i));

        CHECK_FALSE(de.empty());
        CHECK_FALSE(en.empty());
    }
    setLanguage(Language::German);
}

TEST_CASE("all widgets are registered") {
    const auto& all = WidgetRegistry::instance().all();
    CHECK(all.size() >= 8);

    for (const char* id : {"talkers", "mute_status", "talking_while_muted", "channel_info",
                           "channel_join", "connection", "poke", "chat_message"}) {
        CHECK(WidgetRegistry::instance().find(id) != nullptr);
    }
}
