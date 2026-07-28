#pragma once

#include <cstdint>

namespace heatpump
{
    class PowerCycleGuard
    {
    public:
        PowerCycleGuard(uint32_t minimumRunTimeMs, uint32_t minimumOffTimeMs);
        void observe(bool powerOn, uint32_t nowMs);
        bool allows(bool requestedPowerOn, uint32_t nowMs) const;
        bool known() const;

    private:
        uint32_t minimumRunTimeMs_;
        uint32_t minimumOffTimeMs_;
        uint32_t lastChangeMs_ = 0U;
        bool powerOn_ = false;
        bool known_ = false;
    };
}
