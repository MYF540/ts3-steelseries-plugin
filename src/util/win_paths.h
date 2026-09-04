#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace ts3ss {

// UTF-8 <-> UTF-16. The TeamSpeak API and GameSense JSON are both UTF-8, so these are
// only needed at the Win32 boundary (paths, WinHTTP, dialogs) - never in between.
std::wstring utf8ToWide(std::string_view utf8);
std::string  wideToUtf8(std::wstring_view wide);

// %APPDATA%\TS3Client\plugins\ts3_steelseries
// Config and log live beside the plugin so they travel with a TeamSpeak profile.
std::filesystem::path pluginDataDir();

// %PROGRAMDATA%
std::filesystem::path programDataDir();

}  // namespace ts3ss
