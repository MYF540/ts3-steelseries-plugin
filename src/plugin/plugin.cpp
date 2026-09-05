// The TeamSpeak ABI boundary. The ONLY translation unit that includes the TeamSpeak
// headers - everything below src/plugin/ stays free of them and therefore testable
// without a running client.
//
// Two rules hold without exception here:
//   1. No exception may escape a ts3plugin_* function. We share the client's address
//      space; an escaping exception takes TeamSpeak down with us.
//   2. Nothing may block. These run on a client thread that also serves audio.

#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

#include "plugin_definitions.h"
#include "teamspeak/public_definitions.h"
#include "teamspeak/public_errors.h"
#include "ts3_functions.h"

#include "config/config.h"
#include "config/config_dialog.h"
#include "core/state_store.h"
#include "core/worker.h"
#include "gamesense/core_props.h"
#include "plugin/state_sync.h"
#include "plugin/ts3_context.h"
#include "render/composer.h"
#include "render/frame.h"
#include "widgets/registry.h"
#include "util/i18n.h"
#include "util/log.h"
#include "util/win_paths.h"
#include "version.h"

#ifdef _WIN32
#define PLUGINS_EXPORTDLL __declspec(dllexport)
#else
#define PLUGINS_EXPORTDLL __attribute__((visibility("default")))
#endif

namespace {

// Must match the client. TeamSpeak 3.6.x uses 26 (third_party/ts3-pluginsdk/src/plugin.c).
// On mismatch the client refuses the plugin WITHOUT any message - first thing to check
// when it stops loading after a client update.
constexpr int kPluginApiVersion = 26;

struct TS3Functions               g_ts3{};
ts3ss::Ts3Context                 g_context;
ts3ss::StateStore                 g_store;
std::unique_ptr<ts3ss::StateSync> g_sync;
std::unique_ptr<ts3ss::Worker>    g_worker;
char*                             g_pluginId = nullptr;

// Runs a body that must never throw across the ABI edge.
template <typename Fn>
void guard(const char* what, Fn&& body) noexcept {
    try {
        body();
    } catch (const std::exception& e) {
        TS3SS_ERROR << what << " threw: " << e.what();
    } catch (...) {
        TS3SS_ERROR << what << " threw an unknown exception";
    }
}

void installLogSink() {
    ts3ss::logSetSink([](ts3ss::LogLevel level, const std::string& message) {
        if (!g_ts3.logMessage)
            return;

        LogLevel severity = LogLevel_INFO;
        switch (level) {
            case ts3ss::LogLevel::Debug: severity = LogLevel_DEBUG;   break;
            case ts3ss::LogLevel::Info:  severity = LogLevel_INFO;    break;
            case ts3ss::LogLevel::Warn:  severity = LogLevel_WARNING; break;
            case ts3ss::LogLevel::Error: severity = LogLevel_ERROR;   break;
        }
        g_ts3.logMessage(message.c_str(), severity, "ts3_steelseries", 0);
    });
}

// Config is handed around as a shared_ptr to const and swapped atomically. The render
// path therefore needs no lock, and a frame in flight finishes with the config it
// started on instead of changing halfway through.
std::shared_ptr<const ts3ss::Config> g_config;
std::mutex                           g_configMutex;

std::shared_ptr<const ts3ss::Config> currentConfig() {
    std::lock_guard<std::mutex> lock(g_configMutex);
    return g_config;
}

// Handed to the worker. All the "what is worth showing" logic lives in the widgets;
// this only bridges the store to the composer.
ts3ss::Frame frameFromState() {
    return ts3ss::Composer(currentConfig())
        .compose(g_store.snapshot(), std::chrono::steady_clock::now());
}

// --- Settings dialog -------------------------------------------------------

void openSettings() {
    ts3ss::ConfigDialog::showAsync(
        currentConfig(),
        [](const ts3ss::Config& edited) {
            ts3ss::saveConfig(ts3ss::configFilePath(), edited);
            {
                std::lock_guard<std::mutex> lock(g_configMutex);
                g_config = std::make_shared<const ts3ss::Config>(edited);
            }
            ts3ss::setLanguage(edited.language);

            // Hot reload: no TeamSpeak restart. The worker picks up the new pointer on
            // its next tick, and a frame already in flight finishes on the old config
            // rather than changing halfway through.
            if (g_worker)
                g_worker->notifyChanged();

            TS3SS_INFO << "Settings applied";
        },
        []() -> std::string {
            // "Nothing appears on the display" has several possible causes; without
            // this line the only way to tell them apart is reading the log.
            if (const auto address = ts3ss::findGameSenseAddress())
                return std::string(ts3ss::tr(ts3ss::Str::StatusConnected)) + " (" + *address + ")";
            return ts3ss::tr(ts3ss::Str::StatusNoGameSense);
        });
}

// Menu ids. Reported back through ts3plugin_onMenuItemEvent.
enum MenuId {
    kMenuGlobalSettings = 1,
    kMenuClientAddBuddy = 2,
};

struct PluginMenuItem* createMenuItem(enum PluginMenuType type, int id, const char* text) {
    auto* item = static_cast<struct PluginMenuItem*>(malloc(sizeof(struct PluginMenuItem)));
    if (!item)
        return nullptr;

