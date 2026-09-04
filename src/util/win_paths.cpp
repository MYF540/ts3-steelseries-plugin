#include "util/win_paths.h"

#include <windows.h>

#include <vector>

namespace ts3ss {
namespace {

std::filesystem::path envPath(const wchar_t* name) {
    // Two-call pattern: the first tells us the size including the terminator.
    const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0)
        return {};

    std::vector<wchar_t> buffer(needed);
    const DWORD written = GetEnvironmentVariableW(name, buffer.data(), needed);
    if (written == 0 || written >= needed)
        return {};

    return std::filesystem::path(std::wstring(buffer.data(), written));
}

}  // namespace

std::wstring utf8ToWide(std::string_view utf8) {
    if (utf8.empty())
        return {};

    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                         nullptr, 0);
    if (size <= 0)
        return {};

    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), size);
    return out;
}

std::string wideToUtf8(std::wstring_view wide) {
    if (wide.empty())
        return {};

    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};

    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), size,
                        nullptr, nullptr);
    return out;
}

std::filesystem::path pluginDataDir() {
    const auto appData = envPath(L"APPDATA");
    if (appData.empty())
        return {};

    return appData / L"TS3Client" / L"plugins" / L"ts3_steelseries";
}

std::filesystem::path programDataDir() {
    // Named "ProgramData" in the environment, not "PROGRAMDATA" - the lookup is
    // case-insensitive, but the documented spelling is the safer one to pass.
    return envPath(L"ProgramData");
}

}  // namespace ts3ss
