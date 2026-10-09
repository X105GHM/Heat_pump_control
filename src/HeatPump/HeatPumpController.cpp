#include "HeatPump/HeatPumpController.hpp"

#include "Config/Pins.hpp"
#include "Config/AppConfig.hpp"
#include "Logger/Logger.hpp"

#if HEAT_PUMP_ENABLE_COMMAND_DEBUG
#include <cstdio>
#endif
#include <cstring>

namespace
{
    const char* commandTypeToString(const heatpump::HeatPumpCommandType type)
    {
        switch (type)
        {
            case heatpump::HeatPumpCommandType::Power: return "Power";
            case heatpump::HeatPumpCommandType::SetTemperature: return "SetTemperature";
            case heatpump::HeatPumpCommandType::SetMode: return "SetMode";
            case heatpump::HeatPumpCommandType::RequestStatus: return "RequestStatus";
            case heatpump::HeatPumpCommandType::SafetyStop: return "SafetyStop";
            default: return "Unknown";
        }
    }

    #if HEAT_PUMP_ENABLE_COMMAND_DEBUG
    const char* txStatusToString(const heatpump::NetTxStatus status)
    {
        switch (status)
        {
            case heatpump::NetTxStatus::Sent: return "Sent";
            case heatpump::NetTxStatus::Expired: return "Expired";
            case heatpump::NetTxStatus::Failed:
            default: return "Failed";
        }
    }
    #endif

    const char* ackStageToString(const poolwire::CommandAckStage stage)
    {
        switch (stage)
        {
            case poolwire::CommandAckStage::ReceivedByNode: return "ReceivedByNode";
            case poolwire::CommandAckStage::AppliedLocally: return "AppliedLocally";
            case poolwire::CommandAckStage::Rejected: return "Rejected";
            case poolwire::CommandAckStage::Failed:
            default: return "Failed";
        }
    }

    const char* ackResultToString(const poolwire::AckResult result)
    {
        switch (result)
        {
            case poolwire::AckResult::Accepted: return "Accepted";
            case poolwire::AckResult::Rejected: return "Rejected";
            case poolwire::AckResult::QueueFull: return "QueueFull";
            case poolwire::AckResult::InvalidPayload: return "InvalidPayload";
            case poolwire::AckResult::Expired: return "Expired";
            case poolwire::AckResult::Superseded: return "Superseded";
            case poolwire::AckResult::ApplicationFailed:
            default: return "ApplicationFailed";
        }
    }

