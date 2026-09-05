#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace ts3ss {

struct WidgetConfig {
    std::string               id;
    bool                      enabled = true;
    std::chrono::milliseconds duration{5000};
};

struct Config {
    static constexpr int kCurrentVersion = 1;

    // Clamped on load. A one-second floor keeps a notification readable at all; a
    // one-minute ceiling keeps a "duration" from quietly becoming a permanent claim on
    // the display, which is what ADR 0006 and 0007 exist to prevent.
    static constexpr std::chrono::milliseconds kMinDuration{1000};
    static constexpr std::chrono::milliseconds kMaxDuration{60000};

    int version = kCurrentVersion;

    // Display geometry. Measured values, see docs/gamesense-notes.md.
    int maxLines         = 3;
    int charsWithIcon    = 12;
    int charsWithoutIcon = 16;

    std::chrono::milliseconds holdAfterEmpty{6000};

    // Display order. Widgets missing here are appended with their own defaults on load,
    // so a new version's widgets appear without anyone having to delete the file.
    std::vector<WidgetConfig> widgets;

    // Own buddy list, matched against CLIENT_UNIQUE_IDENTIFIER.
    //
    // TeamSpeak's Friend/Foe manager is internal to the client and is not exposed to
    // plugins at all, so "is this person a buddy" has to be answered from our own list.
    std::vector<std::string> buddies;

    bool isBuddy(const std::string& uniqueId) const;
};

// %APPDATA%\TS3Client\plugins\ts3_steelseries\config.json
std::filesystem::path configFilePath();

// Never fails: a missing, unreadable or malformed file yields defaults. A plugin that
// refuses to start over a broken config would be worse than one that ignores it.
Config loadConfig(const std::filesystem::path& file);

bool saveConfig(const std::filesystem::path& file, const Config& config);

// Fills in widgets the file does not mention and drops ids this build does not know.
// Kept separate from loadConfig so it can be tested without touching the disk.
void reconcileWithRegistry(Config& config);

}  // namespace ts3ss
