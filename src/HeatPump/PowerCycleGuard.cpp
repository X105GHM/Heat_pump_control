#include "HeatPump/PowerCycleGuard.hpp"

namespace heatpump
{
    PowerCycleGuard::PowerCycleGuard(const uint32_t minimumRunTimeMs,
                                     const uint32_t minimumOffTimeMs)
        : minimumRunTimeMs_(minimumRunTimeMs),
          minimumOffTimeMs_(minimumOffTimeMs)
    {}

    void PowerCycleGuard::observe(const bool powerOn, const uint32_t nowMs)
    {
        if (!known_ || powerOn != powerOn_)
        {
            powerOn_ = powerOn;
            lastChangeMs_ = nowMs;
            known_ = true;
        }
    }

    bool PowerCycleGuard::allows(const bool requestedPowerOn, const uint32_t nowMs) const
    {
        if (!known_) return false;
        if (requestedPowerOn == powerOn_) return true;
        const uint32_t elapsed = static_cast<uint32_t>(nowMs - lastChangeMs_);
        return requestedPowerOn
            ? elapsed >= minimumOffTimeMs_
            : elapsed >= minimumRunTimeMs_;
    }

}