    item->type = type;
    item->id   = id;
    strncpy_s(item->text, PLUGIN_MENU_BUFSZ, text, _TRUNCATE);
    item->icon[0] = '\0';  // no icon files shipped
    return item;
}

// Adds the client behind a context-menu click to the buddy list.
//
// Necessary because TeamSpeak's own Friend/Foe manager is not exposed to plugins at
// all, so buddies have to be collected by us. Storing the unique identifier rather than
// the nickname is what makes the entry survive a rename.
void addBuddy(uint64 schid, anyID clientId) {
    const auto uid = g_context.clientString(schid, clientId, CLIENT_UNIQUE_IDENTIFIER);
    if (!uid || uid->empty()) {
        TS3SS_WARN << "Could not read unique identifier for client " << clientId;
        return;
    }

    auto config = currentConfig();
    if (!config)
        return;

    ts3ss::Config edited = *config;
    if (edited.isBuddy(*uid)) {
        TS3SS_INFO << "Already a buddy: " << *uid;
        return;
    }

    edited.buddies.push_back(*uid);
    ts3ss::saveConfig(ts3ss::configFilePath(), edited);

    {
        std::lock_guard<std::mutex> lock(g_configMutex);
        g_config = std::make_shared<const ts3ss::Config>(edited);
    }

    const auto name = g_context.clientString(schid, clientId, CLIENT_NICKNAME).value_or(*uid);
    TS3SS_INFO << "Added buddy: " << name;
}

}  // namespace

