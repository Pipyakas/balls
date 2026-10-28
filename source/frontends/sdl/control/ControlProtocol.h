#pragma once

#include <map>
#include <optional>
#include <string>

struct ControlRequest {
    std::string path;
    std::map<std::string, std::string, std::less<>> query;

    std::optional<std::string> text(std::string_view key) const;
    std::optional<int> integer(std::string_view key) const;
    // Ball counts reach the billions, past what integer() holds.
    std::optional<unsigned long long> count(std::string_view key) const;
};

struct ControlResponse {
    int status = 200;
    std::string json = "{\"ok\":true}";

    static ControlResponse ok(std::string json) { return {200, std::move(json)}; }
    static ControlResponse error(int status, std::string_view message);
};

std::string jsonEscape(std::string_view text);
