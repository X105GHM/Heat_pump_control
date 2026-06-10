#include "HeatPump/NetProtocol.hpp"
#include "Logger/Logger.hpp"

namespace heatpump
{
    bool NetProtocol::checksumLooksValid(const NetRawFrame& frame) const
    {
        // Placeholder: PC1001/PC1002-like variants are not guaranteed to share
        // checksum format. Keep this explicit until enough raw captures exist.
        return frame.byteCount > 0 && !frame.overflow;
    }

    const char* NetProtocol::modeToString(const HeatPumpMode mode)
    {
        switch (mode) 
        {
            case HeatPumpMode::Heat: return "Heat";
            case HeatPumpMode::Cool: return "Cool";
            case HeatPumpMode::Auto: return "Auto";
            default: return "Unknown";
        }
    }

    void NetProtocol::logRawFrame(const NetRawFrame& frame) const
    {
        char hex[3 * config::netbus::kMaxBytesPerFrame + 1]{};
        size_t pos = 0;
        for (uint8_t i = 0; i < frame.byteCount && pos + 3 < sizeof(hex); ++i) 
        {
            pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", frame.bytes[i]);
        }

        logger::Logger::log(logger::Type::Protocol,
                            "raw frame: bits=%u bytes=%u overflow=%u data=%s",
                            frame.bitCount,
                            frame.byteCount,
                            frame.overflow ? 1 : 0,
                            hex);
    }

    bool NetProtocol::decode(const NetRawFrame& frame, HeatPumpData& outData) const
    {
        outData = HeatPumpData{};
        outData.waterTemperature = NAN;
        outData.targetTemperature = NAN;
        outData.mode = HeatPumpMode::Unknown;
        logRawFrame(frame);

        if (!checksumLooksValid(frame)) 
        {
            logger::Logger::log(logger::Type::Protocol, "frame rejected by basic validity check");
            return false;
        }

        // Reverse-engineering area:
        // Add known byte/bit mappings here after comparing raw frames with display state.
        // Examples to identify:
        // - power bit changes when display power button is pressed
        // - target temperature byte changes by +/- 1 °C
        // - water temperature follows real sensor/display value
        // - error code appears when controller reports an alarm

        return false; // No confirmed semantic decoding yet.
    }
}
