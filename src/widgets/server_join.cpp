#include "util/i18n.h"
#include "widgets/registry.h"
#include "widgets/widget.h"

namespace ts3ss {
namespace {

// Someone connected to the SERVER (as opposed to merely walking into our channel,
// which channel_join covers).
//
// Defaults to buddies only. Announcing every connect on a busy server would claim the
// display constantly, which is precisely the failure mode ADR 0007 exists to prevent -
// and unlike mute, this one gets worse the more people are around.
//
// "Buddy" is our own list: TeamSpeak's Friend/Foe manager is internal to the client and
// is not exposed to plugins at all, so the config carries unique identifiers the user
// marked. See docs/configuration.md.
class ServerJoinWidget final : public IWidget {
public:
    std::string_view id() const override { return "server_join"; }
    std::string_view displayName() const override { return tr(Str::WidgetServerJoin); }

    std::chrono::milliseconds defaultDuration() const override { return std::chrono::seconds(6); }

    std::optional<WidgetOutput> render(const ClientState& state,
                                       const RenderContext& ctx) const override {
        if (!state.connected || !isFresh(state.lastServerJoin.at, ctx.now, ctx.eventWindow))
            return std::nullopt;

        // Without a buddy list nothing is announced. Silence beats a display that
        // reacts to every stranger.
        if (!state.lastServerJoin.buddy)
            return std::nullopt;

        WidgetOutput out;
        out.lines         = {fitText(state.lastServerJoin.who, ctx.maxCharsPerLine),
                             fitText(tr(Str::IsOnline), ctx.maxCharsPerLine)};
        out.icon          = Icon::Connect;
        out.demandsScreen = true;
        out.priority      = 25;  // above channel_join, below chat
        return out;
    }
};

TS3SS_REGISTER_WIDGET(ServerJoinWidget)

// The counterpart: somebody from the buddy list left the server. Also covers a dropped
// connection, because a timeout arrives through the same path.
//
// Buddies-only for the same reason as above, and doubly so here: leaving is a good deal
// less interesting than arriving, and on a busy server it happens constantly.
class ServerLeaveWidget final : public IWidget {
public:
    std::string_view id() const override { return "server_leave"; }
    std::string_view displayName() const override { return tr(Str::WidgetServerLeave); }

    std::chrono::milliseconds defaultDuration() const override { return std::chrono::seconds(6); }

    std::optional<WidgetOutput> render(const ClientState& state,
                                       const RenderContext& ctx) const override {
        if (!state.connected || !isFresh(state.lastServerLeave.at, ctx.now, ctx.eventWindow))
            return std::nullopt;

        if (!state.lastServerLeave.buddy)
            return std::nullopt;

        WidgetOutput out;
        out.lines         = {fitText(state.lastServerLeave.who, ctx.maxCharsPerLine),
                             fitText(tr(Str::IsOffline), ctx.maxCharsPerLine)};
        out.icon          = Icon::Disconnect;
        out.demandsScreen = true;
        out.priority      = 25;  // same weight as coming online
        return out;
    }
};

TS3SS_REGISTER_WIDGET(ServerLeaveWidget)

}  // namespace
}  // namespace ts3ss
