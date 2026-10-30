#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

// One request of the game's HTTP control API ("Launch options and automation"): a GET path with query parameters.
struct ControlRequest {
    std::string path;
    std::map<std::string, std::string, std::less<>> query;

    std::optional<std::string> text(std::string_view key) const;
    std::optional<int> integer(std::string_view key) const;
    // Ball counts pass 2^31 (the target PC holds about 7.1B), so they are read as 64-bit.
    std::optional<uint64_t> count(std::string_view key) const;
};

struct ControlResponse {
    int status = 200;
    std::string json = "{\"ok\":true}";

    static ControlResponse ok(std::string json = "{\"ok\":true}") { return {200, std::move(json)}; }
    static ControlResponse error(int status, std::string_view message);
};

std::string jsonEscape(std::string_view text);

// The request target of an HTTP request line ("/play?mode=normal&level=DA_FirstLevel"), URL-decoded.
ControlRequest parseControlTarget(std::string_view target);

// A complete HTTP/1.1 response carrying the JSON, for transports that write to a socket themselves.
std::string httpResponseText(const ControlResponse& response);
