#include "core/worker.h"

#include <algorithm>

#include "util/log.h"

namespace ts3ss {

// Named with a capital C: a lowercase "clock" would sit next to ::clock from <ctime>.
using Clock = std::chrono::steady_clock;

Worker::Worker(WorkerConfig config, FrameProvider provider)
    : config_(std::move(config)), provider_(std::move(provider)), session_(config_.session) {}

Worker::~Worker() { stop(); }

void Worker::start() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_)
            return;
        running_ = true;
        dirty_   = true;
    }
    thread_ = std::thread(&Worker::run, this);
}

void Worker::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_)
            return;
        running_ = false;
    }
    wake_.notify_all();

    if (thread_.joinable())
        thread_.join();
}

void Worker::notifyChanged() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        dirty_ = true;
    }
    wake_.notify_one();
}

void Worker::run() {
    TS3SS_INFO << "Worker started";

    for (;;) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            // Timed wait even without changes: heartbeats and the hold timer have to
            // fire on their own.
            wake_.wait_for(lock, config_.minUpdateInterval, [this] { return !running_ || dirty_; });

            if (!running_)
                break;
            dirty_ = false;
        }

        try {
            tick();
        } catch (const std::exception& e) {
            TS3SS_ERROR << "Worker tick threw: " << e.what();
        } catch (...) {
            TS3SS_ERROR << "Worker tick threw an unknown exception";
        }
    }

    // Leaving the display frozen on our last frame would be the worst possible exit -
    // nothing else hands it back until the user touches the base station.
    session_.release();
    session_.disconnect();

    TS3SS_INFO << "Worker stopped";
}

void Worker::tick() {
    const auto now   = Clock::now();
    const Frame frame = provider_ ? provider_() : Frame{};

    // --- Nothing to show: hold briefly, then give the screen back -------------
    if (frame.empty()) {
        if (emptySince_.time_since_epoch().count() == 0)
            emptySince_ = now;

        if (session_.owned()) {
            if (now - emptySince_ >= config_.holdAfterEmpty) {
                session_.release();
            } else if (now - lastHeartbeatAt_ >= config_.heartbeatInterval) {
                session_.heartbeat();
                lastHeartbeatAt_ = now;
            }
        }
        return;
    }

    emptySince_ = {};

    // --- Something to show: make sure we have a connection --------------------
    if (!session_.connected()) {
        if (now < nextConnectAttempt_)
            return;

        if (!session_.connect()) {
            // Exponential-ish backoff so a permanently absent GG costs us almost nothing.
            reconnectDelay_ = reconnectDelay_.count() == 0
                                  ? config_.reconnectMin
                                  : std::min(reconnectDelay_ * 2, config_.reconnectMax);
            nextConnectAttempt_ = now + reconnectDelay_;
            return;
        }

        reconnectDelay_ = std::chrono::milliseconds{0};
        // Force a send: the frame may be unchanged, but the new session has never seen it.
        lastSent_ = Frame{};
    }

    // --- Send only what is new, and not too often -----------------------------
    const bool changed = frame != lastSent_;

    if (changed && now - lastSendAt_ >= config_.minUpdateInterval) {
        if (session_.show(frame)) {
            lastSent_        = frame;
            lastSendAt_      = now;
            lastHeartbeatAt_ = now;  // a frame counts as activity
        }
        return;
    }

    if (!changed && session_.owned() && now - lastHeartbeatAt_ >= config_.heartbeatInterval) {
        session_.heartbeat();
        lastHeartbeatAt_ = now;
    }
}

}  // namespace ts3ss
