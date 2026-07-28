#pragma once

#include <cstdint>

namespace heatpump
{
    class BridgeWatchdog
    {
    public:
        struct Transition
        {
            bool requestSafetyStop = false;
            bool reconnected = false;
        };

        explicit BridgeWatchdog(uint32_t timeoutMs);
        void start(uint32_t nowMs);
        Transition update(uint32_t nowMs, uint32_t lastContactMs);
        bool online() const;

    private:
        uint32_t timeoutMs_;
        uint32_t startedAtMs_ = 0U;
        uint32_t lastSafetyRequestMs_ = 0U;
        bool online_ = false;
    };
}
