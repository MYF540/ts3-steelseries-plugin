#pragma once

#include <string>

namespace ts3ss {

// Minimal synchronous JSON-over-HTTP POST client for the GameSense server on loopback.
//
// NOT thread-safe by design. Exactly one thread (the worker) may own an instance -
// that single-writer rule is also our mitigation for GameSense issue #66, where
// concurrent writers corrupt Arctis screens.
class HttpClient {
public:
    struct Response {
        bool         transportOk = false;  // false = could not talk to the server at all
        unsigned int status      = 0;      // HTTP status when transportOk
        std::string  body;

        bool ok() const { return transportOk && status >= 200 && status < 300; }
    };

    // hostPort as it appears in coreProps.json, e.g. "127.0.0.1:51248".
    explicit HttpClient(const std::string& hostPort);
    ~HttpClient();

    HttpClient(const HttpClient&)            = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    bool valid() const { return connect_ != nullptr; }

    // path without leading slash, e.g. "game_event".
    Response post(const std::string& path, const std::string& jsonBody);

private:
    void* session_ = nullptr;  // HINTERNET
    void* connect_ = nullptr;  // HINTERNET
};

}  // namespace ts3ss
