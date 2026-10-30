#include "control/ControlProtocol.h"

#include <charconv>
#include <format>

namespace {

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

} // namespace

std::optional<std::string> ControlRequest::text(std::string_view key) const {
    const auto it = query.find(key);
    return it == query.end() ? std::nullopt : std::optional<std::string>(it->second);
}

template <typename Number>
std::optional<Number> parseNumber(const std::optional<std::string>& value) {
    if (!value) {
        return std::nullopt;
    }
    Number result = 0;
    const auto [end, error] = std::from_chars(value->data(), value->data() + value->size(), result);
    if (error != std::errc{} || end != value->data() + value->size()) {
        return std::nullopt;
    }
    return result;
}

std::optional<int> ControlRequest::integer(std::string_view key) const { return parseNumber<int>(text(key)); }

std::optional<uint64_t> ControlRequest::count(std::string_view key) const { return parseNumber<uint64_t>(text(key)); }

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

ControlRequest parseControlTarget(std::string_view target) {
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

std::string httpResponseText(const ControlResponse& response) {
    return std::format("HTTP/1.1 {} {}\r\nContent-Type: application/json\r\nContent-Length: {}\r\n"
                       "Connection: close\r\n\r\n{}",
                       response.status, statusText(response.status), response.json.size(), response.json);
}
