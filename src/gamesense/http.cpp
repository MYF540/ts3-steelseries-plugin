#include "gamesense/http.h"

#include <windows.h>
#include <winhttp.h>

#include <vector>

#include "util/log.h"
#include "util/win_paths.h"

namespace ts3ss {
namespace {

constexpr int kResolveTimeoutMs = 2000;
constexpr int kConnectTimeoutMs = 2000;
constexpr int kSendTimeoutMs    = 3000;
constexpr int kReceiveTimeoutMs = 3000;

struct HostPort {
    std::wstring   host;
    INTERNET_PORT  port = 0;
    bool           valid = false;
};

HostPort splitHostPort(const std::string& hostPort) {
    HostPort result;

    const auto colon = hostPort.rfind(':');
    if (colon == std::string::npos || colon + 1 >= hostPort.size())
        return result;

    const auto portText = hostPort.substr(colon + 1);
    unsigned long port  = 0;
    try {
        port = std::stoul(portText);
    } catch (...) {
        return result;
    }
    if (port == 0 || port > 65535)
        return result;

    result.host  = utf8ToWide(hostPort.substr(0, colon));
    result.port  = static_cast<INTERNET_PORT>(port);
    result.valid = !result.host.empty();
    return result;
}

}  // namespace

HttpClient::HttpClient(const std::string& hostPort) {
    const auto target = splitHostPort(hostPort);
    if (!target.valid) {
        TS3SS_ERROR << "Cannot parse GameSense address: " << hostPort;
        return;
    }

    // WINHTTP_ACCESS_TYPE_NO_PROXY matters: with a system proxy configured, WinHTTP
    // would otherwise try to route even 127.0.0.1 through it and every request fails.
    session_ = WinHttpOpen(L"ts3-steelseries-plugin/0.1", WINHTTP_ACCESS_TYPE_NO_PROXY,
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session_) {
        TS3SS_ERROR << "WinHttpOpen failed, error " << GetLastError();
        return;
    }

    WinHttpSetTimeouts(session_, kResolveTimeoutMs, kConnectTimeoutMs, kSendTimeoutMs,
                       kReceiveTimeoutMs);

    connect_ = WinHttpConnect(session_, target.host.c_str(), target.port, 0);
    if (!connect_) {
        TS3SS_ERROR << "WinHttpConnect to " << hostPort << " failed, error " << GetLastError();
        WinHttpCloseHandle(session_);
        session_ = nullptr;
    }
}

HttpClient::~HttpClient() {
    if (connect_)
        WinHttpCloseHandle(connect_);
    if (session_)
        WinHttpCloseHandle(session_);
}

HttpClient::Response HttpClient::post(const std::string& path, const std::string& jsonBody) {
    Response response;
    if (!connect_)
        return response;

    const std::wstring wpath = L"/" + utf8ToWide(path);

    HINTERNET request = WinHttpOpenRequest(connect_, L"POST", wpath.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!request) {
        TS3SS_WARN << "WinHttpOpenRequest(" << path << ") failed, error " << GetLastError();
        return response;
    }

    static const wchar_t kHeaders[] = L"Content-Type: application/json\r\n";

    BOOL sent = WinHttpSendRequest(request, kHeaders, static_cast<DWORD>(-1),
                                   const_cast<char*>(jsonBody.data()),
                                   static_cast<DWORD>(jsonBody.size()),
                                   static_cast<DWORD>(jsonBody.size()), 0);
    if (!sent || !WinHttpReceiveResponse(request, nullptr)) {
        TS3SS_WARN << "POST /" << path << " failed, error " << GetLastError();
        WinHttpCloseHandle(request);
        return response;
    }

    response.transportOk = true;

    DWORD status     = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
    response.status = status;

    // GameSense answers are small (an echo of the request, or a short error), but read
    // the full body anyway - the error text is what makes a rejected handler debuggable.
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available) || available == 0)
            break;

        std::vector<char> chunk(available);
        DWORD             read = 0;
        if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0)
            break;

        response.body.append(chunk.data(), read);
    }

    WinHttpCloseHandle(request);
    return response;
}

}  // namespace ts3ss
