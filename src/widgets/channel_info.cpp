#include "util/i18n.h"
#include "widgets/registry.h"
#include "widgets/widget.h"

namespace ts3ss {
namespace {


// Channel name and member count.
//
// Persistent by nature, so it only claims the screen right after a channel change -
// same reasoning as the mute widget (ADR 0007). The rest of the time it rides along on
// somebody else's frame, which is where a channel name is genuinely useful: seeing who
// is talking AND where.
class ChannelInfoWidget final : public IWidget {
public:
    std::string_view id() const override { return "channel_info"; }
    std::string_view displayName() const override { return tr(Str::WidgetChannelInfo); }

    std::chrono::milliseconds defaultDuration() const override { return std::chrono::seconds(4); }

    std::optional<WidgetOutput> render(const ClientState& state,
                                       const RenderContext& ctx) const override {
        if (!state.connected || state.channelName.empty())
            return std::nullopt;

        WidgetOutput out;
        // No icon: the 32 icon pixels cost four characters, and a channel name needs
        // them more than it needs a symbol.
        out.icon = Icon::None;

        // "Lobby 3/7" - active of total. Inactive means microphone-muted or deafened,
        // i.e. present but unable to answer, which is what the bare total hides.
        //
        // Separated by a space rather than " - ": at twelve characters a line, three
        // characters of punctuation is a quarter of a channel name, and there is no
        // scrolling to recover what gets clipped.
        std::string line = state.channelName;
        if (state.channelClientCount > 0) {
            line += " " + std::to_string(state.channelActiveCount) + "/"
                  + std::to_string(state.channelClientCount);
        }

        out.lines         = {fitText(line, ctx.maxCharsPerLine)};
        out.demandsScreen = isFresh(state.channelChangedAt, ctx.now, ctx.eventWindow);
        return out;
    }
};

TS3SS_REGISTER_WIDGET(ChannelInfoWidget)

// Someone entered our channel.
class ChannelJoinWidget final : public IWidget {
public:
    std::string_view id() const override { return "channel_join"; }
    std::string_view displayName() const override { return tr(Str::WidgetChannelJoin); }

    std::chrono::milliseconds defaultDuration() const override { return std::chrono::seconds(5); }

    std::optional<WidgetOutput> render(const ClientState& state,
                                       const RenderContext& ctx) const override {
        if (!state.connected || !isFresh(state.lastJoin.at, ctx.now, ctx.eventWindow))
            return std::nullopt;

        WidgetOutput out;
        out.lines         = {fitText(state.lastJoin.who, ctx.maxCharsPerLine),
                             fitText(tr(Str::IsHere), ctx.maxCharsPerLine)};
        out.icon          = Icon::Connect;
        out.demandsScreen = true;
        out.priority      = 20;
        return out;
    }
};

TS3SS_REGISTER_WIDGET(ChannelJoinWidget)

}  // namespace
}  // namespace ts3ss
