#include "widgets/registry.h"

#include <algorithm>

namespace ts3ss {

WidgetRegistry& WidgetRegistry::instance() {
    // Function-local static: constructed on first use, so registrations from other
    // translation units cannot run before the registry exists. A namespace-scope
    // instance would depend on static initialisation order across files.
    static WidgetRegistry registry;
    return registry;
}

bool WidgetRegistry::add(std::unique_ptr<IWidget> widget) {
    if (widget)
        instance().widgets_.push_back(std::move(widget));
    return true;
}

const IWidget* WidgetRegistry::find(std::string_view id) const {
    const auto it = std::find_if(widgets_.begin(), widgets_.end(),
                                 [id](const std::unique_ptr<IWidget>& w) { return w->id() == id; });
    return it == widgets_.end() ? nullptr : it->get();
}

}  // namespace ts3ss
