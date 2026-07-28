#include "HeatPump/HeatPumpController.hpp"

#include "Config/Pins.hpp"
#include "Config/AppConfig.hpp"
#include "Logger/Logger.hpp"

#include <cstring>

namespace heatpump
{
    HeatPumpController::HeatPumpController(HeatPumpState& state,
                                           QueueHandle_t commandQueue,
                                           QueueHandle_t resultQueue,
                                           QueueHandle_t netTxQueue,
                                           QueueHandle_t netTxResultQueue)
        : state_(state),
          commandQueue_(commandQueue),
          resultQueue_(resultQueue),
          netTxQueue_(netTxQueue),
          netTxResultQueue_(netTxResultQueue),
          statusLed_(config::pins::kStatusLed),
          errorLed_(config::pins::kErrorLed,
                    config::errorLed::kCount,
                    config::errorLed::kBrightness),
          powerGuard_(config::control::kMinRunTimeMs,
                      config::control::kMinOffTimeMs)
    {}

    void HeatPumpController::begin()
    {
        statusLed_.begin();
        errorLed_.begin();
        startupMs_ = millis();
    }

    const char* HeatPumpController::faultToString(const led::Fault fault)
    {
        switch (fault)
        {
            case led::Fault::NoNetConnection: return "no NET-bus connection";
            case led::Fault::NoCurrentWhileOn: return "power on but no current";
            case led::Fault::CurrentSensorStale: return "current measurement stale";
            case led::Fault::AdcClipping: return "current ADC clipping";
            case led::Fault::HeatPumpReportedError: return "heat pump reported error";
            case led::Fault::None:
            default: return "none";
        }
    }

    led::Fault HeatPumpController::evaluateFault(const HeatPumpData& snapshot,
                                                 uint8_t& detailCode)
    {
        detailCode = 0U;
        const uint32_t now = millis();
        const bool startupGraceFinished =
            static_cast<uint32_t>(now - startupMs_) >= config::control::kStartupGraceMs;

        if (snapshot.errorActive)
        {
            detailCode = snapshot.errorCode;
            return led::Fault::HeatPumpReportedError;
        }
        if (snapshot.currentClipping) return led::Fault::AdcClipping;

        if (startupGraceFinished &&
            (snapshot.lastCurrentUpdateMs == 0U ||
             static_cast<uint32_t>(now - snapshot.lastCurrentUpdateMs) >
                 config::control::kCurrentMeasurementTimeoutMs))
        {
            return led::Fault::CurrentSensorStale;
        }

        if (snapshot.powerStateValid && snapshot.powerOn &&
            snapshot.currentRMS < config::control::kExpectedRunningCurrentMinA)
        {
            if (lowCurrentSinceMs_ == 0U) lowCurrentSinceMs_ = now;
            if (static_cast<uint32_t>(now - lowCurrentSinceMs_) >=
                config::control::kNoCurrentDetectionDelayMs)
            {
                return led::Fault::NoCurrentWhileOn;
            }
        }
        else
        {
            lowCurrentSinceMs_ = 0U;
        }

        if (startupGraceFinished &&
            (snapshot.lastNetFrameMs == 0U ||
             static_cast<uint32_t>(now - snapshot.lastNetFrameMs) >
                 config::control::kNetConnectionTimeoutMs))
        {
            return led::Fault::NoNetConnection;
        }

        return led::Fault::None;
    }

    bool HeatPumpController::netStateIsCurrent(const HeatPumpData& snapshot,
                                               const uint32_t now) const
    {
        return snapshot.configurationValid &&
               snapshot.lastConfigFrameMs != 0U &&
               snapshot.lastNetFrameMs != 0U &&
               static_cast<uint32_t>(now - snapshot.lastConfigFrameMs) <=
                   config::control::kNetConnectionTimeoutMs &&
               static_cast<uint32_t>(now - snapshot.lastNetFrameMs) <=
                   config::control::kNetConnectionTimeoutMs;
    }

    void HeatPumpController::sendResult(const HeatPumpCommand& command,
                                        const poolwire::CommandAckStage stage,
                                        const poolwire::AckResult result,
                                        const uint16_t errorCode)
    {
        if (command.commandId == 0U || resultQueue_ == nullptr) return;

        HeatPumpCommandResult commandResult{};
        commandResult.commandId = command.commandId;
        commandResult.type = command.type;
        commandResult.stage = stage;
        commandResult.result = result;
        commandResult.errorCode = errorCode;

        if (xQueueSend(resultQueue_, &commandResult, 0U) != pdTRUE)
        {
            logger::Logger::log(logger::Type::Control,
                                "command result queue full id=%lu",
                                static_cast<unsigned long>(command.commandId));
        }
    }

