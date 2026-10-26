#include "control/ControlServer.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <format>
#include <string>

namespace {

constexpr auto responseTimeout = std::chrono::seconds(10);
constexpr size_t maximumRequestBytes = 16 * 1024;
constexpr DWORD receiveTimeoutMs = 2000;

int hexValue(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

std::string urlDecode(std::string_view text) {
    std::string decoded;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '+') {
            decoded += ' ';
        } else if (text[i] == '%' && i + 2 < text.size() && hexValue(text[i + 1]) >= 0 && hexValue(text[i + 2]) >= 0) {
            decoded += static_cast<char>(hexValue(text[i + 1]) * 16 + hexValue(text[i + 2]));
            i += 2;
        } else {
            decoded += text[i];
        }
    }
    return decoded;
}

ControlRequest parseTarget(std::string_view target) {
    ControlRequest request;
    const size_t queryStart = target.find('?');
    request.path = urlDecode(target.substr(0, queryStart));
    if (queryStart == std::string_view::npos) {
        return request;
    }
    std::string_view query = target.substr(queryStart + 1);
    while (!query.empty()) {
        const size_t separator = query.find('&');
        const std::string_view pair = query.substr(0, separator);
        const size_t equals = pair.find('=');
        request.query[urlDecode(pair.substr(0, equals))] =
            equals == std::string_view::npos ? std::string() : urlDecode(pair.substr(equals + 1));
        query = separator == std::string_view::npos ? std::string_view() : query.substr(separator + 1);
    }
    return request;
}

std::string_view statusText(int status) {
    switch (status) {
    case 200:
        return "OK";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 409:
        return "Conflict";
    default:
        return "Error";
    }
}

void sendAll(SOCKET socket, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const int result = send(socket, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (result <= 0) {
            return;
        }
        sent += static_cast<size_t>(result);
    }
}

}

ControlServer::~ControlServer() { stop(); }

bool ControlServer::start(int port) {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return false;
    }
    const SOCKET socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketHandle == INVALID_SOCKET) {
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<u_short>(port));
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (bind(socketHandle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(socketHandle, SOMAXCONN) != 0) {
        closesocket(socketHandle);
        return false;
    }
    listenSocket = static_cast<uintptr_t>(socketHandle);
    running = true;
    listener = std::jthread([this] { acceptLoop(); });
    return true;
}

void ControlServer::stop() {
    if (!running.exchange(false)) {
        return;
    }
    closesocket(static_cast<SOCKET>(listenSocket));
    if (listener.joinable()) {
        listener.join();
    }
    std::lock_guard lock(queueMutex);
    for (PendingCommand& command : queue) {
        command.response.set_value(ControlResponse::error(503, "shutting down"));
    }
    queue.clear();
    WSACleanup();
}

void ControlServer::acceptLoop() {
    while (running) {
        const SOCKET client = accept(static_cast<SOCKET>(listenSocket), nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            continue;
        }
        serveClient(static_cast<uintptr_t>(client));
        closesocket(client);
    }
}

void ControlServer::serveClient(uintptr_t clientHandle) {
    const SOCKET client = static_cast<SOCKET>(clientHandle);
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&receiveTimeoutMs),
               sizeof(receiveTimeoutMs));
    std::string request;
    char buffer[2048];
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < maximumRequestBytes) {
        const int received = recv(client, buffer, static_cast<int>(sizeof(buffer)), 0);
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
    PendingCommand command{parseTarget(std::string_view(request).substr(methodEnd + 1, targetEnd - methodEnd - 1)), {}};
    std::future<ControlResponse> future = command.response.get_future();
    {
        std::lock_guard lock(queueMutex);
        queue.push_back(std::move(command));
    }
    ControlResponse response = future.wait_for(responseTimeout) == std::future_status::ready
                                   ? future.get()
                                   : ControlResponse::error(504, "game did not answer in time");
    sendAll(client, std::format("HTTP/1.1 {} {}\r\nContent-Type: application/json\r\nContent-Length: {}\r\n"
                                "Connection: close\r\n\r\n{}",
                                response.status, statusText(response.status), response.json.size(), response.json));
}

void ControlServer::pump(const Handler& handler) {
    std::deque<PendingCommand> commands;
    {
        std::lock_guard lock(queueMutex);
        commands.swap(queue);
    }
    for (PendingCommand& command : commands) {
        command.response.set_value(handler(command.request));
    }
}
