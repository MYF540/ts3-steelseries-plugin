#pragma once

#include <memory>
#include <vector>

#include "widgets/widget.h"

namespace ts3ss {

// Adding a feature must cost one file plus one registration line - that promise is what
// this registry exists for. The composer and the configuration dialog both read from
// here, so neither needs touching when a widget appears.
class WidgetRegistry {
public:
    static WidgetRegistry& instance();

    // Called during static initialisation via TS3SS_REGISTER_WIDGET.
    static bool add(std::unique_ptr<IWidget> widget);

    const std::vector<std::unique_ptr<IWidget>>& all() const { return widgets_; }

    const IWidget* find(std::string_view id) const;

private:
    std::vector<std::unique_ptr<IWidget>> widgets_;
};

}  // namespace ts3ss

// Registration order is the default display order. The anonymous namespace keeps the
// dummy symbol from colliding between translation units.
#define TS3SS_REGISTER_WIDGET(Type)                                            \
    namespace {                                                                \
    const bool kRegistered_##Type =                                            \
        ::ts3ss::WidgetRegistry::add(std::make_unique<Type>());                \
    }
