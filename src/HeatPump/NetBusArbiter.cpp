#include "HeatPump/NetBusArbiter.hpp"

namespace heatpump
{
    NetBusArbiter::NetBusArbiter(const uint32_t minimumCommandIntervalMs) : minimumCommandIntervalMs_(minimumCommandIntervalMs) {}

    bool NetBusArbiter::beginReceive()
    {
        if (state_ != State::Idle) return false;
        state_ = State::Receiving;
        return true;
    }

    void NetBusArbiter::endReceive()
    {
        if (state_ == State::Receiving) state_ = State::Idle;
    }

    bool NetBusArbiter::beginTransmit(const uint32_t nowMs)
    {
        if (state_ != State::Idle) return false;
        if (hasTransmitted_ && static_cast<uint32_t>(nowMs - lastTransmitMs_) < minimumCommandIntervalMs_)
        {
            return false;
        }
        state_ = State::Transmitting;
        return true;
    }

    void NetBusArbiter::endTransmit(const uint32_t nowMs)
    {
        if (state_ != State::Transmitting) return;
        lastTransmitMs_ = nowMs;
        hasTransmitted_ = true;
        state_ = State::Idle;
    }

}
