#pragma once

#include <cstdint>

#include "Config/AppConfig.hpp"

namespace heatpump
{
    struct NetRawFrame
    {
        uint32_t timestampMs = 0U;
        uint32_t completedAtUs = 0U;
        uint16_t bitCount = 0U;
        uint8_t bytes[config::netbus::kMaxBytesPerFrame]{};
        uint8_t byteCount = 0U;
        bool overflow = false;
    };
}