extern "C" {

PLUGINS_EXPORTDLL const char* ts3plugin_name() { return "TS3 SteelSeries OLED"; }
PLUGINS_EXPORTDLL const char* ts3plugin_version() { return TS3SS_VERSION; }
PLUGINS_EXPORTDLL int         ts3plugin_apiVersion() { return kPluginApiVersion; }
PLUGINS_EXPORTDLL const char* ts3plugin_author() { return "MYF540"; }

PLUGINS_EXPORTDLL const char* ts3plugin_description() {
    return "Zeigt TeamSpeak-Status auf dem OLED der SteelSeries Arctis Basisstation.";
}

PLUGINS_EXPORTDLL void ts3plugin_setFunctionPointers(const struct TS3Functions funcs) {
    g_ts3 = funcs;
    g_context.setFunctions(funcs);
}

PLUGINS_EXPORTDLL int ts3plugin_init() {
    // Cannot use guard(): this one has to report failure to the client.
    try {
        const auto dataDir = ts3ss::pluginDataDir();
        ts3ss::logInit(dataDir / "ts3_steelseries.log");
        ts3ss::logSetLevel(ts3ss::LogLevel::Debug);
        installLogSink();

        TS3SS_INFO << "Initialising v" << TS3SS_VERSION << " (plugin API " << kPluginApiVersion
                   << ")";

        // Also a self-check: widgets register through static initialisers, and a count
        // of zero would mean the registry never ran - a failure that would otherwise
        // just look like a permanently blank display.
        {
            ts3ss::LogStream line(ts3ss::LogLevel::Info);
            line << "Widgets registered: " << ts3ss::WidgetRegistry::instance().all().size() << " -";
            for (const auto& widget : ts3ss::WidgetRegistry::instance().all())
                line << " " << widget->id();
        }

        // Written back immediately so the user has a file to edit, complete with every
        // widget this build knows.
        {
            const auto loaded = ts3ss::loadConfig(ts3ss::configFilePath());
            ts3ss::saveConfig(ts3ss::configFilePath(), loaded);

            std::lock_guard<std::mutex> lock(g_configMutex);
            g_config = std::make_shared<const ts3ss::Config>(loaded);
        }
        ts3ss::setLanguage(currentConfig()->language);

        ts3ss::WorkerConfig workerConfig;
        workerConfig.holdAfterEmpty = currentConfig()->holdAfterEmpty;
        g_worker = std::make_unique<ts3ss::Worker>(workerConfig, &frameFromState);

        // The callback only pokes the worker; it must stay this cheap, because it runs
        // on a TeamSpeak thread that also serves audio.
        g_sync = std::make_unique<ts3ss::StateSync>(g_context, g_store, &currentConfig, [] {
            if (g_worker)
                g_worker->notifyChanged();
        });

        g_worker->start();

        // TeamSpeak may already be connected when a plugin is enabled at runtime, in
        // which case no connect event is coming.
        g_sync->setActiveServer(g_context.currentServerConnectionHandler());

        return 0;  // 0 = success
    } catch (const std::exception& e) {
        TS3SS_ERROR << "ts3plugin_init failed: " << e.what();
        return 1;
    } catch (...) {
        TS3SS_ERROR << "ts3plugin_init failed with an unknown exception";
        return 1;
    }
}

PLUGINS_EXPORTDLL void ts3plugin_shutdown() {
    guard("ts3plugin_shutdown", [] {
        TS3SS_INFO << "Shutting down";

        // Joins the worker, which releases the display on its way out. Must happen
        // before the log sink is dropped, so the release is still traceable.
        // The dialog runs on its own thread inside this DLL. If it outlived unload it
        // would be executing freed code, so it goes first.
        ts3ss::ConfigDialog::shutdown();

        // Sync next: it holds a callback into the worker, so it must stop poking one
        // that is being torn down.
        g_sync.reset();

        if (g_worker) {
            g_worker->stop();
            g_worker.reset();
        }

        if (g_pluginId) {
            free(g_pluginId);
            g_pluginId = nullptr;
        }

        ts3ss::logShutdown();
    });

    // The client frees nothing for us and drops the function pointers after this
    // returns, so the sink must be gone by now - logShutdown() above does that.
}

PLUGINS_EXPORTDLL void ts3plugin_registerPluginID(const char* id) {
    guard("ts3plugin_registerPluginID", [id] {
        if (!id)
            return;
        const size_t size = strlen(id) + 1;
        g_pluginId        = static_cast<char*>(malloc(size));
        if (g_pluginId)
            memcpy(g_pluginId, id, size);
    });
}

// Memory handed to the client is allocated by us and freed here, so there is no CRT
// mismatch across the boundary.
PLUGINS_EXPORTDLL void ts3plugin_freeMemory(void* data) { free(data); }

// Enables the "Settings" button next to the plugin in TeamSpeak's addon list.
PLUGINS_EXPORTDLL int ts3plugin_offersConfigure() {
    return PLUGIN_OFFERS_CONFIGURE_NEW_THREAD;
}

PLUGINS_EXPORTDLL void ts3plugin_configure(void* handle, void* qParentWidget) {
    // Both are Qt objects. We deliberately do not touch Qt (ADR 0001) and open a plain
    // Win32 window with no parent instead.
    (void)handle;
    (void)qParentWidget;
    guard("ts3plugin_configure", [] { openSettings(); });
}

// Plugins menu in the main window, plus a client context-menu entry for buddies.
PLUGINS_EXPORTDLL void ts3plugin_initMenus(struct PluginMenuItem*** menuItems, char** menuIcon) {
    // The array is NULL-terminated, hence the extra slot. TeamSpeak frees every
    // allocation here through ts3plugin_freeMemory.
    constexpr size_t kCount = 2;
    *menuItems = static_cast<struct PluginMenuItem**>(
        malloc(sizeof(struct PluginMenuItem*) * (kCount + 1)));
    if (!*menuItems) {
        *menuIcon = nullptr;
        return;
    }

    (*menuItems)[0] =
        createMenuItem(PLUGIN_MENU_TYPE_GLOBAL, kMenuGlobalSettings, "Einstellungen");
    (*menuItems)[1] =
        createMenuItem(PLUGIN_MENU_TYPE_CLIENT, kMenuClientAddBuddy, "Als Buddy merken");
    (*menuItems)[kCount] = nullptr;

    // No icon files are shipped, so no icon is requested.
    *menuIcon = nullptr;
}

PLUGINS_EXPORTDLL void ts3plugin_onMenuItemEvent(uint64 schid, enum PluginMenuType type,
                                                 int menuItemID, uint64 selectedItemID) {
    guard("onMenuItemEvent", [&] {
        switch (menuItemID) {
            case kMenuGlobalSettings:
                openSettings();
                break;

            case kMenuClientAddBuddy:
                if (type == PLUGIN_MENU_TYPE_CLIENT)
                    addBuddy(schid, static_cast<anyID>(selectedItemID));
                break;

            default:
                break;
        }
    });
}

PLUGINS_EXPORTDLL int ts3plugin_requestAutoload() { return 0; }

// ---------------------------------------------------------------------------
// Event callbacks
//
// All of these run on a TeamSpeak thread. They resolve what they need, write it into
// the store, and return - nothing here waits on anything.
// ---------------------------------------------------------------------------

PLUGINS_EXPORTDLL void ts3plugin_onConnectStatusChangeEvent(uint64 schid, int newStatus,
                                                            unsigned int errorNumber) {
    (void)errorNumber;
    guard("onConnectStatusChangeEvent", [&] {
        if (g_sync)
            g_sync->onConnectStatusChanged(schid, newStatus);
    });
}

PLUGINS_EXPORTDLL void ts3plugin_onTalkStatusChangeEvent(uint64 schid, int status,
                                                         int isReceivedWhisper, anyID clientID) {
    guard("onTalkStatusChangeEvent", [&] {
        if (g_sync)
            g_sync->onTalkStatusChanged(schid, status, isReceivedWhisper != 0, clientID);
    });
}

PLUGINS_EXPORTDLL void ts3plugin_onClientSelfVariableUpdateEvent(uint64 schid, int flag,
                                                                 const char* oldValue,
                                                                 const char* newValue) {
    (void)oldValue;
    (void)newValue;
    guard("onClientSelfVariableUpdateEvent", [&] {
        if (g_sync)
            g_sync->onSelfVariableUpdated(schid, flag);
    });
}

PLUGINS_EXPORTDLL void ts3plugin_onClientMoveEvent(uint64 schid, anyID clientID,
                                                   uint64 oldChannelID, uint64 newChannelID,
                                                   int visibility, const char* moveMessage) {
    (void)visibility;
    (void)moveMessage;
    guard("onClientMoveEvent", [&] {
        if (g_sync)
            g_sync->onClientMoved(schid, clientID, oldChannelID, newChannelID);
    });
}

// Being dragged by someone else is a separate callback from moving yourself. Handling
// only the first would silently freeze the channel display whenever an admin moves you.
PLUGINS_EXPORTDLL void ts3plugin_onClientMoveMovedEvent(uint64 schid, anyID clientID,
                                                        uint64 oldChannelID, uint64 newChannelID,
                                                        int visibility, anyID moverID,
                                                        const char* moverName,
                                                        const char* moverUniqueIdentifier,
                                                        const char* moveMessage) {
    (void)visibility;
    (void)moverID;
    (void)moverName;
    (void)moverUniqueIdentifier;
    (void)moveMessage;
    guard("onClientMoveMovedEvent", [&] {
        if (g_sync)
            g_sync->onClientMoved(schid, clientID, oldChannelID, newChannelID);
    });
}

// Someone in our channel dropped out. Without this their name would stay in the member
// count until the next full rebuild.
PLUGINS_EXPORTDLL void ts3plugin_onClientMoveTimeoutEvent(uint64 schid, anyID clientID,
                                                          uint64 oldChannelID, uint64 newChannelID,
                                                          int visibility,
                                                          const char* timeoutMessage) {
    (void)visibility;
    (void)timeoutMessage;
    guard("onClientMoveTimeoutEvent", [&] {
        if (g_sync)
            g_sync->onClientMoved(schid, clientID, oldChannelID, newChannelID);
    });
}

// Another client's mute, deaf or away flag changed. Without this the active/total count
// in the channel line would only refresh when somebody moves.
PLUGINS_EXPORTDLL void ts3plugin_onUpdateClientEvent(uint64 schid, anyID clientID,
                                                     anyID invokerID, const char* invokerName,
                                                     const char* invokerUniqueIdentifier) {
    (void)invokerID;
    (void)invokerName;
    (void)invokerUniqueIdentifier;
    guard("onUpdateClientEvent", [&] {
        if (g_sync)
            g_sync->onClientUpdated(schid, clientID);
    });
}

PLUGINS_EXPORTDLL void ts3plugin_onUpdateChannelEditedEvent(uint64 schid, uint64 channelID,
                                                            anyID invokerID,
                                                            const char* invokerName,
                                                            const char* invokerUniqueIdentifier) {
    (void)invokerID;
    (void)invokerName;
    (void)invokerUniqueIdentifier;
    guard("onUpdateChannelEditedEvent", [&] {
        if (g_sync)
            g_sync->onChannelEdited(schid, channelID);
    });
}

PLUGINS_EXPORTDLL void ts3plugin_currentServerConnectionChanged(uint64 schid) {
    guard("currentServerConnectionChanged", [&] {
        if (g_sync)
            g_sync->setActiveServer(schid);
    });
}

PLUGINS_EXPORTDLL int ts3plugin_onClientPokeEvent(uint64 schid, anyID fromClientID,
                                                  const char* pokerName,
                                                  const char* pokerUniqueIdentity,
                                                  const char* message, int ffIgnored) {
    (void)fromClientID;
    (void)pokerUniqueIdentity;
    (void)ffIgnored;
    guard("onClientPokeEvent", [&] {
        if (g_sync)
            g_sync->onPoked(schid, pokerName, message);
    });
    // 0 = do not swallow the event; TeamSpeak still shows its own poke dialog.
    return 0;
}

PLUGINS_EXPORTDLL void ts3plugin_onConnectionInfoEvent(uint64 schid, anyID clientID) {
    guard("onConnectionInfoEvent", [&] {
        if (g_sync)
            g_sync->onConnectionInfo(schid, clientID);
    });
}

PLUGINS_EXPORTDLL int ts3plugin_onTextMessageEvent(uint64 schid, anyID targetMode,
                                                   anyID toID, anyID fromID,
                                                   const char* fromName,
                                                   const char* fromUniqueIdentifier,
                                                   const char* message, int ffIgnored) {
    (void)targetMode;
    (void)toID;
    (void)fromUniqueIdentifier;
    (void)ffIgnored;
    guard("onTextMessageEvent", [&] {
        if (g_sync)
            g_sync->onTextMessage(schid, fromID, fromName, message);
    });
    return 0;
}

}  // extern "C"
