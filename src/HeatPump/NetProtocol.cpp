#include "HeatPump/NetProtocol.hpp"
#include "Logger/Logger.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    bool checksum12Ok(const uint8_t* bytes)
    {
        uint16_t sum = 0;

        for (uint8_t i = 0; i < 11; ++i)
        {
            sum = static_cast<uint16_t>(sum + bytes[i]);
        }

        return static_cast<uint8_t>(sum & 0xFFU) == bytes[11];
    }

    bool checksum9Ok(const uint8_t* bytes)
    {
        uint16_t sum = 0;

        for (uint8_t i = 2; i < 8; ++i)
        {
            sum = static_cast<uint16_t>(sum + bytes[i]);
        }

        return static_cast<uint8_t>(sum & 0xFFU) == bytes[8];
    }

    void bytesToHex(const uint8_t* bytes, const uint8_t count, char* out, const size_t outSize)
    {
        size_t pos = 0;

        for (uint8_t i = 0; i < count && pos + 3 < outSize; ++i)
        {
            pos += snprintf(out + pos, outSize - pos, "%02X ", bytes[i]);
        }
    }

    float decodeTemperature(const uint8_t raw)
    {
        const bool halfDegree = (raw & 0x01U) != 0;
        const bool plusTwoOffset = (raw & 0x40U) != 0;
        const bool negative = (raw & 0x80U) != 0;

        float value = static_cast<float>((raw >> 1U) & 0x1FU);

        if (halfDegree)
        {
            value += 0.5F;
        }

        if (plusTwoOffset)
        {
            value += 2.0F;
        }

        if (negative)
        {
            value = -value;
        }

        return value;
    }

    heatpump::HeatPumpMode decodeMode(const uint8_t modeByte)
    {
        const bool heat = (modeByte & 0x10U) != 0;
        const bool automatic = (modeByte & 0x20U) != 0;

        if (automatic)
        {
            return heatpump::HeatPumpMode::Auto;
        }

        if (heat)
        {
            return heatpump::HeatPumpMode::Heat;
        }

        return heatpump::HeatPumpMode::Cool;
    }

    float selectTargetTemperature(const uint8_t* bytes, const heatpump::HeatPumpMode mode)
    {
        const float coolingTarget = decodeTemperature(bytes[3]);
        const float heatingTarget = decodeTemperature(bytes[4]);
        const float autoTarget = decodeTemperature(bytes[5]);

        switch (mode)
        {
            case heatpump::HeatPumpMode::Cool:
                return coolingTarget;

            case heatpump::HeatPumpMode::Heat:
                return heatingTarget;

            case heatpump::HeatPumpMode::Auto:
                return autoTarget;

            default:
                return heatingTarget;
        }
    }
}

namespace heatpump
{
    bool NetProtocol::checksumLooksValid(const NetRawFrame& frame) const
    {
        if (frame.overflow)
        {
            return false;
        }

        if (frame.byteCount == config::netbus::kLongFrameBytes)
        {
            return frame.bitCount == config::netbus::kLongFrameBytes * 8U &&
                   checksum12Ok(frame.bytes);
        }

        if (frame.byteCount == config::netbus::kShortFrameBytes)
        {
            return frame.bitCount == config::netbus::kShortFrameBytes * 8U &&
                   checksum9Ok(frame.bytes);
        }

        return false;
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
        bytesToHex(frame.bytes, frame.byteCount, hex, sizeof(hex));

        /*logger::Logger::log(logger::Type::Protocol,
                            "raw frame: bits=%u bytes=%u overflow=%u data=%s",
                            frame.bitCount,
                            frame.byteCount,
                            frame.overflow ? 1 : 0,
                            hex); */
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
            logger::Logger::log(logger::Type::Protocol,
                                "frame rejected: checksum/length invalid bits=%u bytes=%u",
                                static_cast<unsigned>(frame.bitCount),
                                static_cast<unsigned>(frame.byteCount));
            return false;
        }

