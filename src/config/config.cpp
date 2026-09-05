#include "config/config.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

#include "util/log.h"
#include "util/win_paths.h"
#include "widgets/registry.h"

namespace ts3ss {
namespace {

using nlohmann::json;

long long clampDuration(long long ms) {
    return std::min(std::max(ms, Config::kMinDuration.count()), Config::kMaxDuration.count());
}

}  // namespace

bool Config::isBuddy(const std::string& uniqueId) const {
    return !uniqueId.empty()
        && std::find(buddies.begin(), buddies.end(), uniqueId) != buddies.end();
}

std::filesystem::path configFilePath() {
    const auto dir = pluginDataDir();
    return dir.empty() ? std::filesystem::path{} : dir / "config.json";
}

void reconcileWithRegistry(Config& config) {
    const auto& registry = WidgetRegistry::instance();

    // Drop ids this build does not know, and collapse duplicates. A config written by a
    // newer version must not take the display down; skipping is the graceful answer.
    std::vector<WidgetConfig> kept;
    std::set<std::string>     seen;

    for (const auto& entry : config.widgets) {
        if (seen.count(entry.id))
            continue;
        if (!registry.find(entry.id)) {
            TS3SS_WARN << "Config names unknown widget '" << entry.id << "' - ignored";
            continue;
        }
        seen.insert(entry.id);

        WidgetConfig copy = entry;
        copy.duration     = std::chrono::milliseconds(clampDuration(entry.duration.count()));
        kept.push_back(std::move(copy));
    }

    // Append widgets the file does not mention. This is how a new feature shows up
    // after an update without anyone deleting their config.
    for (const auto& widget : registry.all()) {
        const std::string id(widget->id());
        if (seen.count(id))
            continue;

        WidgetConfig entry;
        entry.id       = id;
        entry.enabled  = widget->enabledByDefault();
        entry.duration = widget->defaultDuration();
        kept.push_back(std::move(entry));
    }

    config.widgets = std::move(kept);
}

Config loadConfig(const std::filesystem::path& file) {
    Config config;

    std::ifstream in(file);
    if (!in) {
        TS3SS_INFO << "No config file yet, using defaults";
        reconcileWithRegistry(config);
        return config;
    }

    std::ostringstream contents;
    contents << in.rdbuf();

    const auto parsed = json::parse(contents.str(), nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        TS3SS_WARN << "Config file is not valid JSON - using defaults";
        reconcileWithRegistry(config);
        return config;
    }

    config.version = parsed.value("version", Config::kCurrentVersion);
    if (config.version > Config::kCurrentVersion) {
        // Written by a newer build. Read what we understand but never write it back,
        // otherwise downgrading once would silently destroy the user's settings.
        TS3SS_WARN << "Config version " << config.version << " is newer than this build ("
                   << Config::kCurrentVersion << ") - it will not be overwritten";
    }

    config.language = languageFromString(parsed.value("language", std::string("auto")));

    if (const auto it = parsed.find("display"); it != parsed.end() && it->is_object()) {
        config.maxLines         = it->value("max_lines", config.maxLines);
        config.charsWithIcon    = it->value("chars_with_icon", config.charsWithIcon);
        config.charsWithoutIcon = it->value("chars_without_icon", config.charsWithoutIcon);
        config.holdAfterEmpty =
            std::chrono::milliseconds(it->value("hold_ms", config.holdAfterEmpty.count()));
    }

    if (const auto it = parsed.find("thresholds"); it != parsed.end() && it->is_object()) {
        config.pingWarnMs     = it->value("ping_ms", config.pingWarnMs);
        config.packetLossWarn = it->value("packet_loss_percent", config.packetLossWarn);
    }

    // Clamped rather than rejected: a nonsensical threshold should not stop the plugin,
    // and silently ignoring it would leave the user wondering why nothing changed.
    config.pingWarnMs = std::min(std::max(config.pingWarnMs, Config::kMinPingWarnMs),
                                 Config::kMaxPingWarnMs);
    config.packetLossWarn = std::min(std::max(config.packetLossWarn, Config::kMinPacketLossWarn),
                                     Config::kMaxPacketLossWarn);

    if (const auto it = parsed.find("widgets"); it != parsed.end() && it->is_array()) {
        for (const auto& entry : *it) {
            if (!entry.is_object())
                continue;

            WidgetConfig widget;
            widget.id = entry.value("id", std::string{});
            if (widget.id.empty())
                continue;

            widget.enabled  = entry.value("enabled", true);
            widget.duration = std::chrono::milliseconds(
                clampDuration(entry.value("duration_ms", static_cast<long long>(5000))));
            config.widgets.push_back(std::move(widget));
        }
    }

    if (const auto it = parsed.find("buddies"); it != parsed.end() && it->is_array()) {
        for (const auto& entry : *it) {
            if (entry.is_string())
                config.buddies.push_back(entry.get<std::string>());
        }
    }

    reconcileWithRegistry(config);
    return config;
}

bool saveConfig(const std::filesystem::path& file, const Config& config) {
    if (file.empty())
        return false;

    if (config.version > Config::kCurrentVersion) {
        TS3SS_WARN << "Refusing to overwrite a newer config version";
        return false;
    }

    json widgets = json::array();
    for (const auto& widget : config.widgets) {
        widgets.push_back({
            {"id", widget.id},
            {"enabled", widget.enabled},
            {"duration_ms", widget.duration.count()},
        });
    }

    const json document = {
        {"version", Config::kCurrentVersion},
        {"language", languageToString(config.language)},
        {"display",
         {
             {"max_lines", config.maxLines},
             {"chars_with_icon", config.charsWithIcon},
             {"chars_without_icon", config.charsWithoutIcon},
             {"hold_ms", config.holdAfterEmpty.count()},
         }},
        {"thresholds",
         {
             {"ping_ms", config.pingWarnMs},
             {"packet_loss_percent", config.packetLossWarn},
         }},
        {"widgets", widgets},
        {"buddies", config.buddies},
    };

    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);

    std::ofstream out(file, std::ios::trunc);
    if (!out) {
        TS3SS_ERROR << "Cannot write config to " << file.string();
        return false;
    }

    out << document.dump(2) << '\n';
    return out.good();
}

}  // namespace ts3ss