    void HeatPumpController::completePending(const poolwire::CommandAckStage stage,
                                             const poolwire::AckResult result,
                                             const uint16_t errorCode)
    {
        if (!pending_.active) return;
        sendResult(pending_.command, stage, result, errorCode);

        if (pending_.safetyStop && stage == poolwire::CommandAckStage::AppliedLocally)
        {
            safetyStopRequested_ = false;
        }
        pending_ = PendingCommand{};
    }

    bool HeatPumpController::submitConfigCommand(const HeatPumpCommand& command,
                                                 const HeatPumpData& snapshot,
                                                 const bool safetyStop)
    {
        const uint32_t now = millis();
        if (!netStateIsCurrent(snapshot, now))
        {
            if (!safetyStop)
            {
                sendResult(command,
                           poolwire::CommandAckStage::Failed,
                           poolwire::AckResult::ApplicationFailed,
                           3U);
            }
            return false;
        }

        if (NetConfiguration::matches(snapshot.configFrame, command))
        {
            if (!safetyStop)
            {
                sendResult(command,
                           poolwire::CommandAckStage::AppliedLocally,
                           poolwire::AckResult::Accepted,
                           0U);
            }
            else
            {
                safetyStopRequested_ = false;
            }
            return true;
        }

        if ((command.type == HeatPumpCommandType::Power ||
             command.type == HeatPumpCommandType::SafetyStop) &&
            !powerGuard_.allows(command.type == HeatPumpCommandType::Power && command.powerOn,
                                now))
        {
            if (!safetyStop)
            {
                sendResult(command,
                           poolwire::CommandAckStage::Rejected,
                           poolwire::AckResult::Rejected,
                           2U);
            }
            return false;
        }

        NetTxRequest request{};
        if (!NetConfiguration::apply(snapshot.configFrame, command, request.bytes))
        {
            if (!safetyStop)
            {
                sendResult(command,
                           poolwire::CommandAckStage::Rejected,
                           poolwire::AckResult::InvalidPayload,
                           4U);
            }
            return false;
        }

        request.byteCount = static_cast<uint8_t>(NetConfiguration::kFrameSize);
        request.commandId = command.commandId;
        request.commandType = command.type;
        request.localExpiresAtMs = safetyStop ? UINT32_MAX : command.localExpiresAtMs;
        request.reportResult = true;

        if (netTxQueue_ == nullptr || xQueueSend(netTxQueue_, &request, 0U) != pdTRUE)
        {
            if (!safetyStop)
            {
                sendResult(command,
                           poolwire::CommandAckStage::Rejected,
                           poolwire::AckResult::QueueFull,
                           5U);
            }
            return false;
        }

        pending_ = PendingCommand{};
        pending_.active = true;
        pending_.safetyStop = safetyStop;
        pending_.command = command;
        pending_.phase = PendingPhase::AwaitingTransmit;
        std::memcpy(pending_.expectedFrame,
                    request.bytes,
                    sizeof(pending_.expectedFrame));
        return true;
    }

    void HeatPumpController::processNetTxResults()
    {
        NetTxResult result{};
        while (netTxResultQueue_ != nullptr &&
               xQueueReceive(netTxResultQueue_, &result, 0U) == pdTRUE)
        {
            if (!pending_.active ||
                pending_.command.commandId != result.commandId ||
                pending_.command.type != result.commandType)
            {
                continue;
            }

            if (result.status == NetTxStatus::Sent)
            {
                pending_.phase = PendingPhase::AwaitingTelemetry;
                pending_.transmittedAtMs = result.completedAtMs;
            }
            else if (result.status == NetTxStatus::Expired)
            {
                completePending(poolwire::CommandAckStage::Rejected,
                                poolwire::AckResult::Expired,
                                6U);
            }
            else
            {
                completePending(poolwire::CommandAckStage::Failed,
                                poolwire::AckResult::ApplicationFailed,
                                7U);
            }
        }
    }

