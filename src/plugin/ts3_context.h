#pragma once

// Every call into the TeamSpeak API goes through here.
//
// Two reasons for a single choke point:
//   1. Strings from get*VariableAsString are allocated by the client and must be handed
//      back to ts3Functions.freeMemory. One RAII wrapper beats sprinkling free calls.
//   2. All of it runs on the TeamSpeak thread. Keeping the calls in one place makes it
//      obvious that the worker thread never touches the API - the invariant from
//      docs/architecture.md.

#include <optional>
#include <string>
#include <vector>

#include "teamspeak/public_definitions.h"
#include "ts3_functions.h"

namespace ts3ss {

class Ts3Context {
public:
    void setFunctions(const struct TS3Functions& functions) { ts3_ = functions; }
    bool ready() const { return ts3_.getClientID != nullptr; }

    uint64 currentServerConnectionHandler() const;

    // One of enum ConnectStatus. Only STATUS_CONNECTION_ESTABLISHED guarantees that
    // channels and clients can be queried.
    std::optional<int> connectionStatus(uint64 schid) const;

    std::optional<anyID>  ownClientId(uint64 schid) const;
    std::optional<uint64> channelOfClient(uint64 schid, anyID clientId) const;

    std::optional<std::string> clientString(uint64 schid, anyID clientId, size_t flag) const;
    std::optional<int>         clientInt(uint64 schid, anyID clientId, size_t flag) const;

    std::optional<std::string> selfString(uint64 schid, size_t flag) const;
    std::optional<int>         selfInt(uint64 schid, size_t flag) const;

    std::optional<std::string> channelString(uint64 schid, uint64 channelId, size_t flag) const;
    std::optional<std::string> serverString(uint64 schid, size_t flag) const;

    std::vector<anyID> channelClients(uint64 schid, uint64 channelId) const;

    // Connection quality is measured asynchronously: ask here, read the values in
    // onConnectionInfoEvent once the client has them.
    void requestConnectionInfo(uint64 schid, anyID clientId) const;

    std::optional<uint64> connectionUInt64(uint64 schid, anyID clientId, size_t flag) const;
    std::optional<double> connectionDouble(uint64 schid, anyID clientId, size_t flag) const;

    void log(const char* message) const;

private:
    struct TS3Functions ts3_ {};
};

}  // namespace ts3ss
