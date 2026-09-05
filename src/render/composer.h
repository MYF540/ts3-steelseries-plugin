#pragma once

#include <memory>
#include <string>
#include <vector>

#include "config/config.h"
#include "core/client_state.h"
#include "render/frame.h"
#include "widgets/widget.h"

namespace ts3ss {

// Turns widget output into the one frame that goes to the display.
//
// The rule that matters lives here: a frame is produced only when at least one widget
// sets demandsScreen. Persistent state fills leftover lines but can never claim the
// display on its own - see docs/decisions/0007-transient-vs-persistent.md.
class Composer {
public:
    // Without a config, every registered widget runs in registration order with its own
    // default duration - which is exactly what the unit tests want.
    Composer() = default;
    explicit Composer(std::shared_ptr<const Config> config) : config_(std::move(config)) {}

    Frame compose(const ClientState& state, Timestamp now) const;

private:
    std::shared_ptr<const Config> config_;
};

}  // namespace ts3ss
