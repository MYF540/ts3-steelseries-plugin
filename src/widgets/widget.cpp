#include "widgets/widget.h"

#include <algorithm>

namespace ts3ss {

bool RenderContext::isBuddy(const std::string& uniqueId) const {
    if (!buddies || uniqueId.empty())
        return false;
    return std::find(buddies->begin(), buddies->end(), uniqueId) != buddies->end();
}

std::string fitText(const std::string& text, int maxChars) {
    if (maxChars <= 0)
        return {};

    const auto limit = static_cast<size_t>(maxChars);
    if (text.size() <= limit)
        return text;

    // A trailing dot signals "there was more" without eating a second character the way
    // a three-dot ellipsis would. At twelve characters per line that matters.
    if (limit == 1)
        return text.substr(0, 1);
    return text.substr(0, limit - 1) + ".";
}

bool isFresh(Timestamp event, Timestamp now, std::chrono::milliseconds window) {
    if (event.time_since_epoch().count() == 0)
        return false;  // never happened
    if (now < event)
        return true;   // clock oddity; treat as just-happened rather than stale
    return (now - event) < window;
}

}  // namespace ts3ss
