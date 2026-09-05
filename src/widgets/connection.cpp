#include "util/i18n.h"
#include "widgets/registry.h"
#include "widgets/widget.h"

namespace ts3ss {
namespace {


// Connection status.
//
// Reports transitions and problems only, never the steady "connected" state. A widget
// that says "connected" forever would hold the display forever - see ADR 0007. That
// the connection is up is evident from nobody complaining.
class ConnectionWidget final : public IWidget {
public:
    std::string_view id() const override { return "connection"; }
    std::string_view displayName() const override { return tr(Str::WidgetConnection); }

    std::chrono::milliseconds defaultDuration() const override { return std::chrono::seconds(5); }

    std::optional<WidgetOutput> render(const ClientState& state,
                                       const RenderContext& ctx) const override {
        const bool recent = isFresh(state.connectionChangedAt, ctx.now, ctx.eventWindow);

        if (state.connecting) {
            WidgetOutput out;
            out.lines         = {fitText(tr(Str::Connecting), ctx.maxCharsPerLine)};
            out.icon          = Icon::Connect;
            out.demandsScreen = true;
            out.priority      = 40;
            return out;
        }

        // Being disconnected is worth saying once. Saying it permanently would mean the
        // display belongs to us whenever TeamSpeak is merely open.
        if (!state.connected) {
            if (!recent)
                return std::nullopt;

            WidgetOutput out;
            out.lines         = {fitText(tr(Str::Disconnected), ctx.maxCharsPerLine)};
            out.icon          = Icon::Disconnect;
            out.demandsScreen = true;
            out.priority      = 40;
            return out;
        }

        if (!recent)
            return std::nullopt;

        WidgetOutput out;
        out.lines = {fitText(state.serverName.empty() ? std::string(tr(Str::Connected)) : state.serverName,
                             ctx.maxCharsPerLine)};
        out.icon          = Icon::Connect;
        out.demandsScreen = true;
        out.priority      = 40;
        return out;
    }
};

TS3SS_REGISTER_WIDGET(ConnectionWidget)

}  // namespace
}  // namespace ts3ss
