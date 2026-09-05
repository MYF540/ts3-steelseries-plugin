#include "plugin/ts3_context.h"

#include "teamspeak/public_errors.h"
#include "util/log.h"

namespace ts3ss {
namespace {

// Owns a char* that the TeamSpeak client allocated, and gives it back through
// ts3Functions.freeMemory. Forgetting this leaks on every single state update, which
// in a long session adds up quietly.
class OwnedString {
public:
    OwnedString(const struct TS3Functions& ts3, char* raw) : ts3_(ts3), raw_(raw) {}
    ~OwnedString() {
        if (raw_ && ts3_.freeMemory)
            ts3_.freeMemory(raw_);
    }

    OwnedString(const OwnedString&)            = delete;
    OwnedString& operator=(const OwnedString&) = delete;

    std::string copy() const { return raw_ ? std::string(raw_) : std::string{}; }

private:
    const struct TS3Functions& ts3_;
    char*                      raw_;
};

}  // namespace

uint64 Ts3Context::currentServerConnectionHandler() const {
    if (!ts3_.getCurrentServerConnectionHandlerID)
        return 0;
    return ts3_.getCurrentServerConnectionHandlerID();
}

std::optional<int> Ts3Context::connectionStatus(uint64 schid) const {
    if (!ts3_.getConnectionStatus || schid == 0)
        return std::nullopt;

    int status = 0;
    if (ts3_.getConnectionStatus(schid, &status) != ERROR_ok)
        return std::nullopt;
    return status;
}

std::optional<anyID> Ts3Context::ownClientId(uint64 schid) const {
    if (!ts3_.getClientID || schid == 0)
        return std::nullopt;

    anyID id = 0;
    if (ts3_.getClientID(schid, &id) != ERROR_ok)
        return std::nullopt;
    return id;
}

std::optional<uint64> Ts3Context::channelOfClient(uint64 schid, anyID clientId) const {
    if (!ts3_.getChannelOfClient || schid == 0)
        return std::nullopt;

    uint64 channelId = 0;
    if (ts3_.getChannelOfClient(schid, clientId, &channelId) != ERROR_ok)
        return std::nullopt;
    return channelId;
}

std::optional<std::string> Ts3Context::clientString(uint64 schid, anyID clientId,
                                                    size_t flag) const {
    if (!ts3_.getClientVariableAsString || schid == 0)
        return std::nullopt;

    char* raw = nullptr;
    if (ts3_.getClientVariableAsString(schid, clientId, flag, &raw) != ERROR_ok)
        return std::nullopt;

    return OwnedString(ts3_, raw).copy();
}

std::optional<int> Ts3Context::clientInt(uint64 schid, anyID clientId, size_t flag) const {
    if (!ts3_.getClientVariableAsInt || schid == 0)
        return std::nullopt;

    int value = 0;
    if (ts3_.getClientVariableAsInt(schid, clientId, flag, &value) != ERROR_ok)
        return std::nullopt;
    return value;
}

std::optional<std::string> Ts3Context::selfString(uint64 schid, size_t flag) const {
    if (!ts3_.getClientSelfVariableAsString || schid == 0)
        return std::nullopt;

    char* raw = nullptr;
    if (ts3_.getClientSelfVariableAsString(schid, flag, &raw) != ERROR_ok)
        return std::nullopt;

    return OwnedString(ts3_, raw).copy();
}

std::optional<int> Ts3Context::selfInt(uint64 schid, size_t flag) const {
    if (!ts3_.getClientSelfVariableAsInt || schid == 0)
        return std::nullopt;

    int value = 0;
    if (ts3_.getClientSelfVariableAsInt(schid, flag, &value) != ERROR_ok)
        return std::nullopt;
    return value;
}

std::optional<std::string> Ts3Context::channelString(uint64 schid, uint64 channelId,
                                                     size_t flag) const {
    if (!ts3_.getChannelVariableAsString || schid == 0 || channelId == 0)
        return std::nullopt;

    char* raw = nullptr;
    if (ts3_.getChannelVariableAsString(schid, channelId, flag, &raw) != ERROR_ok)
        return std::nullopt;

    return OwnedString(ts3_, raw).copy();
}

std::optional<std::string> Ts3Context::serverString(uint64 schid, size_t flag) const {
    if (!ts3_.getServerVariableAsString || schid == 0)
        return std::nullopt;

    char* raw = nullptr;
    if (ts3_.getServerVariableAsString(schid, flag, &raw) != ERROR_ok)
        return std::nullopt;

    return OwnedString(ts3_, raw).copy();
}

std::vector<anyID> Ts3Context::channelClients(uint64 schid, uint64 channelId) const {
    std::vector<anyID> clients;
    if (!ts3_.getChannelClientList || schid == 0 || channelId == 0)
        return clients;

    anyID* raw = nullptr;
    if (ts3_.getChannelClientList(schid, channelId, &raw) != ERROR_ok || !raw)
        return clients;

    // The list is zero-terminated, not length-prefixed.
    for (anyID* p = raw; *p != 0; ++p)
        clients.push_back(*p);

    if (ts3_.freeMemory)
        ts3_.freeMemory(raw);

    return clients;
}

void Ts3Context::requestConnectionInfo(uint64 schid, anyID clientId) const {
    if (ts3_.requestConnectionInfo && schid != 0)
        ts3_.requestConnectionInfo(schid, clientId, nullptr);
}

std::optional<uint64> Ts3Context::connectionUInt64(uint64 schid, anyID clientId,
                                                   size_t flag) const {
    if (!ts3_.getConnectionVariableAsUInt64 || schid == 0)
        return std::nullopt;

    uint64 value = 0;
    if (ts3_.getConnectionVariableAsUInt64(schid, clientId, flag, &value) != ERROR_ok)
        return std::nullopt;
    return value;
}

std::optional<double> Ts3Context::connectionDouble(uint64 schid, anyID clientId,
                                                   size_t flag) const {
    if (!ts3_.getConnectionVariableAsDouble || schid == 0)
        return std::nullopt;

    double value = 0.0;
    if (ts3_.getConnectionVariableAsDouble(schid, clientId, flag, &value) != ERROR_ok)
        return std::nullopt;
    return value;
}

void Ts3Context::log(const char* message) const {
    if (ts3_.logMessage)
        ts3_.logMessage(message, LogLevel_INFO, "ts3_steelseries", 0);
}

}  // namespace ts3ss
