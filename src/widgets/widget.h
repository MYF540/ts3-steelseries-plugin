#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/client_state.h"
#include "render/frame.h"

namespace ts3ss {

struct WidgetOutput {
    std::vector<std::string> lines;

    // Wanted icon. There is only ONE per frame; the composer gives it to the
    // highest-priority contributor that asks for one.
    Icon icon = Icon::None;

    // Higher wins when more widgets speak than there are lines. 0 is normal; raise it
    // for things that must cut through, like talking into a muted microphone.
    int priority = 0;

    // Does this justify taking the display away from whatever else is using it?
    //
    // true  = something just happened (someone spoke, a poke arrived, mute was toggled)
    // false = something is currently true (you are muted, the channel is called X)
    //
    // Only a true here can claim the screen. See
    // docs/decisions/0007-transient-vs-persistent.md - defaulting to false is what
    // keeps a careless widget from holding the display forever.
    bool demandsScreen = false;
};

struct RenderContext {
    int maxLines        = 3;
    int maxCharsPerLine = 12;

    // Passed in rather than read from the clock inside widgets, so that "is this event
    // still recent enough" stays deterministic in tests.
    Timestamp now;

    // How long THIS widget's event stays on screen. The composer fills it in per
    // widget from the config, so the value is user-configurable (1-60 s) without any
    // widget having to know that a config exists.
    std::chrono::milliseconds eventWindow{5000};

    // Unique identifiers the user marked as buddies. TeamSpeak's own Friend/Foe list is
    // client-internal and not exposed to plugins, so this is our own.
    const std::vector<std::string>* buddies = nullptr;

    // Above these, connection_quality speaks up. Configurable because "bad" depends on
    // the connection and on how readily the user wants to be interrupted.
    int    pingWarnMs     = 150;
    double packetLossWarn = 2.0;

    bool isBuddy(const std::string& uniqueId) const;
};

class IWidget {
public:
    virtual ~IWidget() = default;

    // Stable, machine-readable - this is the key in config.json. Never change it.
    virtual std::string_view id() const = 0;

    // Shown in the configuration dialog; may change freely.
    virtual std::string_view displayName() const = 0;

    virtual bool enabledByDefault() const { return true; }

    // Used when the config does not mention this widget yet. Clamped to 1-60 s on load.
    virtual std::chrono::milliseconds defaultDuration() const { return std::chrono::seconds(5); }

    // nullopt means "nothing to contribute right now" and is the normal case.
    virtual std::optional<WidgetOutput> render(const ClientState& state,
                                               const RenderContext& ctx) const = 0;
};

// Shortens to fit. There is no scrolling on this display - anything past the limit is
// simply not drawn - so widgets must cut deliberately rather than hope.
std::string fitText(const std::string& text, int maxChars);

// True while an event is fresh enough to be worth the screen.
bool isFresh(Timestamp event, Timestamp now, std::chrono::milliseconds window);

}  // namespace ts3ss
