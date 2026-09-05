#include "util/i18n.h"
#include "widgets/registry.h"
#include "widgets/widget.h"

namespace ts3ss {
namespace {

class PokeWidget final : public IWidget {
public:
    std::string_view id() const override { return "poke"; }
    std::string_view displayName() const override { return tr(Str::WidgetPoke); }

    // A poke demands attention by definition, so it outstays a passing message.
    std::chrono::milliseconds defaultDuration() const override { return std::chrono::seconds(8); }

    std::optional<WidgetOutput> render(const ClientState& state,
                                       const RenderContext& ctx) const override {
        if (!isFresh(state.lastPoke.at, ctx.now, ctx.eventWindow))
            return std::nullopt;

        WidgetOutput out;
        out.lines.push_back(fitText(tr(Str::PokeFrom), ctx.maxCharsPerLine));
        out.lines.push_back(fitText(state.lastPoke.who, ctx.maxCharsPerLine));
        if (!state.lastPoke.text.empty() && ctx.maxLines >= 3)
            out.lines.push_back(fitText(state.lastPoke.text, ctx.maxCharsPerLine));

        out.icon          = Icon::None;  // no bell icon exists; keep the width instead
        out.demandsScreen = true;
        out.priority      = 50;  // below "talking while muted", above everything else
        return out;
    }
};

TS3SS_REGISTER_WIDGET(PokeWidget)

class ChatMessageWidget final : public IWidget {
public:
    std::string_view id() const override { return "chat_message"; }
    std::string_view displayName() const override { return tr(Str::WidgetChatMessage); }

    std::chrono::milliseconds defaultDuration() const override { return std::chrono::seconds(6); }

    std::optional<WidgetOutput> render(const ClientState& state,
                                       const RenderContext& ctx) const override {
        if (!isFresh(state.lastMessage.at, ctx.now, ctx.eventWindow))
            return std::nullopt;

        WidgetOutput out;
        out.lines.push_back(fitText(state.lastMessage.who + ":", ctx.maxCharsPerLine));

        // Twelve characters of a chat message is not a message, it is a hint that one
        // arrived. Two lines make it occasionally readable; more would crowd out
        // everything else.
        if (!state.lastMessage.text.empty()) {
            const auto width = static_cast<size_t>(ctx.maxCharsPerLine);
            out.lines.push_back(fitText(state.lastMessage.text.substr(0, width), ctx.maxCharsPerLine));

            if (state.lastMessage.text.size() > width && ctx.maxLines >= 3) {
                out.lines.push_back(
                    fitText(state.lastMessage.text.substr(width), ctx.maxCharsPerLine));
            }
        }

        out.icon          = Icon::None;
        out.demandsScreen = true;
        out.priority      = 30;
        return out;
    }
};

TS3SS_REGISTER_WIDGET(ChatMessageWidget)

}  // namespace
}  // namespace ts3ss
