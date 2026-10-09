#include "HeatPump/BridgeWatchdog.hpp"

namespace heatpump
{
    BridgeWatchdog::BridgeWatchdog(const uint32_t timeoutMs) : timeoutMs_(timeoutMs){}

    void BridgeWatchdog::start(const uint32_t nowMs)
    {
        startedAtMs_ = nowMs;
        lastSafetyRequestMs_ = nowMs;
        online_ = false;
    }

    BridgeWatchdog::Transition BridgeWatchdog::update(const uint32_t nowMs, const uint32_t lastContactMs)
    {
        const bool isOnline = lastContactMs != 0U && static_cast<uint32_t>(nowMs - lastContactMs) <= timeoutMs_;
        const bool reconnected = isOnline && !online_;
        online_ = isOnline;

        bool requestSafetyStop = false;
        if (!online_ && static_cast<uint32_t>(nowMs - startedAtMs_) >= timeoutMs_ && static_cast<uint32_t>(nowMs - lastSafetyRequestMs_) >= timeoutMs_)
        {
            requestSafetyStop = true;
            lastSafetyRequestMs_ = nowMs;
        }

        return {requestSafetyStop, reconnected};
    }

    bool BridgeWatchdog::online() const
    {
        return online_;
    }
}
