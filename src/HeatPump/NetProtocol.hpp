#pragma once
#include "HeatPump/NetFrame.hpp"
#include "HeatPump/HeatPumpData.hpp"

namespace heatpump
{
    class NetProtocol
    {
    public:
        bool decode(const NetRawFrame& frame, HeatPumpData& outData) const;
        bool checksumLooksValid(const NetRawFrame& frame) const;

    private:
        #if HEAT_PUMP_ENABLE_NET_DEBUG
        void logRawFrame(const NetRawFrame& frame) const;
        #endif
    };
}
