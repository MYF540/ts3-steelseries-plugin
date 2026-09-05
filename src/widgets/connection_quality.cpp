#include "widgets/registry.h"
#include "widgets/widget.h"

namespace ts3ss {
namespace {

// Thresholds at which a connection stops being merely imperfect and starts being the
// reason people cannot understand you.
constexpr int    kPingWarnMs      = 150;
constexpr double kPacketLossWarn  = 2.0;   // percent

// Ping and packet loss - but only when they are bad.
//
// A permanent ping readout would be a persistent state and would hold the display
// forever (ADR 0007). More to the point, a good ping is not information: it tells you
// nothing you needed to know. Only the bad case is worth interrupting for.
//
// demandsScreen stays true while the problem lasts rather than for a fixed window,
// because unlike a poke this is not a moment - it is a condition you want to keep
// seeing while it persists. That it ends by itself is what keeps it honest.
class ConnectionQualityWidget final : public IWidget {
public:
    std::string_view id() const override { return "connection_quality"; }
    std::string_view displayName() const override { return "Ping / Paketverlust"; }

    std::optional<WidgetOutput> render(const ClientState& state,
                                       const RenderContext& ctx) const override {
        if (!state.connected)
            return std::nullopt;

        const bool badPing = state.pingMs >= 0 && state.pingMs >= kPingWarnMs;
        const bool badLoss = state.packetLoss >= kPacketLossWarn;

        if (!badPing && !badLoss)
            return std::nullopt;

        WidgetOutput out;
        out.icon          = Icon::None;  // twelve characters are not enough for a number
        out.demandsScreen = true;
        out.priority      = 45;  // below a poke, above a chat message

        if (badLoss) {
            // Packet loss is the more damaging of the two: high ping is annoying, loss
            // makes words disappear.
            const int percent = static_cast<int>(state.packetLoss + 0.5);
            out.lines.push_back(fitText("Paketverlust", ctx.maxCharsPerLine));
            out.lines.push_back(fitText(std::to_string(percent) + "%", ctx.maxCharsPerLine));
        } else {
            out.lines.push_back(fitText("Ping hoch", ctx.maxCharsPerLine));
            out.lines.push_back(fitText(std::to_string(state.pingMs) + " ms", ctx.maxCharsPerLine));
        }

        return out;
    }
};

TS3SS_REGISTER_WIDGET(ConnectionQualityWidget)

}  // namespace
}  // namespace ts3ss
