#pragma once

// Turns TeamSpeak callbacks into ClientState updates.
//
// Runs entirely on the TeamSpeak thread. Every id is resolved to a plain string HERE,
// so the worker thread never has to call back into the client - the invariant from
// docs/architecture.md.
//
// Also the single place where the active-server-tab filter lives
// (docs/decisions/0004-single-active-server-tab.md). Doing that check per callback
// would be the easiest thing in this design to forget in one of them, and the symptom
// - a background tab bleeding into the display - is hard to spot.

#include <functional>
#include <memory>

#include "config/config.h"
#include "core/state_store.h"
#include "plugin/ts3_context.h"

namespace ts3ss {

class StateSync {
public:
    using ConfigProvider = std::function<std::shared_ptr<const Config>()>;

    StateSync(Ts3Context& context, StateStore& store, ConfigProvider config,
              std::function<void()> onChanged)
        : ts3_(context), store_(store), config_(std::move(config)),
          onChanged_(std::move(onChanged)) {}

    // The foreground tab changed, or we want a clean re-read after connecting.
    void setActiveServer(uint64 schid);

    // Discards everything and re-reads from the client. Used on tab switch and once a
    // connection reaches STATUS_CONNECTION_ESTABLISHED, because incremental updates
    // cannot recover what happened while we were not looking.
    void rebuild();

    void onConnectStatusChanged(uint64 schid, int newStatus);
    void onTalkStatusChanged(uint64 schid, int status, bool receivedWhisper, anyID clientId);
    void onSelfVariableUpdated(uint64 schid, int flag);
    void onClientMoved(uint64 schid, anyID clientId, uint64 oldChannelId, uint64 newChannelId);
    void onChannelEdited(uint64 schid, uint64 channelId);

    // Another client's variables changed - mute, deaf or away. Needed because the
    // active/total count would otherwise only refresh when somebody moves.
    void onClientUpdated(uint64 schid, anyID clientId);
    void onPoked(uint64 schid, const char* fromName, const char* message);
    void onTextMessage(uint64 schid, anyID fromId, const char* fromName, const char* message);
    void onConnectionInfo(uint64 schid, anyID clientId);

    uint64 activeServer() const { return activeSchid_; }

private:
    // True when the event belongs to the tab we are showing. Everything else is dropped.
    bool isActive(uint64 schid) const { return schid != 0 && schid == activeSchid_; }

    void refreshSelfFlags(ClientState& state) const;
    void refreshChannel(ClientState& state) const;
    void refreshTalkers(ClientState& state) const;

    std::string nicknameOf(anyID clientId) const;
    bool        isBuddy(anyID clientId) const;

    // Connection quality is polled by piggybacking on incoming callbacks rather than
    // from a timer.
    //
    // The alternative would be the worker thread calling requestConnectionInfo, which
    // would break the "worker never touches the TeamSpeak API" invariant for one
    // number. The trade-off: when absolutely nothing happens on the server, ping stops
    // being refreshed - which is also when nobody is talking and it matters least.
    void maybeRequestConnectionInfo();

    void commit(const StateStore::Mutation& mutation);

    Ts3Context&           ts3_;
    StateStore&           store_;
    ConfigProvider        config_;
    std::function<void()> onChanged_;

    uint64    activeSchid_ = 0;
    anyID     ownClientId_ = 0;
    uint64    ownChannel_  = 0;
    Timestamp lastConnectionInfoRequest_{};
};

}  // namespace ts3ss
