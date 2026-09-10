#include <algorithm>
#include <vector>

#include "util/i18n.h"
#include "widgets/registry.h"
#include "widgets/widget.h"

namespace ts3ss {
namespace {

// Who is audible right now, plus whoever stopped recently enough to still be worth a
// line. The reason this project exists.
//
// The linger is not cosmetic. Without it a name appears and vanishes on every short
// "yeah", and in a lively channel the display never settles. The configured duration of
// this widget is that linger window - for every other widget the duration says how long
// an event stays up, and here it says the same thing about a name.
class TalkersWidget final : public IWidget {
public:
    std::string_view id() const override { return "talkers"; }
    std::string_view displayName() const override { return tr(Str::WidgetTalkers); }

    std::chrono::milliseconds defaultDuration() const override { return std::chrono::seconds(3); }

    // Fuer diese Liste ist die Dauer das Nachleuchten, nicht die Anzeigezeit.
    Str durationLabel() const override { return Str::LabelLinger; }

    std::optional<WidgetOutput> render(const ClientState& state,
                                       const RenderContext& ctx) const override {
        if (!state.connected || state.talkers.empty())
            return std::nullopt;

        std::vector<const TalkerInfo*> visible;
        for (const auto& talker : state.talkers) {
            // Hiding yourself only affects this list. The "talking while muted" warning
            // is a separate widget and stays untouched - it is about you by definition,
            // and suppressing it would remove the single most useful thing this display
            // can tell anyone.
            if (ctx.hideSelfInTalkers && talker.isSelf)
                continue;

            if (talker.speaking || isFresh(talker.lastActive, ctx.now, ctx.eventWindow))
                visible.push_back(&talker);
        }

        if (visible.empty())
            return std::nullopt;

        // Most recent on top, older below. Whoever is still speaking counts as the most
        // recent activity there is, so they sort above anyone who has already stopped.
        std::stable_sort(visible.begin(), visible.end(),
                         [](const TalkerInfo* a, const TalkerInfo* b) {
                             if (a->speaking != b->speaking)
                                 return a->speaking;
                             return a->lastActive > b->lastActive;
                         });

        WidgetOutput out;
        out.icon     = Icon::Talking;
        out.priority = 10;

        // A name inside the linger window still claims the screen. Someone who spoke two
        // seconds ago is a fresh event, exactly like a poke - and the point of the linger
        // is that the display stops twitching, which it would not do if the list vanished
        // the instant the last person went quiet.
        //
        // Consistent with ADR 0007 rather than an exception to it: the window is bounded
        // and user-controlled (1-60 s), so this can never become a permanent claim.
        out.demandsScreen = true;

        const auto budget = static_cast<size_t>(std::max(1, ctx.maxTalkerLines));
        for (const TalkerInfo* talker : visible) {
            if (out.lines.size() >= budget)
                break;
            out.lines.push_back(fitText(talker->name, ctx.maxCharsPerLine));
        }

        return out;
    }
};

TS3SS_REGISTER_WIDGET(TalkersWidget)

}  // namespace
}  // namespace ts3ss
