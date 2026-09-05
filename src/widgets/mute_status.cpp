#include "widgets/registry.h"
#include "widgets/widget.h"

namespace ts3ss {
namespace {

// Mute and deaf.
//
// This widget is the reason ADR 0007 exists. Being muted is a STATE, not an event - a
// user can sit muted for hours. Reporting it permanently held the screen forever and
// kept flickering against NowPlaying, which is exactly what ADR 0006 set out to avoid.
//
// So: claim the screen only just after the toggle, then keep quiet while still showing
// the line if something else is holding the display anyway.
class MuteStatusWidget final : public IWidget {
public:
    std::string_view id() const override { return "mute_status"; }
    std::string_view displayName() const override { return "Mute / Deaf"; }

    // A mute toggle is worth a brief look, not a lingering banner.
    std::chrono::milliseconds defaultDuration() const override { return std::chrono::seconds(4); }

    std::optional<WidgetOutput> render(const ClientState& state,
                                       const RenderContext& ctx) const override {
        if (!state.connected)
            return std::nullopt;

        if (!state.inputMuted && !state.outputMuted && !state.away)
            return std::nullopt;

        WidgetOutput out;
        out.icon = Icon::Muted;

        // Deaf implies microphone mute per the TeamSpeak header, so saying both would
        // be redundant.
        if (state.outputMuted)
            out.lines.push_back("Ton aus");
        else if (state.inputMuted)
            out.lines.push_back("Mikro aus");
        else
            out.lines.push_back("Abwesend");

        out.lines.back() = fitText(out.lines.back(), ctx.maxCharsPerLine);

        // The switch is the event; the state that follows is not.
        out.demandsScreen = isFresh(state.selfFlagsChangedAt, ctx.now, ctx.eventWindow);
        return out;
    }
};

TS3SS_REGISTER_WIDGET(MuteStatusWidget)

// Talking into a muted microphone. Separate from the widget above because it IS an
// event, it outranks everything, and it is the single most useful thing this display
// can tell anyone.
class TalkingWhileMutedWidget final : public IWidget {
public:
    std::string_view id() const override { return "talking_while_muted"; }
    std::string_view displayName() const override { return "Stumm gesprochen"; }

    std::optional<WidgetOutput> render(const ClientState& state,
                                       const RenderContext& ctx) const override {
        if (!state.connected || !state.talkingWhileMuted)
            return std::nullopt;

        WidgetOutput out;
        out.lines         = {fitText("MIKRO AUS!", ctx.maxCharsPerLine),
                             fitText("du sprichst", ctx.maxCharsPerLine)};
        out.icon          = Icon::Muted;
        out.demandsScreen = true;
        out.priority      = 100;  // nothing outranks this
        return out;
    }
};

TS3SS_REGISTER_WIDGET(TalkingWhileMutedWidget)

}  // namespace
}  // namespace ts3ss
