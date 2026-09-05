#pragma once

#include <functional>
#include <mutex>

#include "core/client_state.h"

namespace ts3ss {

// The handover point between the TeamSpeak thread and our worker thread.
//
// TeamSpeak callbacks mutate through apply(); the worker reads through snapshot().
// Both take the lock only briefly - snapshot() copies and gets out, so the worker
// never renders or sends HTTP while holding it.
class StateStore {
public:
    using Mutation = std::function<void(ClientState&)>;

    // Runs the mutation under the lock and reports whether anything actually changed.
    // The comparison is what keeps a burst of TeamSpeak events from turning into a
    // burst of screen updates: three callbacks that describe the same situation wake
    // the worker once.
    bool apply(const Mutation& mutation) {
        std::lock_guard<std::mutex> lock(mutex_);

        const ClientState before = state_;
        mutation(state_);

        if (state_ == before)
            return false;

        ++version_;
        return true;
    }

    ClientState snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }

    unsigned long long version() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return version_;
    }

    // Used when switching server tabs: the old state must not bleed into the new one.
    // See docs/decisions/0004-single-active-server-tab.md.
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = ClientState{};
        ++version_;
    }

private:
    mutable std::mutex mutex_;
    ClientState        state_;
    unsigned long long version_ = 0;
};

}  // namespace ts3ss
