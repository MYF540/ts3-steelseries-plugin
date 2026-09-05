#pragma once

#include <functional>
#include <memory>
#include <string>

#include "config/config.h"

namespace ts3ss {

// The settings window, reachable two ways:
//   - the "Settings" button in TeamSpeak's plugin manager (ts3plugin_configure)
//   - Plugins -> TS3 SteelSeries OLED -> Einstellungen (a global menu item)
//
// Plain Win32, no Qt. Qt belongs to the client, and linking a second copy into its
// process is not something a guest should do (see ADR 0001).
class ConfigDialog {
public:
    // Called on the dialog thread when the user confirms. Receives the edited config;
    // the implementation persists it and swaps it in for the running plugin.
    using ApplyFn = std::function<void(const Config&)>;

    // Supplies the line shown at the bottom - "GameSense verbunden", "GG laeuft nicht".
    // Without it, "nothing appears on the display" has several possible causes and no
    // way to tell them apart short of reading the log.
    using StatusFn = std::function<std::string()>;

    // Opens the window on its own thread and returns immediately.
    //
    // Both entry points need this. ts3plugin_configure would allow a modal loop on the
    // thread TeamSpeak provides, but a menu item arrives on the client's UI thread,
    // where a modal loop would freeze the whole client. One code path for both is
    // simpler than two that must not be confused.
    //
    // A second call while the window is open just brings it to the front.
    static void showAsync(std::shared_ptr<const Config> current, ApplyFn apply, StatusFn status);

    // Closes the window and joins its thread. Must run before the DLL unloads,
    // otherwise the dialog thread outlives the code it is executing.
    static void shutdown();
};

}  // namespace ts3ss
