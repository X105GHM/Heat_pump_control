#pragma once

#include <cstddef>
#include <cstdint>

#include "HeatPump/HeatPumpCommand.hpp"

namespace heatpump
{
    class NetConfiguration
    {
    public:
        static constexpr size_t kFrameSize = config::netbus::kLongFrameBytes;

        static uint8_t checksum(const uint8_t* frame);
        static bool isValid(const uint8_t* frame, size_t size);
        static bool apply(const uint8_t* current, const HeatPumpCommand& command, uint8_t* output);
        static bool matches(const uint8_t* actual, const HeatPumpCommand& command);

    private:
        static uint8_t encodeTemperature(float temperature);
        static float decodeTemperature(uint8_t raw);
        static HeatPumpMode decodeMode(uint8_t modeByte);
    };
}
