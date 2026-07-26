#include "HeatPump/HeatPumpController.hpp"
#include "Config/Pins.hpp"
#include "Config/AppConfig.hpp"
#include "Logger/Logger.hpp"

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
        if (requestedPowerOn && elapsed < config::control::kMinOffTimeMs) return false;
        if (!requestedPowerOn && elapsed < config::control::kMinRunTimeMs) return false;
        return true;
    }

    const char* HeatPumpController::faultToString(const led::Fault fault)
    {
        switch (fault) {
            case led::Fault::NoNetConnection: return "no NET-bus connection";
            case led::Fault::NoCurrentWhileOn: return "power on but no current";
            case led::Fault::CurrentSensorStale: return "current measurement stale";
            case led::Fault::AdcClipping: return "current ADC clipping";
            case led::Fault::HeatPumpReportedError: return "heat pump reported error";
            case led::Fault::None:
            default: return "none";
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

        if (snap.currentClipping) return led::Fault::AdcClipping;

        if (startupGraceFinished && (snap.lastCurrentUpdateMs == 0 ||
             (now - snap.lastCurrentUpdateMs) > config::control::kCurrentMeasurementTimeoutMs)) 
        {
            return led::Fault::CurrentSensorStale;
        }

        if (snap.powerStateValid && snap.powerOn && snap.currentRMS < config::control::kExpectedRunningCurrentMinA) 
        {
            if (lowCurrentSinceMs_ == 0) lowCurrentSinceMs_ = now;
            if ((now - lowCurrentSinceMs_) >= config::control::kNoCurrentDetectionDelayMs) 
            {
                return led::Fault::NoCurrentWhileOn;
            }
        } 
        else 
        {
            lowCurrentSinceMs_ = 0;
        }

        if (startupGraceFinished && (snap.lastNetFrameMs == 0 || (now - snap.lastNetFrameMs) > config::control::kNetConnectionTimeoutMs)) 
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
                logger::Logger::log(logger::Type::Control, "power command blocked by min run/off-time guard");
                continue;
            }


            logger::Logger::log(logger::Type::Control, "safe default: command accepted by software, but NET TX is disabled until explicitly enabled");
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
        (void)command;

        logger::Logger::log(logger::Type::Control, "command TX disabled; use manual nettx HEXFRAME for raw bus tests");
    }
}
