#include "render/composer.h"

#include <algorithm>

#include "widgets/registry.h"

namespace ts3ss {
namespace {

struct Entry {
    const IWidget*            widget;
    std::chrono::milliseconds duration;
};

struct Contribution {
    WidgetOutput output;
    size_t       userIndex;  // position in the configured order, for stable sorting
};

struct Pass {
    std::vector<Contribution> contributions;
    bool                      anyDemandsScreen = false;
    Icon                      icon             = Icon::None;
};

Pass runPass(const std::vector<Entry>& entries, const ClientState& state, RenderContext ctx) {
    Pass pass;

    for (size_t i = 0; i < entries.size(); ++i) {
        // Each widget sees its own configured duration, so "how long does a poke stay
        // up" is a user setting without any widget knowing a config exists.
        ctx.eventWindow = entries[i].duration;

        auto output = entries[i].widget->render(state, ctx);
        if (!output || output->lines.empty())
            continue;

        pass.anyDemandsScreen = pass.anyDemandsScreen || output->demandsScreen;
        pass.contributions.push_back({std::move(*output), i});
    }

    // Order: whoever claimed the screen first, then by priority, then the user's order.
    //
    // The demandsScreen key is not cosmetic. Without it a persistent widget that merely
    // rides along can take the top line away from the event that caused the display to
    // appear at all - muting the microphone would show "Lobby 3" above "Mikro aus"
    // purely because channel_info happens to be registered earlier.
    std::stable_sort(pass.contributions.begin(), pass.contributions.end(),
                     [](const Contribution& a, const Contribution& b) {
                         if (a.output.demandsScreen != b.output.demandsScreen)
                             return a.output.demandsScreen;
                         if (a.output.priority != b.output.priority)
                             return a.output.priority > b.output.priority;
                         return a.userIndex < b.userIndex;
                     });

    // One icon per frame - GameSense puts icon-id on the frame, not the line. After
    // sorting, the first contributor that wants one is the most urgent.
    for (const auto& contribution : pass.contributions) {
        if (contribution.output.icon != Icon::None) {
            pass.icon = contribution.output.icon;
            break;
        }
    }

    return pass;
}

}  // namespace

Frame Composer::compose(const ClientState& state, Timestamp now) const {
    const auto& registry = WidgetRegistry::instance();

    std::vector<Entry> entries;
    if (config_) {
        for (const auto& widget : config_->widgets) {
            if (!widget.enabled)
                continue;
            // An id the registry does not know means the config names a widget this
            // build lacks. Skipping beats failing: a config from a newer version must
            // not brick the display.
            if (const IWidget* found = registry.find(widget.id))
                entries.push_back({found, widget.duration});
        }
    } else {
        for (const auto& widget : registry.all())
            entries.push_back({widget.get(), widget->defaultDuration()});
    }

    RenderContext ctx;
    ctx.maxLines = config_ ? config_->maxLines : 3;
    ctx.now      = now;
    ctx.buddies  = config_ ? &config_->buddies : nullptr;

    const int withIcon    = config_ ? config_->charsWithIcon : 12;
    const int withoutIcon = config_ ? config_->charsWithoutIcon : 16;

    // First pass uses the narrow width, because whether an icon appears depends on what
    // the widgets return and that is not known yet. Guessing wide would produce text
    // that gets clipped on the device - there is no scrolling to rescue it.
    ctx.maxCharsPerLine = withIcon;
    Pass pass           = runPass(entries, state, ctx);

    // Nothing happened. Hand the screen back to SteelSeries GG.
    // Persistent state alone never gets here - see ADR 0007.
    if (!pass.anyDemandsScreen)
        return Frame{};

    // No icon after all, so four more characters per line are available. Widgets must
    // render again to use them: they already shortened their text, and widening
    // afterwards cannot restore characters that are gone.
    if (pass.icon == Icon::None && withoutIcon > withIcon) {
        ctx.maxCharsPerLine = withoutIcon;
        pass                = runPass(entries, state, ctx);
    }

    Frame frame;
    frame.icon = pass.icon;

    const int maxLines = ctx.maxLines;
    for (const auto& contribution : pass.contributions) {
        for (const auto& line : contribution.output.lines) {
            if (static_cast<int>(frame.lines.size()) >= maxLines)
                return frame;
            frame.lines.push_back(line);
        }
    }

    return frame;
}

}  // namespace ts3ss
