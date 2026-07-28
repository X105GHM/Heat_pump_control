#include "HeatPump/HeatPumpData.hpp"

#include <cstring>

namespace heatpump
{
    void mergeDecodedData(HeatPumpData& current,
                          const HeatPumpData& partial,
                          const uint32_t receivedAtMs)
    {
        if (hasField(partial, WaterTemperatureField))
        {
            current.waterTemperature = partial.waterTemperature;
        }
        if (hasField(partial, TargetTemperatureField))
        {
            current.targetTemperature = partial.targetTemperature;
        }
        if (hasField(partial, PowerStateField))
        {
            current.powerOn = partial.powerOn;
            current.powerStateValid = partial.powerStateValid;
        }
        if (hasField(partial, ModeField))
        {
            current.mode = partial.mode;
        }
        if (hasField(partial, ErrorField))
        {
            current.errorActive = partial.errorActive;
            current.errorCode = partial.errorCode;
        }
        if (hasField(partial, ConfigurationField))
        {
            std::memcpy(current.configFrame,
                        partial.configFrame,
                        sizeof(current.configFrame));
            current.configurationValid = partial.configurationValid;
            current.lastConfigFrameMs = receivedAtMs;
        }

        current.validFields |= partial.validFields;
    }
}
