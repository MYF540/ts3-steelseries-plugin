#include "util/i18n.h"
#include "widgets/registry.h"
#include "widgets/widget.h"

namespace ts3ss {
namespace {

// Who is audible right now. The reason this project exists.
class TalkersWidget final : public IWidget {
public:
    std::string_view id() const override { return "talkers"; }
    std::string_view displayName() const override { return tr(Str::WidgetTalkers); }

    std::optional<WidgetOutput> render(const ClientState& state,
                                       const RenderContext& ctx) const override {
        if (!state.connected || state.talkers.empty())
            return std::nullopt;

        WidgetOutput out;
        out.icon          = Icon::Talking;
        out.demandsScreen = true;  // an event, and the whole point of the display
        out.priority      = 10;

        // One name per line, at most three - the display has no room for more, and a
        // counter in place of the third name would trade a real name for an abstraction
        // nobody can act on.
        for (const auto& talker : state.talkers) {
            if (static_cast<int>(out.lines.size()) >= ctx.maxLines)
                break;
            out.lines.push_back(fitText(talker.name, ctx.maxCharsPerLine));
        }

        return out;
    }
};

TS3SS_REGISTER_WIDGET(TalkersWidget)

}  // namespace
}  // namespace ts3ss
