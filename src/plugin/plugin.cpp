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
#include <string>

#include "plugin_definitions.h"
#include "teamspeak/public_definitions.h"
#include "teamspeak/public_errors.h"
#include "ts3_functions.h"

#include "core/worker.h"
#include "render/frame.h"
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

struct TS3Functions        g_ts3{};
std::unique_ptr<ts3ss::Worker> g_worker;
char*                      g_pluginId = nullptr;

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

// Phase 1 scaffolding: a fixed frame, so "does the DLL load and reach the display?"
// can be answered before any TeamSpeak state exists. Phase 3 replaces this with the
// widget composer.
ts3ss::Frame staticDemoFrame() {
    ts3ss::Frame frame;
    frame.lines = {"TeamSpeak 3", "Plugin aktiv", "v" TS3SS_VERSION};
    frame.icon  = ts3ss::Icon::Connect;
    return frame;
}

}  // namespace

extern "C" {

PLUGINS_EXPORTDLL const char* ts3plugin_name() { return "TS3 SteelSeries OLED"; }
PLUGINS_EXPORTDLL const char* ts3plugin_version() { return TS3SS_VERSION; }
PLUGINS_EXPORTDLL int         ts3plugin_apiVersion() { return kPluginApiVersion; }
PLUGINS_EXPORTDLL const char* ts3plugin_author() { return "linus@bohneberg.eu"; }

PLUGINS_EXPORTDLL const char* ts3plugin_description() {
    return "Zeigt TeamSpeak-Status auf dem OLED der SteelSeries Arctis Basisstation.";
}

PLUGINS_EXPORTDLL void ts3plugin_setFunctionPointers(const struct TS3Functions funcs) {
    g_ts3 = funcs;
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

        ts3ss::WorkerConfig config;
        g_worker = std::make_unique<ts3ss::Worker>(config, &staticDemoFrame);
        g_worker->start();

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

PLUGINS_EXPORTDLL int ts3plugin_offersConfigure() {
    // The configuration dialog arrives in phase 4.
    return PLUGIN_OFFERS_NO_CONFIGURE;
}

PLUGINS_EXPORTDLL int ts3plugin_requestAutoload() { return 0; }

}  // extern "C"
