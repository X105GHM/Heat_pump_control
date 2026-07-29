#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <PoolWireProtocol.hpp>
#include "HeatPump/HeatPumpState.hpp"
#include "HeatPump/NetBus.hpp"
#include "HeatPump/NetConfiguration.hpp"
#include "HeatPump/PowerCycleGuard.hpp"
#include "Led/StatusLed.hpp"
#include "Led/ErrorLed.hpp"

namespace heatpump
{
    class HeatPumpController
    {
    public:
        HeatPumpController(HeatPumpState& state,
                           QueueHandle_t commandQueue,
                           QueueHandle_t resultQueue,
                           QueueHandle_t netTxQueue,
                           QueueHandle_t netTxResultQueue);
        void begin();
        void processControl();

    private:
        enum class PendingPhase : uint8_t { AwaitingTransmit, AwaitingTelemetry };

        struct PendingCommand
        {
            HeatPumpCommand command{};
            uint8_t expectedFrame[NetConfiguration::kFrameSize]{};
            uint32_t transmittedAtMs = 0U;
            uint32_t lastObservedConfigFrameMs = 0U;
            PendingPhase phase = PendingPhase::AwaitingTransmit;
            bool active = false;
            bool safetyStop = false;
        };

        void processSafetyRequest(const HeatPumpData& snapshot);
        void processNetTxResults();
        void processPendingConfirmation(const HeatPumpData& snapshot);
        void processNextCommand(const HeatPumpData& snapshot);
        bool submitConfigCommand(const HeatPumpCommand& command,
                                 const HeatPumpData& snapshot,
                                 bool safetyStop);
        void completePending(poolwire::CommandAckStage stage,
                             poolwire::AckResult result,
                             uint16_t errorCode);
        void sendResult(const HeatPumpCommand& command,
                        poolwire::CommandAckStage stage,
                        poolwire::AckResult result,
                        uint16_t errorCode);
        void rejectQueuedCommandsForSafety();
        bool netStateIsCurrent(const HeatPumpData& snapshot, uint32_t now) const;
        led::Fault evaluateFault(const HeatPumpData& snap, uint8_t& detailCode);
        static const char* faultToString(led::Fault fault);

        HeatPumpState& state_;
        QueueHandle_t commandQueue_;
        QueueHandle_t resultQueue_;
        QueueHandle_t netTxQueue_;
        QueueHandle_t netTxResultQueue_;
        led::StatusLed statusLed_;
        led::ErrorLed errorLed_;
        PowerCycleGuard powerGuard_;
        PendingCommand pending_{};
        uint32_t startupMs_{0};
        uint32_t lowCurrentSinceMs_{0};
        bool safetyStopRequested_{false};
        led::Fault lastFault_{led::Fault::None};
    };
}
