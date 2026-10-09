#include "HeatPump/NetConfiguration.hpp"

#include <cmath>
#include <cstring>

namespace heatpump
{
    uint8_t NetConfiguration::checksum(const uint8_t* frame)
    {
        uint16_t sum = 0U;
        for (size_t index = 0U; index < kFrameSize - 1U; ++index)
        {
            sum = static_cast<uint16_t>(sum + frame[index]);
        }
        return static_cast<uint8_t>(sum & 0xFFU);
    }

    bool NetConfiguration::isValid(const uint8_t* frame, const size_t size)
    {
        return frame != nullptr &&
               size == kFrameSize &&
               frame[0] == 0x81U &&
               frame[1] == 0xB1U &&
               checksum(frame) == frame[kFrameSize - 1U];
    }

    uint8_t NetConfiguration::encodeTemperature(float temperature)
    {
        const float adjusted = temperature - 2.0F;
        const int halfSteps = static_cast<int>(std::round(adjusted * 2.0F));
        const uint8_t integerPart = static_cast<uint8_t>(halfSteps / 2);
        const uint8_t halfBit = static_cast<uint8_t>(halfSteps % 2);
        return static_cast<uint8_t>((integerPart << 1U) | halfBit | 0x40U);
    }

    float NetConfiguration::decodeTemperature(const uint8_t raw)
    {
        float value = static_cast<float>((raw >> 1U) & 0x1FU);
        if ((raw & 0x01U) != 0U) value += 0.5F;
        if ((raw & 0x40U) != 0U) value += 2.0F;
        if ((raw & 0x80U) != 0U) value = -value;
        return value;
    }

    HeatPumpMode NetConfiguration::decodeMode(const uint8_t modeByte)
    {
        if ((modeByte & 0x20U) != 0U) return HeatPumpMode::Auto;
        if ((modeByte & 0x10U) != 0U) return HeatPumpMode::Heat;
        return HeatPumpMode::Cool;
    }

    bool NetConfiguration::apply(const uint8_t* current, const HeatPumpCommand& command, uint8_t* output)
    {
        if (!isValid(current, kFrameSize) || output == nullptr)
        {
            return false;
        }

        std::memcpy(output, current, kFrameSize);

        switch (command.type)
        {
            case HeatPumpCommandType::Power:
            case HeatPumpCommandType::SafetyStop:
                if (command.type == HeatPumpCommandType::Power && command.powerOn)
                    output[2] |= 0x01U;
                else
                    output[2] &= static_cast<uint8_t>(~0x01U);
                break;

            case HeatPumpCommandType::SetMode:
                if (command.mode != HeatPumpMode::Heat && command.mode != HeatPumpMode::Cool && command.mode != HeatPumpMode::Auto)
                {
                    return false;
                }
                output[2] &= static_cast<uint8_t>(~0x30U);
                if (command.mode == HeatPumpMode::Heat) output[2] |= 0x10U;
                if (command.mode == HeatPumpMode::Auto) output[2] |= 0x20U;
                break;

            case HeatPumpCommandType::SetTemperature:
            {
                if (!std::isfinite(command.targetTemperature) ||
                    command.targetTemperature < 5.0F ||
                    command.targetTemperature > 33.5F)
                {
                    return false;
                }
                const uint8_t encoded = encodeTemperature(command.targetTemperature);
                switch (decodeMode(output[2]))
                {
                    case HeatPumpMode::Cool: output[3] = encoded; break;
                    case HeatPumpMode::Heat: output[4] = encoded; break;
                    case HeatPumpMode::Auto: output[5] = encoded; break;
                    default: return false;
                }
                break;
            }

            default:
                return false;
        }

        output[kFrameSize - 1U] = checksum(output);
        return true;
    }

    bool NetConfiguration::matches(const uint8_t* actual, const HeatPumpCommand& command)
    {
        if (!isValid(actual, kFrameSize)) return false;

        switch (command.type)
        {
            case HeatPumpCommandType::Power:
                return ((actual[2] & 0x01U) != 0U) == command.powerOn;
            case HeatPumpCommandType::SafetyStop:
                return (actual[2] & 0x01U) == 0U;
            case HeatPumpCommandType::SetMode:
                return decodeMode(actual[2]) == command.mode;
            case HeatPumpCommandType::SetTemperature:
            {
                uint8_t raw = actual[4];
                if (decodeMode(actual[2]) == HeatPumpMode::Cool) raw = actual[3];
                if (decodeMode(actual[2]) == HeatPumpMode::Auto) raw = actual[5];
                return std::fabs(decodeTemperature(raw) - command.targetTemperature) < 0.26F;
            }
            default:
                return false;
        }
    }
}
