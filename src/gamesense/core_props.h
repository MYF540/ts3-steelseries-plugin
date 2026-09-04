#pragma once

#include <optional>
#include <string>

namespace ts3ss {

// Locates the GameSense HTTP server.
//
// The port changes between GG restarts, so this must be re-read whenever a
// connection attempt is made - never cached across a reconnect.
//
// Returns nullopt when GG is not running. That is a normal state, not an error.
std::optional<std::string> findGameSenseAddress();

}  // namespace ts3ss