        if (frame.byteCount == config::netbus::kShortFrameBytes)
        {
            logger::Logger::log(logger::Type::Protocol,
                                "short frame ok but ignored: type=0x%02X subtype=0x%02X",
                                frame.bytes[0],
                                frame.bytes[1]);
            return true;
        }

        const uint8_t* bytes = frame.bytes;

        if (bytes[1] != 0xB1U)
        {
            logger::Logger::log(logger::Type::Protocol,
                                "frame ignored: unexpected subtype 0x%02X",
                                bytes[1]);
            return false;
        }

        switch (bytes[0])
        {
            case 0xD1:
            {
                const float waterTemperature = decodeTemperature(bytes[9]);

                outData.waterTemperature = waterTemperature;
                outData.validFields |= WaterTemperatureField;

                logger::Logger::log(logger::Type::Protocol,
                                    "D1 conditions1: water/t02=%.1fC raw=0x%02X",
                                    waterTemperature,
                                    bytes[9]);

                return true;
            }

            case 0xD2:
            {
                const float t03 = decodeTemperature(bytes[4]);
                const float exhaust = decodeTemperature(bytes[5]);
                const float coil = decodeTemperature(bytes[6]);
                const float temp4 = decodeTemperature(bytes[8]);

                logger::Logger::log(logger::Type::Protocol,
                                    "D2 conditions2: t03=%.1fC exhaust=%.1fC coil=%.1fC temp4=%.1fC",
                                    t03,
                                    exhaust,
                                    coil,
                                    temp4);

                return true;
            }

            case 0x81:
            {
                const uint8_t modeByte = bytes[2];

                const bool powerOn = (modeByte & 0x01U) != 0;
                const HeatPumpMode mode = decodeMode(modeByte);

                const float coolingTarget = decodeTemperature(bytes[3]);
                const float heatingTarget = decodeTemperature(bytes[4]);
                const float autoTarget = decodeTemperature(bytes[5]);
                const float selectedTarget = selectTargetTemperature(bytes, mode);

                outData.powerOn = powerOn;
                outData.powerStateValid = true;
                outData.mode = mode;
                outData.targetTemperature = selectedTarget;
                outData.validFields |= PowerStateField |
                                       ModeField |
                                       TargetTemperatureField |
                                       ConfigurationField;
                std::memcpy(outData.configFrame,
                            frame.bytes,
                            config::netbus::kLongFrameBytes);
                outData.configurationValid = true;

                logger::Logger::log(logger::Type::Protocol,
                                    "81 conf1: power=%u mode=%s modeByte=0x%02X cool=%.1fC heat=%.1fC auto=%.1fC selected=%.1fC",
                                    powerOn ? 1 : 0,
                                    modeToString(mode),
                                    modeByte,
                                    coolingTarget,
                                    heatingTarget,
                                    autoTarget,
                                    selectedTarget);

                return true;
            }

            case 0x82:
            {
              //  logger::Logger::log(logger::Type::Protocol, "82 config/status frame ok, currently not decoded");
                return true;
            }

            case 0x83:
            {
               //  logger::Logger::log(logger::Type::Protocol, "83 config/status frame ok, currently not decoded");
                return true;
            }

            case 0x84:
            {
                // logger::Logger::log(logger::Type::Protocol, "84 config/status frame ok, currently not decoded");
                return true;
            }

            case 0x85:
            {
               //  logger::Logger::log(logger::Type::Protocol, "85 config/status frame ok, currently not decoded");
                return true;
            }

            case 0x86:
            {
              //   logger::Logger::log(logger::Type::Protocol, "86 config/status frame ok, currently not decoded");
                return true;
            }

            default:
            {
                logger::Logger::log(logger::Type::Protocol,"known checksum but unknown frame type 0x%02X", bytes[0]);
                return false;
            }
        }
    }
}
