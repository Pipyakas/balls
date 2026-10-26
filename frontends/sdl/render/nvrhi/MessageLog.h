#pragma once

#include <nvrhi/nvrhi.h>

#include <SDL3/SDL_log.h>

class MessageLog final : public nvrhi::IMessageCallback {
public:
    void message(nvrhi::MessageSeverity severity, const char* text) override {
        if (severity == nvrhi::MessageSeverity::Info) {
            return;
        }
        SDL_Log("nvrhi: %s", text);
    }

    static MessageLog& instance() {
        static MessageLog log;
        return log;
    }
};
