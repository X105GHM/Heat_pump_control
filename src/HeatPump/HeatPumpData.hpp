#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "Core/Types.hpp"
#include "Config/AppConfig.hpp"

namespace heatpump
{
    enum class HeatPumpMode : uint8_t
    {
        Heat = 0,
        Cool = 1,
        Auto = 2,
        Unknown = 255
    };

    enum HeatPumpDataField : uint16_t
    {
        WaterTemperatureField = 1U << 0U,
        TargetTemperatureField = 1U << 1U,
        PowerStateField = 1U << 2U,
        ModeField = 1U << 3U,
        ErrorField = 1U << 4U,
        ConfigurationField = 1U << 5U
    };

    struct HeatPumpData
    {
        float waterTemperature = NAN;
        float targetTemperature = NAN;
        float currentRMS = 0.0F;
        bool powerOn = false;
        bool powerStateValid = false;
        bool compressorRunning = false;
        bool errorActive = false;
        uint8_t errorCode = 0U;
        HeatPumpMode mode = HeatPumpMode::Unknown;
        float waveform[WF_SAMPLES]{};
        bool currentClipping = false;
        uint32_t lastNetFrameMs = 0U;
        uint32_t lastCurrentUpdateMs = 0U;
        uint32_t lastConfigFrameMs = 0U;
        uint16_t validFields = 0U;
        uint8_t configFrame[config::netbus::kLongFrameBytes]{};
        bool configurationValid = false;
    };

    constexpr bool hasField(const HeatPumpData& data, const HeatPumpDataField field)
    {
        return (data.validFields & static_cast<uint16_t>(field)) != 0U;
    }

    void mergeDecodedData(HeatPumpData& current,
                          const HeatPumpData& partial,
                          uint32_t receivedAtMs);
}
