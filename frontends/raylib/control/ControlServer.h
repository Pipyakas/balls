#pragma once

#include "control/ControlProtocol.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>

class ControlServer {
public:
    using Handler = std::function<ControlResponse(const ControlRequest&)>;

    ~ControlServer();

    bool start(int port);
    void stop();
    void pump(const Handler& handler);
    bool isRunning() const { return running; }

private:
    struct PendingCommand {
        ControlRequest request;
        std::promise<ControlResponse> response;
    };

    void acceptLoop();
    void serveClient(uintptr_t client);

    std::jthread listener;
    std::atomic<bool> running = false;
    uintptr_t listenSocket = ~uintptr_t{0};
    std::mutex queueMutex;
    std::deque<PendingCommand> queue;
};
