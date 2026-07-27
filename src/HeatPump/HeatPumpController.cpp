#include "HeatPump/HeatPumpController.hpp"
#include "Config/Pins.hpp"
#include "Config/AppConfig.hpp"
#include "Logger/Logger.hpp"

#include <cmath>
#include <cstdio>

namespace
{
    static constexpr size_t kConfigFrameSize = 12;

    uint8_t checksum12(const uint8_t* frame)
    {
        uint16_t sum = 0;

        for (uint8_t i = 0; i < 11; ++i)
        {
            sum = static_cast<uint16_t>(sum + frame[i]);
        }

        return static_cast<uint8_t>(sum & 0xFFU);
    }

    void updateChecksum12(uint8_t* frame)
    {
        frame[11] = checksum12(frame);
    }

    uint8_t encodeTemperature(const float temperature)
    {
        float value = temperature;

        if (!std::isfinite(value))
        {
            value = 28.0F;
        }

        if (value < 5.0F)
        {
            value = 5.0F;
        }

        if (value > 35.0F)
        {
            value = 35.0F;
        }

        const bool useOffset = value >= 2.0F;
        const float encodedValue = useOffset ? value - 2.0F : value;

        int halfSteps = static_cast<int>(std::round(encodedValue * 2.0F));

        if (halfSteps < 0)
        {
            halfSteps = 0;
        }

        if (halfSteps > 63)
        {
            halfSteps = 63;
        }

        const uint8_t integerPart = static_cast<uint8_t>(halfSteps / 2);
        const uint8_t halfBit = static_cast<uint8_t>(halfSteps % 2);

        uint8_t raw = static_cast<uint8_t>((integerPart << 1U) | halfBit);

        if (useOffset)
        {
            raw |= 0x40U;
        }

        return raw;
    }

    heatpump::HeatPumpMode modeFromFrameByte(const uint8_t modeByte)
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

    void setPower(uint8_t* frame, const bool powerOn)
    {
        if (powerOn)
        {
            frame[2] |= 0x01U;
        }
        else
        {
            frame[2] &= static_cast<uint8_t>(~0x01U);
        }
    }

    void setMode(uint8_t* frame, const heatpump::HeatPumpMode mode)
    {
        frame[2] &= static_cast<uint8_t>(~0x30U);

        switch (mode)
        {
            case heatpump::HeatPumpMode::Heat:
                frame[2] |= 0x10U;
                break;

            case heatpump::HeatPumpMode::Auto:
                frame[2] |= 0x20U;
                break;

            case heatpump::HeatPumpMode::Cool:
                break;

            default:
                break;
        }
    }

    void setTargetTemperatureForCurrentMode(uint8_t* frame, const float temperature)
    {
        const uint8_t encoded = encodeTemperature(temperature);
        const heatpump::HeatPumpMode mode = modeFromFrameByte(frame[2]);

        switch (mode)
        {
            case heatpump::HeatPumpMode::Cool:
                frame[3] = encoded;
                break;

            case heatpump::HeatPumpMode::Heat:
                frame[4] = encoded;
                break;

            case heatpump::HeatPumpMode::Auto:
                frame[5] = encoded;
                break;

            default:
                frame[5] = encoded;
                break;
        }
    }

    void frameToHex(const uint8_t* frame, char* out, const size_t outSize)
    {
        size_t pos = 0;

        for (size_t i = 0; i < kConfigFrameSize && pos + 3 < outSize; ++i)
        {
            pos += std::snprintf(out + pos, outSize - pos, "%02X ", frame[i]);
        }
    }
}

namespace heatpump
{
    HeatPumpController::HeatPumpController(HeatPumpState& state, NetBus& bus, QueueHandle_t commandQueue)
        : state_(state),
          bus_(bus),
          commandQueue_(commandQueue),
          statusLed_(config::pins::kStatusLed),
          errorLed_(config::pins::kErrorLed, config::errorLed::kCount, config::errorLed::kBrightness)
    {}

    void HeatPumpController::begin()
    {
        statusLed_.begin();
        errorLed_.begin();
        startupMs_ = millis();
        lastPowerChangeMs_ = startupMs_;
    }

    bool HeatPumpController::allowedByMinTimes(const bool requestedPowerOn) const
    {
        const uint32_t elapsed = millis() - lastPowerChangeMs_;

        if (requestedPowerOn && elapsed < config::control::kMinOffTimeMs)
        {
            return false;
        }

        if (!requestedPowerOn && elapsed < config::control::kMinRunTimeMs)
        {
            return false;
        }

        return true;
    }

    const char* HeatPumpController::faultToString(const led::Fault fault)
    {
        switch (fault)
        {
            case led::Fault::NoNetConnection:
                return "no NET-bus connection";

            case led::Fault::NoCurrentWhileOn:
                return "power on but no current";

            case led::Fault::CurrentSensorStale:
                return "current measurement stale";

            case led::Fault::AdcClipping:
                return "current ADC clipping";

            case led::Fault::HeatPumpReportedError:
                return "heat pump reported error";

            case led::Fault::None:
            default:
                return "none";
        }
    }

