#include "util/log.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>

namespace ts3ss {
namespace {

std::mutex    g_mutex;
std::ofstream g_file;
LogSink       g_sink;
LogLevel      g_minimum = LogLevel::Info;

const char* levelName(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

std::string timestamp() {
    using namespace std::chrono;

    const auto now  = system_clock::now();
    const auto secs = system_clock::to_time_t(now);
    const auto ms   = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm{};
    localtime_s(&tm, &secs);

    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0') << std::setw(3)
        << ms.count();
    return out.str();
}

}  // namespace

void logInit(const std::filesystem::path& file) {
    std::lock_guard<std::mutex> lock(g_mutex);

    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);

    // Truncate on start. A plugin log is only ever read right after reproducing a
    // problem; an endlessly growing file would just have to be rotated later.
    g_file.open(file, std::ios::out | std::ios::trunc);
}

void logSetSink(LogSink sink) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sink = std::move(sink);
}

void logSetLevel(LogLevel minimum) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_minimum = minimum;
}

void logShutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file.is_open()) {
        g_file.flush();
        g_file.close();
    }
    // Dropped before the TeamSpeak function pointers go away, so nothing can call
    // back into a client that is already tearing itself down.
    g_sink = nullptr;
}

void logWrite(LogLevel level, const std::string& message) {
    if (message.empty())
        return;

    LogSink sinkCopy;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (level < g_minimum)
            return;

        if (g_file.is_open()) {
            g_file << timestamp() << " [" << levelName(level) << "] " << message << '\n';
            // Flushed every line on purpose: when this plugin takes the client down
            // with it, the last line before the crash is the interesting one.
            g_file.flush();
        }
        sinkCopy = g_sink;
    }

    // Called outside the lock. The sink reaches into the TeamSpeak client, and holding
    // our mutex across a foreign call is how deadlocks are made.
    if (sinkCopy) {
        try {
            sinkCopy(level, message);
        } catch (...) {
            // A failing log sink must never propagate - least of all out of a
            // LogStream destructor.
        }
    }
}

}  // namespace ts3ss
