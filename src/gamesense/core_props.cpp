#include "gamesense/core_props.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

#include "util/log.h"
#include "util/win_paths.h"

namespace ts3ss {
namespace {

// Two locations exist in the wild. The "SteelSeries Engine 3" path is the one the SDK
// documents; newer GG installs also write a copy under "GG". Both were present on the
// development machine, so try the documented one first and fall back.
const wchar_t* const kCandidates[] = {
    L"SteelSeries\\SteelSeries Engine 3\\coreProps.json",
    L"SteelSeries\\GG\\coreProps.json",
};

std::optional<std::string> readAddressFrom(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in)
        return std::nullopt;

    std::ostringstream contents;
    contents << in.rdbuf();

    const auto parsed = nlohmann::json::parse(contents.str(), nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded()) {
        TS3SS_WARN << "coreProps.json is not valid JSON: " << file.string();
        return std::nullopt;
    }

    const auto address = parsed.value("address", std::string{});
    if (address.empty()) {
        TS3SS_WARN << "coreProps.json has no 'address' key: " << file.string();
        return std::nullopt;
    }

    return address;
}

}  // namespace

std::optional<std::string> findGameSenseAddress() {
    const auto programData = programDataDir();
    if (programData.empty()) {
        TS3SS_WARN << "%ProgramData% could not be resolved";
        return std::nullopt;
    }

    for (const wchar_t* relative : kCandidates) {
        const auto file = programData / relative;
        if (auto address = readAddressFrom(file)) {
            TS3SS_INFO << "GameSense at " << *address << " (from " << file.string() << ")";
            return address;
        }
    }

    // Reached whenever SteelSeries GG is simply not running. Deliberately not an error:
    // the worker retries with backoff and picks GG up as soon as it starts.
    TS3SS_DEBUG << "coreProps.json not found - SteelSeries GG does not appear to be running";
    return std::nullopt;
}

}  // namespace ts3ss
