#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace ts3ss {

using Timestamp = std::chrono::steady_clock::time_point;

// Someone audible in our own channel right now.
//
// Names are resolved on the TeamSpeak thread and stored as plain strings. The worker
// therefore never has to look an id back up - see the invariant in docs/architecture.md.
struct TalkerInfo {
    std::string name;
    bool        whispering = false;
    Timestamp   since;

    friend bool operator==(const TalkerInfo& a, const TalkerInfo& b) {
        return a.name == b.name && a.whispering == b.whispering;
    }
};

// A thing that just happened: a poke, a chat message, someone joining the channel.
//
// Carries its own timestamp because widgets decide by age whether it is still worth
// showing. `at` is part of equality on purpose - being poked twice by the same person
// with the same text must register as a new event, and only the timestamp differs.
struct Notice {
    std::string who;
    std::string text;
    Timestamp   at{};

    // Whether `who` is on the user's own buddy list. Resolved on the TeamSpeak thread
    // because it needs CLIENT_UNIQUE_IDENTIFIER, which the worker must not go looking
    // for. TeamSpeak's own Friend/Foe list is client-internal and not exposed to
    // plugins, so this comes from our config.
    bool buddy = false;

    bool valid() const { return at.time_since_epoch().count() != 0; }

    friend bool operator==(const Notice& a, const Notice& b) {
        return a.who == b.who && a.text == b.text && a.at == b.at && a.buddy == b.buddy;
    }
};

// Everything the display could possibly want to know, as a plain value type: copyable,
// comparable, and free of any TeamSpeak types. That is what lets widgets be unit-tested
// without a running client.
//
// Holds exactly one server connection - the tab currently in the foreground.
// See docs/decisions/0004-single-active-server-tab.md.
struct ClientState {
    // Connection
    bool        connected  = false;
    bool        connecting = false;
    std::string serverName;

    // Own channel
    std::string channelName;
    int         channelClientCount = 0;

    // Of those, the ones who can actually take part: neither microphone-muted nor
    // deafened. "3/7" says something "7" does not - how many people could answer you.
    int channelActiveCount = 0;

    // Own status
    std::string ownNickname;
    bool        inputMuted  = false;  // microphone muted
    bool        outputMuted = false;  // speakers muted (implies microphone muted)
    bool        away        = false;

    // True while we are talking into a muted microphone. TeamSpeak reports this
    // separately (STATUS_TALKING_WHILE_DISABLED) and it is the single most useful
    // thing this display can tell anyone.
    bool talkingWhileMuted = false;

    // Who is audible in our own channel
    std::vector<TalkerInfo> talkers;

    // Connection quality. -1 means "not measured yet" - distinct from a genuinely
    // excellent connection, which matters because the widget must stay silent while
    // nothing is known rather than claim everything is fine.
    int    pingMs     = -1;
    double packetLoss = -1.0;

    // Recent events
    Notice lastPoke;
    Notice lastMessage;
    Notice lastJoin;        // entered our channel
    Notice lastServerJoin;  // connected to the server at all

    // When the corresponding persistent values last changed. Widgets use these to show
    // a state briefly after it flips and then fall silent, instead of holding the
    // display forever - see docs/decisions/0007-transient-vs-persistent.md.
    Timestamp selfFlagsChangedAt{};
    Timestamp channelChangedAt{};
    Timestamp connectionChangedAt{};

    // Timestamps above are intentionally NOT compared. They only ever change together
    // with the value they describe, and including them would make an unchanged state
    // look modified after every rebuild.
    friend bool operator==(const ClientState& a, const ClientState& b) {
        return a.connected == b.connected && a.connecting == b.connecting
            && a.serverName == b.serverName && a.channelName == b.channelName
            && a.channelClientCount == b.channelClientCount
            && a.channelActiveCount == b.channelActiveCount && a.ownNickname == b.ownNickname
            && a.inputMuted == b.inputMuted && a.outputMuted == b.outputMuted
            && a.away == b.away && a.talkingWhileMuted == b.talkingWhileMuted
            && a.talkers == b.talkers && a.lastPoke == b.lastPoke
            && a.lastMessage == b.lastMessage && a.lastJoin == b.lastJoin
            && a.lastServerJoin == b.lastServerJoin && a.pingMs == b.pingMs
            && a.packetLoss == b.packetLoss;
    }
    friend bool operator!=(const ClientState& a, const ClientState& b) { return !(a == b); }
};

}  // namespace ts3ss
