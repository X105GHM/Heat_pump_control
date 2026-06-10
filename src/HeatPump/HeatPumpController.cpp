#include "HeatPump/HeatPumpController.hpp"
#include "Config/Pins.hpp"
#include "Config/AppConfig.hpp"
#include "Logger/Logger.hpp"

namespace heatpump
{
    HeatPumpController::HeatPumpController(HeatPumpState& state, NetBus& bus, QueueHandle_t commandQueue)
        : state_(state), bus_(bus), commandQueue_(commandQueue), statusLed_(config::pins::kStatusLed)
    {
    }

    void HeatPumpController::begin()
    {
        statusLed_.begin();
        lastPowerChangeMs_ = millis();
    }

    bool HeatPumpController::allowedByMinTimes(const bool requestedPowerOn) const
    {
        const uint32_t elapsed = millis() - lastPowerChangeMs_;
        if (requestedPowerOn && elapsed < config::control::kMinOffTimeMs) return false;
        if (!requestedPowerOn && elapsed < config::control::kMinRunTimeMs) return false;
        return true;
    }

    void HeatPumpController::processControl()
    {
        const HeatPumpData snap = state_.snapshot();
        const bool status = snap.powerOn || snap.compressorRunning || snap.currentRMS > config::control::kCompressorCurrentThresholdA;
        statusLed_.set(status);

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
                logger::Logger::log(logger::Type::Control, "power command blocked by min run/off-time guard");
                continue;
            }

            logger::Logger::log(logger::Type::Control, "safe default: command accepted by software, but NET TX is disabled until explicitly enabled");
        }

        if (snap.powerOn != lastPowerState_) 
        {
            lastPowerState_ = snap.powerOn;
            lastPowerChangeMs_ = millis();
        }
    }

    void HeatPumpController::sendCommandUnsafeUntilProtocolVerified(const HeatPumpCommand& command)
    {
        bool bits[16]{};
        size_t bitCount = 0;

        switch (command.type) 
        {
            case HeatPumpCommandType::Power:
                bits[0] = true;
                bits[1] = command.powerOn;
                bitCount = 2;
                break;
            case HeatPumpCommandType::RequestStatus:
                bits[0] = false;
                bits[1] = true;
                bitCount = 2;
                break;
            default:
                logger::Logger::log(logger::Type::Control, "TX placeholder for this command is not implemented");
                return;
        }

        logger::Logger::log(logger::Type::Control, "WARNING: sending placeholder NET bits; use only after validating bus timing");
        bus_.sendBitsSafe(bits, bitCount);
    }
}
