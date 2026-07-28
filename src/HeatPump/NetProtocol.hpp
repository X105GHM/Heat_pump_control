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
        void logRawFrame(const NetRawFrame& frame) const;

    private:
        static const char* modeToString(HeatPumpMode mode);
    };
}