    #if HEAT_PUMP_ENABLE_COMMAND_DEBUG
    void frameToHex(const uint8_t* frame, const size_t frameSize, char* output, const size_t outputSize)
    {
        size_t position = 0U;
        for (size_t index = 0U; index < frameSize && position + 3U < outputSize; ++index)
        {
            position += std::snprintf(output + position, outputSize - position, "%02X ", frame[index]);
        }
    }
    #endif
}

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
          errorLed_(config::pins::kErrorLed, config::errorLed::kCount, config::errorLed::kBrightness),
          powerGuard_(config::control::kMinRunTimeMs, config::control::kMinOffTimeMs)
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

    led::Fault HeatPumpController::evaluateFault(const HeatPumpData& snapshot, uint8_t& detailCode)
    {
        detailCode = 0U;
        const uint32_t now = millis();
        const bool startupGraceFinished = static_cast<uint32_t>(now - startupMs_) >= config::control::kStartupGraceMs;

        if (snapshot.errorActive)
        {
            detailCode = snapshot.errorCode;
            return led::Fault::HeatPumpReportedError;
        }
        if (snapshot.currentClipping) return led::Fault::AdcClipping;

        if (startupGraceFinished && (snapshot.lastCurrentUpdateMs == 0U || static_cast<uint32_t>(now - snapshot.lastCurrentUpdateMs) > config::control::kCurrentMeasurementTimeoutMs))
        {
            return led::Fault::CurrentSensorStale;
        }

        if (snapshot.powerStateValid && snapshot.powerOn && snapshot.currentRMS < config::control::kExpectedRunningCurrentMinA)
        {
            if (lowCurrentSinceMs_ == 0U) lowCurrentSinceMs_ = now;
            if (static_cast<uint32_t>(now - lowCurrentSinceMs_) >= config::control::kNoCurrentDetectionDelayMs)
            {
                return led::Fault::NoCurrentWhileOn;
            }
        }
        else
        {
            lowCurrentSinceMs_ = 0U;
        }

        if (startupGraceFinished && (snapshot.lastNetFrameMs == 0U || static_cast<uint32_t>(now - snapshot.lastNetFrameMs) > config::control::kNetConnectionTimeoutMs))
        {
            return led::Fault::NoNetConnection;
        }

        return led::Fault::None;
    }

    bool HeatPumpController::netStateIsCurrent(const HeatPumpData& snapshot, const uint32_t now) const
    {
        return snapshot.configurationValid &&
                snapshot.lastConfigFrameMs != 0U &&
               snapshot.lastNetFrameMs != 0U &&
               static_cast<uint32_t>(now - snapshot.lastConfigFrameMs) <= config::control::kNetConnectionTimeoutMs &&
               static_cast<uint32_t>(now - snapshot.lastNetFrameMs) <= config::control::kNetConnectionTimeoutMs;
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

        if (xQueueSend(resultQueue_, &commandResult, pdMS_TO_TICKS(20)) != pdTRUE)
        {
            logger::Logger::log(logger::Level::Error, logger::Type::Control, "command result queue full id=%lu", static_cast<unsigned long>(command.commandId));
        }
    }

    void HeatPumpController::completePending(const poolwire::CommandAckStage stage, const poolwire::AckResult result, const uint16_t errorCode)
    {
        if (!pending_.active) return;
        const logger::Level level = stage == poolwire::CommandAckStage::AppliedLocally ? logger::Level::Info : logger::Level::Error;
        logger::Logger::log(level,
                            logger::Type::Control,
                            "command final id=%lu type=%s stage=%s result=%s error=%u response_ms=%lu",
                            static_cast<unsigned long>(pending_.command.commandId),
                            commandTypeToString(pending_.command.type),
                            ackStageToString(stage),
                            ackResultToString(result),
                            static_cast<unsigned>(errorCode),
                            pending_.firstTransmittedAtMs == 0U
                                ? 0UL : static_cast<unsigned long>(millis() - pending_.firstTransmittedAtMs));
        sendResult(pending_.command, stage, result, errorCode);

        if (pending_.safetyStop && stage == poolwire::CommandAckStage::AppliedLocally)
        {
            safetyStopRequested_ = false;
        }
        pending_ = PendingCommand{};
    }

    bool HeatPumpController::pendingPowerCommandIsRetryable() const
    {
        return pending_.active &&
               (pending_.command.type == HeatPumpCommandType::Power ||
                pending_.command.type == HeatPumpCommandType::SafetyStop);
    }

    void HeatPumpController::queuePendingPowerRetry(const uint32_t now)
    {
        if (!pendingPowerCommandIsRetryable() || pending_.phase != PendingPhase::AwaitingTelemetry || static_cast<int32_t>(now - pending_.nextRetryAtMs) < 0)
        {
            return;
        }

        NetTxRequest request{};
        std::memcpy(request.bytes, pending_.expectedFrame, NetConfiguration::kFrameSize);
        request.byteCount = static_cast<uint8_t>(NetConfiguration::kFrameSize);
        request.commandId = pending_.command.commandId;
        request.commandType = pending_.command.type;
        request.localExpiresAtMs = pending_.safetyStop ? UINT32_MAX : pending_.command.localExpiresAtMs;
        request.reportResult = true;

        if (netTxQueue_ != nullptr && xQueueSend(netTxQueue_, &request, 0U) == pdTRUE)
        {
            pending_.phase = PendingPhase::AwaitingTransmit;
            #if HEAT_PUMP_ENABLE_COMMAND_DEBUG
            logger::Logger::log(logger::Level::Debug,
                                logger::Type::Control,
                                "power command retry queued id=%lu attempt=%u desired=%s elapsed_ms=%lu",
                                static_cast<unsigned long>(pending_.command.commandId),
                                static_cast<unsigned>(pending_.transmitAttemptCount + 1U),
                                pending_.command.type == HeatPumpCommandType::Power &&
                                    pending_.command.powerOn
                                        ? "on"
                                        : "off",
                                pending_.firstTransmittedAtMs == 0U ? 0UL : static_cast<unsigned long>(now - pending_.firstTransmittedAtMs));
            #endif
            return;
        }

        pending_.nextRetryAtMs = now + config::control::kPowerCommandRetryIntervalMs;
        logger::Logger::log(logger::Level::Warn,
                            logger::Type::Control,
                            "power command retry queue busy id=%lu next_ms=%lu",
                            static_cast<unsigned long>(pending_.command.commandId),
                            static_cast<unsigned long>(pending_.nextRetryAtMs));
    }

    bool HeatPumpController::submitConfigCommand(const HeatPumpCommand& command, const HeatPumpData& snapshot, const bool safetyStop)
    {
        const uint32_t now = millis();
        if (!netStateIsCurrent(snapshot, now))
        {
            if (!safetyStop)
            {
                sendResult(command, poolwire::CommandAckStage::Failed, poolwire::AckResult::ApplicationFailed, 3U);
            }
            return false;
        }

        if (NetConfiguration::matches(snapshot.configFrame, command))
        {
            if (!safetyStop)
            {
                sendResult(command, poolwire::CommandAckStage::AppliedLocally, poolwire::AckResult::Accepted, 0U);
            }
            else
            {
                safetyStopRequested_ = false;
            }
            return true;
        }

        if ((command.type == HeatPumpCommandType::Power || command.type == HeatPumpCommandType::SafetyStop) && !powerGuard_.allows(command.type == HeatPumpCommandType::Power && command.powerOn, now))
        {
            if (!safetyStop)
            {
                sendResult(command, poolwire::CommandAckStage::Rejected, poolwire::AckResult::Rejected, 2U);
            }
            return false;
        }

        NetTxRequest request{};
        if (!NetConfiguration::apply(snapshot.configFrame, command, request.bytes))
        {
            if (!safetyStop)
            {
                sendResult(command, poolwire::CommandAckStage::Rejected, poolwire::AckResult::InvalidPayload, 4U);
            }
            return false;
        }

        request.byteCount = static_cast<uint8_t>(NetConfiguration::kFrameSize);
        request.commandId = command.commandId;
        request.commandType = command.type;
        request.localExpiresAtMs = safetyStop ? UINT32_MAX : command.localExpiresAtMs;
        request.reportResult = true;

        #if HEAT_PUMP_ENABLE_COMMAND_DEBUG
            char hex[3U * NetConfiguration::kFrameSize + 1U]{};
            frameToHex(request.bytes, request.byteCount, hex, sizeof(hex));
            logger::Logger::log(logger::Level::Debug,
                            logger::Type::Control,
                            "command prepared id=%lu sequence=%lu type=%s power=%u target=%.1f mode=%u messageType=%u expectedResponse=0x81/B1 checksum=0x%02X frame=%s",
                            static_cast<unsigned long>(command.commandId),
                            static_cast<unsigned long>(command.sequenceNumber),
                            commandTypeToString(command.type),
                            command.powerOn ? 1U : 0U,
                            command.targetTemperature,
                            static_cast<unsigned>(command.mode),
                            static_cast<unsigned>(poolwire::MessageType::HeatPumpCommand),
                            request.bytes[NetConfiguration::kFrameSize - 1U],
                            hex);
        #endif

        if (netTxQueue_ == nullptr || xQueueSend(netTxQueue_, &request, 0U) != pdTRUE)
        {
            if (!safetyStop)
            {
                sendResult(command, poolwire::CommandAckStage::Rejected, poolwire::AckResult::QueueFull, 5U);
            }
            return false;
        }

        pending_ = PendingCommand{};
        pending_.active = true;
        pending_.safetyStop = safetyStop;
        pending_.command = command;
        pending_.phase = PendingPhase::AwaitingTransmit;
        std::memcpy(pending_.expectedFrame, request.bytes, sizeof(pending_.expectedFrame));
        return true;
    }

    void HeatPumpController::processNetTxResults()
    {
        NetTxResult result{};
        while (netTxResultQueue_ != nullptr && xQueueReceive(netTxResultQueue_, &result, 0U) == pdTRUE)
        {
            if (!pending_.active || pending_.command.commandId != result.commandId || pending_.command.type != result.commandType)
            {
                continue;
            }

            #if HEAT_PUMP_ENABLE_COMMAND_DEBUG
            logger::Logger::log(logger::Level::Debug,
                                logger::Type::Control,
                                "NET TX result id=%lu type=%s status=%s completed_ms=%lu",
                                static_cast<unsigned long>(result.commandId),
                                commandTypeToString(result.commandType),
                                txStatusToString(result.status),
                                static_cast<unsigned long>(result.completedAtMs));
            #endif

            if (result.status == NetTxStatus::Sent)
            {
                pending_.phase = PendingPhase::AwaitingTelemetry;
                if (pending_.firstTransmittedAtMs == 0U)
                {
                    pending_.firstTransmittedAtMs = result.completedAtMs;
                }
                pending_.nextRetryAtMs = result.completedAtMs + config::control::kPowerCommandRetryIntervalMs;
                ++pending_.transmitAttemptCount;

                if (pendingPowerCommandIsRetryable())
                {
                #if HEAT_PUMP_ENABLE_COMMAND_DEBUG
                    logger::Logger::log(logger::Level::Debug,
                                        logger::Type::Control,
                                        "power command attempt sent id=%lu attempt=%u desired=%s next_retry_ms=%lu",
                                        static_cast<unsigned long>(pending_.command.commandId),
                                        static_cast<unsigned>(pending_.transmitAttemptCount),
                                        pending_.command.type == HeatPumpCommandType::Power &&
                                            pending_.command.powerOn ? "on" : "off",
                                        static_cast<unsigned long>(pending_.nextRetryAtMs));
                #endif
                }
            }
            else if (result.status == NetTxStatus::Expired)
            {
                completePending(poolwire::CommandAckStage::Rejected, poolwire::AckResult::Expired, 6U);
            }
            else
            {
                if (pendingPowerCommandIsRetryable())
                {
                    pending_.phase = PendingPhase::AwaitingTelemetry;
                    pending_.nextRetryAtMs = millis() + config::control::kPowerCommandRetryIntervalMs;
                    logger::Logger::log(logger::Level::Warn, logger::Type::Control,
                                        "power command TX failed; retry retained id=%lu next_ms=%lu",
                                        static_cast<unsigned long>(pending_.command.commandId),
                                        static_cast<unsigned long>(pending_.nextRetryAtMs));
                }
                else
                {
                    completePending(poolwire::CommandAckStage::Failed, poolwire::AckResult::ApplicationFailed, 7U);
                }
            }
        }
    }

    void HeatPumpController::processPendingConfirmation(const HeatPumpData& snapshot)
    {
        if (!pending_.active) return;

        const uint32_t now = millis();
        if (!pending_.safetyStop && static_cast<int32_t>(pending_.command.localExpiresAtMs - now) <= 0)
        {
            completePending(poolwire::CommandAckStage::Failed, poolwire::AckResult::Expired, 8U);
            return;
        }

        if (pending_.firstTransmittedAtMs == 0U)
        {
            queuePendingPowerRetry(now);
            return;
        }

        const bool freshConfiguration = snapshot.configurationValid && static_cast<int32_t>(snapshot.lastConfigFrameMs - pending_.firstTransmittedAtMs) >= 0;
        const bool matches = freshConfiguration && NetConfiguration::matches(snapshot.configFrame, pending_.command);

        #if HEAT_PUMP_ENABLE_COMMAND_DEBUG
        if (freshConfiguration && snapshot.lastConfigFrameMs != pending_.lastObservedConfigFrameMs)
        {
            pending_.lastObservedConfigFrameMs = snapshot.lastConfigFrameMs;
            char actualHex[3U * NetConfiguration::kFrameSize + 1U]{};
            char expectedHex[3U * NetConfiguration::kFrameSize + 1U]{};
            frameToHex(snapshot.configFrame,
                       NetConfiguration::kFrameSize,
                       actualHex,
                       sizeof(actualHex));
            frameToHex(pending_.expectedFrame,
                       NetConfiguration::kFrameSize,
                       expectedHex,
                       sizeof(expectedHex));
            logger::Logger::log(logger::Level::Debug,
                                logger::Type::Control,
                                "NET readback id=%lu response_ms=%lu checksum=%s status=%s expected=%s actual=%s",
                                static_cast<unsigned long>(pending_.command.commandId),
                                static_cast<unsigned long>(snapshot.lastConfigFrameMs - pending_.firstTransmittedAtMs),
                                NetConfiguration::isValid(snapshot.configFrame, NetConfiguration::kFrameSize) ? "OK" : "BAD",
                                matches ? "confirmed" : "mismatch",
                                expectedHex,
                                actualHex);
        }
        #endif

        if (matches)
        {
            completePending(poolwire::CommandAckStage::AppliedLocally, poolwire::AckResult::Accepted, 0U);
            return;
        }

        if (static_cast<uint32_t>(now - pending_.firstTransmittedAtMs) >= config::control::kCommandConfirmationTimeoutMs)
        {
            completePending(poolwire::CommandAckStage::Failed, poolwire::AckResult::ApplicationFailed, 9U);
            return;
        }

        queuePendingPowerRetry(now);
    }

    void HeatPumpController::rejectQueuedCommandsForSafety()
    {
        HeatPumpCommand queued{};
        while (commandQueue_ != nullptr && xQueueReceive(commandQueue_, &queued, 0U) == pdTRUE)
        {
            if (queued.type != HeatPumpCommandType::SafetyStop)
            {
                sendResult(queued, poolwire::CommandAckStage::Rejected, poolwire::AckResult::Rejected, 10U);
            }
        }
    }

    void HeatPumpController::processSafetyRequest(const HeatPumpData& snapshot)
    {
        HeatPumpCommand front{};
        if (commandQueue_ != nullptr && xQueuePeek(commandQueue_, &front, 0U) == pdTRUE && front.type == HeatPumpCommandType::SafetyStop)
        {
            xQueueReceive(commandQueue_, &front, 0U);
            safetyStopRequested_ = true;

            if (pending_.active)
            {
                completePending(poolwire::CommandAckStage::Failed, poolwire::AckResult::ApplicationFailed, 10U);
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

        logger::Logger::log(logger::Level::Info,
                            logger::Type::Control,
                            "command requested id=%lu sequence=%lu type=%s power=%u target=%.1f mode=%u expires_ms=%lu",
                            static_cast<unsigned long>(command.commandId),
                            static_cast<unsigned long>(command.sequenceNumber),
                            commandTypeToString(command.type),
                            command.powerOn ? 1U : 0U,
                            command.targetTemperature,
                            static_cast<unsigned>(command.mode),
                            static_cast<unsigned long>(command.localExpiresAtMs));

        const uint32_t now = millis();
        if (command.type == HeatPumpCommandType::SafetyStop)
        {
            safetyStopRequested_ = true;
            processSafetyRequest(snapshot);
            return;
        }

        if (static_cast<int32_t>(command.localExpiresAtMs - now) <= 0)
        {
            sendResult(command, poolwire::CommandAckStage::Rejected, poolwire::AckResult::Expired, 1U);
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

        statusLed_.set((snapshot.powerStateValid && snapshot.powerOn) || snapshot.compressorRunning);

        uint8_t faultDetail = 0U;
        const led::Fault fault = evaluateFault(snapshot, faultDetail);
        errorLed_.setFault(fault, faultDetail);
        errorLed_.update();

        if (fault != lastFault_)
        {
            logger::Logger::log(fault == led::Fault::None
                                    ? logger::Level::Info
                                    : logger::Level::Warn,
                                logger::Type::Control,
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
