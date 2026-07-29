#pragma once

#include <cstdint>

namespace heatpump
{
    class NetBusArbiter
    {
    public:
        enum class State : uint8_t { Idle, Receiving, Transmitting };

        explicit NetBusArbiter(uint32_t minimumCommandIntervalMs);
        bool beginReceive();
        void endReceive();
        bool beginTransmit(uint32_t nowMs);
        void endTransmit(uint32_t nowMs);

    private:
        uint32_t minimumCommandIntervalMs_;
        uint32_t lastTransmitMs_ = 0U;
        bool hasTransmitted_ = false;
        State state_ = State::Idle;
    };
}
