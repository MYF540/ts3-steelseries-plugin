#include "plugin/state_sync.h"

#include <algorithm>

#include "teamspeak/public_rare_definitions.h"
#include "util/log.h"

namespace ts3ss {

void StateSync::commit(const StateStore::Mutation& mutation) {
    if (store_.apply(mutation) && onChanged_)
        onChanged_();
}

std::string StateSync::nicknameOf(anyID clientId) const {
    if (auto name = ts3_.clientString(activeSchid_, clientId, CLIENT_NICKNAME))
        return *name;
    return "?";
}

bool StateSync::isBuddy(anyID clientId) const {
    if (!config_)
        return false;

    const auto config = config_();
    if (!config || config->buddies.empty())
        return false;

    // Matched by unique identifier, not nickname: nicknames change, identities do not.
    const auto uid = ts3_.clientString(activeSchid_, clientId, CLIENT_UNIQUE_IDENTIFIER);
    return uid && config->isBuddy(*uid);
}

void StateSync::maybeRequestConnectionInfo() {
    if (activeSchid_ == 0 || ownClientId_ == 0)
        return;

    // Roughly the interval at which TeamSpeak itself refreshes these values; asking
    // more often yields the same numbers.
    constexpr std::chrono::seconds kInterval{5};

    const auto now = std::chrono::steady_clock::now();
    if (lastConnectionInfoRequest_.time_since_epoch().count() != 0
        && now - lastConnectionInfoRequest_ < kInterval) {
        return;
    }

    lastConnectionInfoRequest_ = now;
    ts3_.requestConnectionInfo(activeSchid_, ownClientId_);
}

void StateSync::onConnectionInfo(uint64 schid, anyID clientId) {
    if (!isActive(schid) || clientId != ownClientId_)
        return;

    const auto ping = ts3_.connectionUInt64(schid, clientId, CONNECTION_PING);
    const auto loss = ts3_.connectionDouble(schid, clientId, CONNECTION_PACKETLOSS_TOTAL);

    commit([&](ClientState& state) {
        if (ping)
            state.pingMs = static_cast<int>(*ping);
        if (loss)
            state.packetLoss = *loss;
    });
}

void StateSync::setActiveServer(uint64 schid) {
    if (schid == activeSchid_)
        return;

    TS3SS_DEBUG << "Active server tab -> " << schid;
    activeSchid_ = schid;
    ownClientId_ = 0;
    ownChannel_  = 0;

    // Wiping first matters: without it, talkers from the previous tab would linger
    // until they happen to stop talking - on a server we are no longer looking at.
    store_.reset();
    rebuild();
}

void StateSync::rebuild() {
    if (activeSchid_ == 0) {
        store_.reset();
        if (onChanged_)
            onChanged_();
        return;
    }

    const int status = ts3_.connectionStatus(activeSchid_).value_or(STATUS_DISCONNECTED);

    // Below STATUS_CONNECTION_ESTABLISHED there are no channels and no client list to
    // read - querying anyway just yields errors.
    if (status != STATUS_CONNECTION_ESTABLISHED) {
        const bool connecting = status != STATUS_DISCONNECTED;
        commit([connecting](ClientState& state) {
            state                     = ClientState{};
            state.connecting          = connecting;
            state.connectionChangedAt = std::chrono::steady_clock::now();
        });
        return;
    }

    ownClientId_ = ts3_.ownClientId(activeSchid_).value_or(0);
    ownChannel_  = ownClientId_ ? ts3_.channelOfClient(activeSchid_, ownClientId_).value_or(0) : 0;

    commit([this](ClientState& state) {
        const bool wasConnected = state.connected;

        state            = ClientState{};
        state.connected  = true;
        state.serverName = ts3_.serverString(activeSchid_, VIRTUALSERVER_NAME).value_or("");
        if (ownClientId_)
            state.ownNickname = nicknameOf(ownClientId_);

        // A rebuild after a tab switch is not a new connection, so it should not
        // announce "connected" on the display all over again.
        if (!wasConnected)
            state.connectionChangedAt = std::chrono::steady_clock::now();

        refreshSelfFlags(state);
        refreshChannel(state);
        refreshTalkers(state);
    });
}

void StateSync::refreshSelfFlags(ClientState& state) const {
    const bool input  = ts3_.selfInt(activeSchid_, CLIENT_INPUT_MUTED).value_or(0) != 0;
    const bool output = ts3_.selfInt(activeSchid_, CLIENT_OUTPUT_MUTED).value_or(0) != 0;
    // CLIENT_AWAY lives in ClientPropertiesRare, but that enum continues the numbering
    // of ClientProperties, so the same getter accepts it.
    const bool away = ts3_.selfInt(activeSchid_, CLIENT_AWAY).value_or(0) != 0;

    // Stamped only on an actual flip. The mute widget claims the screen for a few
    // seconds after a change and then falls silent (ADR 0007); stamping on every
    // refresh would make it claim the screen forever, which is the bug that ADR 0007
    // exists to fix.
    if (input != state.inputMuted || output != state.outputMuted || away != state.away)
        state.selfFlagsChangedAt = std::chrono::steady_clock::now();

    state.inputMuted  = input;
    state.outputMuted = output;
    state.away        = away;
}

void StateSync::refreshChannel(ClientState& state) const {
    if (ownChannel_ == 0) {
        state.channelName        = {};
        state.channelClientCount = 0;
        return;
    }

    const auto name = ts3_.channelString(activeSchid_, ownChannel_, CHANNEL_NAME).value_or("");

    // Only the name counts as a change worth showing. The member count moves whenever
    // anyone comes or goes, and that should not keep re-claiming the display.
    if (name != state.channelName)
        state.channelChangedAt = std::chrono::steady_clock::now();

    state.channelName        = name;
    state.channelClientCount = static_cast<int>(ts3_.channelClients(activeSchid_, ownChannel_).size());
}

void StateSync::refreshTalkers(ClientState& state) const {
    state.talkers.clear();
    if (ownChannel_ == 0)
        return;

    // A full rebuild has to ask who is talking right now; there is no event replay.
    for (anyID id : ts3_.channelClients(activeSchid_, ownChannel_)) {
        if (ts3_.clientInt(activeSchid_, id, CLIENT_FLAG_TALKING).value_or(0) == 0)
            continue;

        TalkerInfo talker;
        talker.name  = nicknameOf(id);
        talker.since = std::chrono::steady_clock::now();
        state.talkers.push_back(std::move(talker));
    }
}

void StateSync::onConnectStatusChanged(uint64 schid, int newStatus) {
    // Deliberately not filtered through isActive(): the very first connection has to be
    // able to claim the active tab, and at that moment activeSchid_ may still be 0.
    if (activeSchid_ == 0 && newStatus != STATUS_DISCONNECTED) {
        setActiveServer(schid);
        return;
    }

    if (!isActive(schid))
        return;

    if (newStatus == STATUS_CONNECTION_ESTABLISHED) {
        rebuild();
        return;
    }

    if (newStatus == STATUS_DISCONNECTED) {
        ownClientId_ = 0;
        ownChannel_  = 0;
        commit([](ClientState& state) { state = ClientState{}; });
        return;
    }

    commit([](ClientState& state) {
        state            = ClientState{};
        state.connecting = true;
    });
}

void StateSync::onTalkStatusChanged(uint64 schid, int status, bool receivedWhisper,
                                    anyID clientId) {
    if (!isActive(schid))
        return;

    maybeRequestConnectionInfo();

    // "Talking into a muted microphone" only ever refers to ourselves, and it is the
    // single most useful thing this display can report.
    if (clientId == ownClientId_ && status == STATUS_TALKING_WHILE_DISABLED) {
        commit([](ClientState& state) { state.talkingWhileMuted = true; });
        return;
    }
    if (clientId == ownClientId_ && status == STATUS_NOT_TALKING) {
        commit([](ClientState& state) { state.talkingWhileMuted = false; });
    }

    // Whispers reach us regardless of channel; normal talking only counts inside our own.
    if (!receivedWhisper) {
        const auto channel = ts3_.channelOfClient(schid, clientId);
        if (!channel || *channel != ownChannel_)
            return;
    }

    if (status == STATUS_TALKING) {
        const std::string name = nicknameOf(clientId);
        commit([&name, receivedWhisper](ClientState& state) {
            const auto it = std::find_if(state.talkers.begin(), state.talkers.end(),
                                         [&](const TalkerInfo& t) { return t.name == name; });
            if (it != state.talkers.end())
                return;

            TalkerInfo talker;
            talker.name       = name;
            talker.whispering = receivedWhisper;
            talker.since      = std::chrono::steady_clock::now();
            state.talkers.push_back(std::move(talker));
        });
        return;
    }

    const std::string name = nicknameOf(clientId);
    commit([&name](ClientState& state) {
        state.talkers.erase(std::remove_if(state.talkers.begin(), state.talkers.end(),
                                           [&](const TalkerInfo& t) { return t.name == name; }),
                            state.talkers.end());
    });
}

void StateSync::onSelfVariableUpdated(uint64 schid, int flag) {
    if (!isActive(schid))
        return;

    switch (flag) {
        case CLIENT_INPUT_MUTED:
        case CLIENT_OUTPUT_MUTED:
        case CLIENT_INPUT_HARDWARE:
        case CLIENT_OUTPUT_HARDWARE:
        case CLIENT_AWAY:
            break;
        default:
            // Self variables fire for plenty of things the display does not care about.
            return;
    }

    commit([this](ClientState& state) { refreshSelfFlags(state); });
}

void StateSync::onClientMoved(uint64 schid, anyID clientId, uint64 oldChannelId,
                              uint64 newChannelId) {
    if (!isActive(schid))
        return;

    maybeRequestConnectionInfo();

    // oldChannelId == 0 means the client did not move between channels - it appeared on
    // the server. That is a different event from walking into our channel, and the one
    // worth knowing about for people on the buddy list.
    if (oldChannelId == 0 && clientId != ownClientId_) {
        const std::string name  = nicknameOf(clientId);
        const bool        buddy = isBuddy(clientId);

        commit([&name, buddy](ClientState& state) {
            state.lastServerJoin.who   = name;
            state.lastServerJoin.buddy = buddy;
            state.lastServerJoin.at    = std::chrono::steady_clock::now();
        });
        // Falls through: they may have appeared directly in our channel.
    }

    // We moved: everything about the channel changes at once, so re-read rather than
    // patch. This also covers being dragged by someone else, which arrives through a
    // different callback but ends up here.
    if (clientId == ownClientId_) {
        ownChannel_ = newChannelId;
        commit([this](ClientState& state) {
            refreshChannel(state);
            refreshTalkers(state);
        });
        return;
    }

    // Somebody else came or went.
    if (oldChannelId != ownChannel_ && newChannelId != ownChannel_)
        return;

    const bool        joined = (newChannelId == ownChannel_);
    const std::string name   = nicknameOf(clientId);

    commit([this, joined, &name](ClientState& state) {
        refreshChannel(state);

        if (joined) {
            state.lastJoin.who = name;
            state.lastJoin.at  = std::chrono::steady_clock::now();
            return;
        }

        // They left: drop them from the talker list, which gets no stop event.
        state.talkers.erase(std::remove_if(state.talkers.begin(), state.talkers.end(),
                                           [&](const TalkerInfo& t) { return t.name == name; }),
                            state.talkers.end());
    });
}

void StateSync::onChannelEdited(uint64 schid, uint64 channelId) {
    if (!isActive(schid) || channelId != ownChannel_)
        return;

    commit([this](ClientState& state) { refreshChannel(state); });
}

void StateSync::onPoked(uint64 schid, const char* fromName, const char* message) {
    if (!isActive(schid))
        return;

    // Both strings belong to the client and are only valid for the duration of the
    // callback, so they are copied here rather than referenced.
    const std::string who  = fromName ? fromName : "?";
    const std::string text = message ? message : "";

    commit([&who, &text](ClientState& state) {
        state.lastPoke.who  = who;
        state.lastPoke.text = text;
        state.lastPoke.at   = std::chrono::steady_clock::now();
    });
}

void StateSync::onTextMessage(uint64 schid, anyID fromId, const char* fromName,
                              const char* message) {
    if (!isActive(schid))
        return;

    // Our own messages are not news to us.
    if (fromId == ownClientId_)
        return;

    const std::string who  = fromName ? fromName : "?";
    const std::string text = message ? message : "";
    if (text.empty())
        return;

    commit([&who, &text](ClientState& state) {
        state.lastMessage.who  = who;
        state.lastMessage.text = text;
        state.lastMessage.at   = std::chrono::steady_clock::now();
    });
}

}  // namespace ts3ss
