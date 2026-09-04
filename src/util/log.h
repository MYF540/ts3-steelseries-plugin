#pragma once

#include <filesystem>
#include <functional>
#include <sstream>
#include <string>

namespace ts3ss {

enum class LogLevel { Debug, Info, Warn, Error };

// The TeamSpeak client log is reached through a sink that the ABI layer installs.
// That indirection is what keeps everything below src/plugin/ free of the TeamSpeak
// headers, and therefore testable without a running client.
using LogSink = std::function<void(LogLevel, const std::string&)>;

void logInit(const std::filesystem::path& file);
void logSetSink(LogSink sink);
void logSetLevel(LogLevel minimum);
void logShutdown();
void logWrite(LogLevel level, const std::string& message);

// Accumulates into a buffer and emits once on destruction, so a single log line
// cannot be torn apart by another thread writing between two operator<< calls.
class LogStream {
public:
    explicit LogStream(LogLevel level) : level_(level) {}
    ~LogStream() { logWrite(level_, buffer_.str()); }

    LogStream(const LogStream&) = delete;
    LogStream& operator=(const LogStream&) = delete;

    template <typename T>
    LogStream& operator<<(const T& value) {
        buffer_ << value;
        return *this;
    }

private:
    LogLevel           level_;
    std::ostringstream buffer_;
};

}  // namespace ts3ss

#define TS3SS_DEBUG ::ts3ss::LogStream(::ts3ss::LogLevel::Debug)
#define TS3SS_INFO  ::ts3ss::LogStream(::ts3ss::LogLevel::Info)
#define TS3SS_WARN  ::ts3ss::LogStream(::ts3ss::LogLevel::Warn)
#define TS3SS_ERROR ::ts3ss::LogStream(::ts3ss::LogLevel::Error)