    led::Fault HeatPumpController::evaluateFault(const HeatPumpData& snap, uint8_t& detailCode)
    {
        detailCode = 0;

        const uint32_t now = millis();
        const bool startupGraceFinished = (now - startupMs_) >= config::control::kStartupGraceMs;

        if (snap.errorActive)
        {
            detailCode = snap.errorCode;
            return led::Fault::HeatPumpReportedError;
        }

        if (snap.currentClipping)
        {
            return led::Fault::AdcClipping;
        }

        if (startupGraceFinished &&
            (snap.lastCurrentUpdateMs == 0 ||
             (now - snap.lastCurrentUpdateMs) > config::control::kCurrentMeasurementTimeoutMs))
        {
            return led::Fault::CurrentSensorStale;
        }

        if (snap.powerStateValid &&
            snap.powerOn &&
            snap.currentRMS < config::control::kExpectedRunningCurrentMinA)
        {
            if (lowCurrentSinceMs_ == 0)
            {
                lowCurrentSinceMs_ = now;
            }

            if ((now - lowCurrentSinceMs_) >= config::control::kNoCurrentDetectionDelayMs)
            {
                return led::Fault::NoCurrentWhileOn;
            }
        }
        else
        {
            lowCurrentSinceMs_ = 0;
        }

        if (startupGraceFinished &&
            (snap.lastNetFrameMs == 0 ||
             (now - snap.lastNetFrameMs) > config::control::kNetConnectionTimeoutMs))
        {
            return led::Fault::NoNetConnection;
        }

        return led::Fault::None;
    }

    void HeatPumpController::processControl()
    {
        const HeatPumpData snap = state_.snapshot();

        const bool status = (snap.powerStateValid && snap.powerOn) || snap.compressorRunning;
        statusLed_.set(status);

        uint8_t faultDetail = 0;
        const led::Fault fault = evaluateFault(snap, faultDetail);

        errorLed_.setFault(fault, faultDetail);
        errorLed_.update();

        if (fault != lastFault_)
        {
            logger::Logger::log(logger::Type::Control,
                                "fault changed: %s detail=%u",
                                faultToString(fault),
                                static_cast<unsigned>(faultDetail));

            lastFault_ = fault;
        }

        HeatPumpCommand command{};

        while (xQueueReceive(commandQueue_, &command, 0) == pdTRUE)
        {
            logger::Logger::log(logger::Type::Control,
                                "command received type=%u power=%u target=%.1f mode=%u",
                                static_cast<unsigned>(command.type),
                                command.powerOn ? 1 : 0,
                                command.targetTemperature,
                                static_cast<unsigned>(command.mode));

            if (command.type == HeatPumpCommandType::Power && !allowedByMinTimes(command.powerOn))
            {
                logger::Logger::log(logger::Type::Control,
                                    "power command blocked by min run/off-time guard");
                continue;
            }

            sendCommandUnsafeUntilProtocolVerified(command);
        }

        if (snap.powerStateValid && snap.powerOn != lastPowerState_)
        {
            lastPowerState_ = snap.powerOn;
            lastPowerChangeMs_ = millis();
            lowCurrentSinceMs_ = 0;
        }
    }

    void HeatPumpController::sendCommandUnsafeUntilProtocolVerified(const HeatPumpCommand& command)
    {
        static uint8_t configFrame[kConfigFrameSize] =
        {
            0x81, 0xB1, 0x26, 0x72, 0x76, 0x74,
            0x3D, 0x3D, 0x3D, 0x3D, 0x3C, 0xE4
        };

        switch (command.type)
        {
            case HeatPumpCommandType::Power:
            {
                setPower(configFrame, command.powerOn);
                updateChecksum12(configFrame);

                logger::Logger::log(logger::Type::Control,
                                    "NET command: power %s",
                                    command.powerOn ? "ON" : "OFF");

                break;
            }

            case HeatPumpCommandType::SetTemperature:
            {
                setTargetTemperatureForCurrentMode(configFrame, command.targetTemperature);
                updateChecksum12(configFrame);

                logger::Logger::log(logger::Type::Control,
                                    "NET command: target %.1fC",
                                    command.targetTemperature);

                break;
            }

            case HeatPumpCommandType::SetMode:
            {
                setMode(configFrame, command.mode);
                updateChecksum12(configFrame);

                logger::Logger::log(logger::Type::Control,
                                    "NET command: mode %u",
                                    static_cast<unsigned>(command.mode));

                break;
            }

            case HeatPumpCommandType::RequestStatus:
            {
                logger::Logger::log(logger::Type::Control,
                                    "NET command: request status ignored; status is sent periodically");

                return;
            }

            default:
            {
                logger::Logger::log(logger::Type::Control,
                                    "NET command rejected: unknown type=%u",
                                    static_cast<unsigned>(command.type));

                return;
            }
        }

        char hex[3 * kConfigFrameSize + 1]{};
        frameToHex(configFrame, hex, sizeof(hex));

        logger::Logger::log(logger::Type::Control,
                            "NET TX config frame: %s",
                            hex);

        bus_.sendBytesSafe(configFrame, kConfigFrameSize);
    }
}