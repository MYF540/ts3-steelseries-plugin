#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

#include "gamesense/session.h"
#include "render/frame.h"

namespace ts3ss {

struct WorkerConfig {
    SessionConfig session;

    // Floor between two game_event requests. Bursts of TeamSpeak events are coalesced
    // into a single frame - a mitigation for GameSense issue #66, not an optimisation.
    std::chrono::milliseconds minUpdateInterval{120};

    // How long the display stays up after the last widget went quiet.
    std::chrono::milliseconds holdAfterEmpty{6000};

    // Comfortably inside GG's 15 s deinitialize timer.
    std::chrono::milliseconds heartbeatInterval{8000};

    // Retry pacing while SteelSeries GG is not running.
    std::chrono::milliseconds reconnectMin{10000};
    std::chrono::milliseconds reconnectMax{60000};
};

// Owns the only thread allowed to talk to GameSense.
//
// TeamSpeak callbacks must never block, so they only update state and call notify();
// everything slow happens here. This thread never calls back into the TeamSpeak API.
class Worker {
public:
    // Called on the worker thread to ask "what should be on screen right now?".
    // An empty frame means: release the display.
    using FrameProvider = std::function<Frame()>;

    Worker(WorkerConfig config, FrameProvider provider);
    ~Worker();

    Worker(const Worker&)            = delete;
    Worker& operator=(const Worker&) = delete;

    void start();

    // Releases the screen and joins the thread. Safe to call more than once.
    void stop();

    // Wakes the worker after a state change. Cheap enough for any callback.
    void notifyChanged();

private:
    void run();
    void tick();

    WorkerConfig  config_;
    FrameProvider provider_;
    Session       session_;

    std::thread             thread_;
    std::mutex              mutex_;
    std::condition_variable wake_;
    bool                    running_ = false;
    bool                    dirty_   = true;

    // Worker-thread-only state; no locking needed.
    Frame                                 lastSent_;
    std::chrono::steady_clock::time_point lastSendAt_{};
    std::chrono::steady_clock::time_point lastHeartbeatAt_{};
    std::chrono::steady_clock::time_point emptySince_{};
    std::chrono::steady_clock::time_point nextConnectAttempt_{};
    std::chrono::milliseconds             reconnectDelay_{0};
};

}  // namespace ts3ss
