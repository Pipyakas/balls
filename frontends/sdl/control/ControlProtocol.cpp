#include "control/ControlProtocol.h"

#include <charconv>
#include <format>

std::optional<std::string> ControlRequest::text(std::string_view key) const {
    const auto it = query.find(key);
    return it == query.end() ? std::nullopt : std::optional<std::string>(it->second);
}

std::optional<int> ControlRequest::integer(std::string_view key) const {
    const auto value = text(key);
    if (!value) {
        return std::nullopt;
    }
    int result = 0;
    const auto [end, error] = std::from_chars(value->data(), value->data() + value->size(), result);
    if (error != std::errc{} || end != value->data() + value->size()) {
        return std::nullopt;
    }
    return result;
}

ControlResponse ControlResponse::error(int status, std::string_view message) {
    return {status, std::format("{{\"ok\":false,\"error\":\"{}\"}}", jsonEscape(message))};
}

std::string jsonEscape(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (const char character : text) {
        switch (character) {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(character) < 0x20) {
                escaped += std::format("\\u{:04x}", static_cast<int>(character));
            } else {
                escaped += character;
            }
        }
    }
    return escaped;
}
