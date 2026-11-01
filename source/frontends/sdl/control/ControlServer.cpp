#include "control/ControlServer.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <chrono>
#include <format>
#include <string>

namespace {

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket invalidSocket = INVALID_SOCKET;
using ReceiveTimeout = DWORD;
constexpr ReceiveTimeout receiveTimeout = 2000;

bool startSockets() {
    WSADATA data{};
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

void stopSockets() { WSACleanup(); }

void closeSocket(NativeSocket socket) { closesocket(socket); }

void wakeBlockedAccept(NativeSocket socket) { closesocket(socket); }
#else
using NativeSocket = int;
constexpr NativeSocket invalidSocket = -1;
using ReceiveTimeout = timeval;
constexpr ReceiveTimeout receiveTimeout{2, 0};

bool startSockets() { return true; }

void stopSockets() {}

void closeSocket(NativeSocket socket) { close(socket); }

void wakeBlockedAccept(NativeSocket socket) {
    shutdown(socket, SHUT_RDWR);
    close(socket);
}
#endif

constexpr auto responseTimeout = std::chrono::seconds(10);
constexpr size_t maximumRequestBytes = 16 * 1024;

void sendAll(NativeSocket socket, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const auto result = send(socket, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (result <= 0) {
            return;
        }
        sent += static_cast<size_t>(result);
    }
}

}

ControlServer::~ControlServer() { stop(); }

bool ControlServer::start(int port) {
    if (!startSockets()) {
        return false;
    }
    const NativeSocket socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketHandle == invalidSocket) {
        stopSockets();
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (bind(socketHandle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(socketHandle, SOMAXCONN) != 0) {
        closeSocket(socketHandle);
        stopSockets();
        return false;
    }
    listenSocket = static_cast<uintptr_t>(socketHandle);
    running = true;
    listener = std::thread([this] { acceptLoop(); });
    return true;
}

void ControlServer::stop() {
    if (!running.exchange(false)) {
        return;
    }
    wakeBlockedAccept(static_cast<NativeSocket>(listenSocket));
    if (listener.joinable()) {
        listener.join();
    }
    std::lock_guard lock(queueMutex);
    for (std::deque<PendingCommand>* pending : {&queue, &deferred}) {
        for (PendingCommand& command : *pending) {
            command.response.set_value(ControlResponse::error(503, "shutting down"));
        }
        pending->clear();
    }
    stopSockets();
}

void ControlServer::acceptLoop() {
    while (running) {
        const NativeSocket client = accept(static_cast<NativeSocket>(listenSocket), nullptr, nullptr);
        if (client == invalidSocket) {
            continue;
        }
        serveClient(static_cast<uintptr_t>(client));
        closeSocket(client);
    }
}

void ControlServer::serveClient(uintptr_t clientHandle) {
    const NativeSocket client = static_cast<NativeSocket>(clientHandle);
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&receiveTimeout), sizeof(receiveTimeout));
    std::string request;
    char buffer[2048];
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < maximumRequestBytes) {
        const auto received = recv(client, buffer, static_cast<int>(sizeof(buffer)), 0);
        if (received <= 0) {
            return;
        }
        request.append(buffer, static_cast<size_t>(received));
    }
    const size_t methodEnd = request.find(' ');
    const size_t targetEnd = request.find(' ', methodEnd + 1);
    if (methodEnd == std::string::npos || targetEnd == std::string::npos) {
        return;
    }
    PendingCommand command{parseControlTarget(std::string_view(request).substr(methodEnd + 1, targetEnd - methodEnd - 1)), {}};
    std::future<ControlResponse> future = command.response.get_future();
    {
        std::lock_guard lock(queueMutex);
        queue.push_back(std::move(command));
    }
    ControlResponse response = future.wait_for(responseTimeout) == std::future_status::ready
                                   ? future.get()
                                   : ControlResponse::error(504, "game did not answer in time");
    sendAll(client, httpResponseText(response));
}

void ControlServer::pump(const Handler& handler) {
    std::deque<PendingCommand> commands;
    {
        std::lock_guard lock(queueMutex);
        commands.swap(queue);
    }
    for (PendingCommand& command : commands) {
        if (std::optional<ControlResponse> response = handler(command.request)) {
            command.response.set_value(std::move(*response));
        } else {
            deferred.push_back(std::move(command));
        }
    }
}

void ControlServer::completeDeferred(const Completion& completion) {
    for (PendingCommand& command : deferred) {
        command.response.set_value(completion(command.request));
    }
    deferred.clear();
}