    void HeatPumpController::processPendingConfirmation(const HeatPumpData& snapshot)
    {
        if (!pending_.active) return;

        const uint32_t now = millis();
        if (pending_.phase == PendingPhase::AwaitingTransmit &&
            !pending_.safetyStop &&
            static_cast<int32_t>(pending_.command.localExpiresAtMs - now) <= 0)
        {
            completePending(poolwire::CommandAckStage::Failed,
                            poolwire::AckResult::Expired,
                            8U);
            return;
        }

        if (pending_.phase != PendingPhase::AwaitingTelemetry) return;

        if (snapshot.configurationValid &&
            snapshot.lastConfigFrameMs >= pending_.transmittedAtMs &&
            NetConfiguration::matches(snapshot.configFrame, pending_.command))
        {
            completePending(poolwire::CommandAckStage::AppliedLocally,
                            poolwire::AckResult::Accepted,
                            0U);
            return;
        }

        if (static_cast<uint32_t>(now - pending_.transmittedAtMs) >=
            config::control::kCommandConfirmationTimeoutMs)
        {
            completePending(poolwire::CommandAckStage::Failed,
                            poolwire::AckResult::ApplicationFailed,
                            9U);
        }
    }

    void HeatPumpController::rejectQueuedCommandsForSafety()
    {
        HeatPumpCommand queued{};
        while (commandQueue_ != nullptr && xQueueReceive(commandQueue_, &queued, 0U) == pdTRUE)
        {
            if (queued.type != HeatPumpCommandType::SafetyStop)
            {
                sendResult(queued,
                           poolwire::CommandAckStage::Rejected,
                           poolwire::AckResult::Rejected,
                           10U);
            }
        }
    }

    void HeatPumpController::processSafetyRequest(const HeatPumpData& snapshot)
    {
        HeatPumpCommand front{};
        if (commandQueue_ != nullptr &&
            xQueuePeek(commandQueue_, &front, 0U) == pdTRUE &&
            front.type == HeatPumpCommandType::SafetyStop)
        {
            xQueueReceive(commandQueue_, &front, 0U);
            safetyStopRequested_ = true;

            if (pending_.active)
            {
                completePending(poolwire::CommandAckStage::Failed,
                                poolwire::AckResult::ApplicationFailed,
                                10U);
            }
            rejectQueuedCommandsForSafety();
        }

        if (!safetyStopRequested_ || pending_.active) return;

        HeatPumpCommand stop{};
        stop.type = HeatPumpCommandType::SafetyStop;
        stop.powerOn = false;
        submitConfigCommand(stop, snapshot, true);
    }

    void HeatPumpController::processNextCommand(const HeatPumpData& snapshot)
    {
        if (pending_.active || safetyStopRequested_ || commandQueue_ == nullptr) return;

        HeatPumpCommand command{};
        if (xQueueReceive(commandQueue_, &command, 0U) != pdTRUE) return;

        const uint32_t now = millis();
        if (command.type == HeatPumpCommandType::SafetyStop)
        {
            safetyStopRequested_ = true;
            processSafetyRequest(snapshot);
            return;
        }

        if (static_cast<int32_t>(command.localExpiresAtMs - now) <= 0)
        {
            sendResult(command,
                       poolwire::CommandAckStage::Rejected,
                       poolwire::AckResult::Expired,
                       1U);
            return;
        }

        if (command.type == HeatPumpCommandType::RequestStatus)
        {
            const bool netOnline = netStateIsCurrent(snapshot, now);
            sendResult(command,
                       netOnline
                           ? poolwire::CommandAckStage::AppliedLocally
                           : poolwire::CommandAckStage::Failed,
                       netOnline
                           ? poolwire::AckResult::Accepted
                           : poolwire::AckResult::ApplicationFailed,
                       netOnline ? 0U : 3U);
            return;
        }

        submitConfigCommand(command, snapshot, false);
    }

    void HeatPumpController::processControl()
    {
        HeatPumpData snapshot = state_.snapshot();
        if (snapshot.powerStateValid)
        {
            powerGuard_.observe(snapshot.powerOn, millis());
        }

        statusLed_.set((snapshot.powerStateValid && snapshot.powerOn) ||
                       snapshot.compressorRunning);

        uint8_t faultDetail = 0U;
        const led::Fault fault = evaluateFault(snapshot, faultDetail);
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

        processSafetyRequest(snapshot);
        processNetTxResults();
        snapshot = state_.snapshot();
        processPendingConfirmation(snapshot);
        processSafetyRequest(snapshot);
        processNextCommand(snapshot);
    }
}
