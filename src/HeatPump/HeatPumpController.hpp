#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "HeatPump/HeatPumpState.hpp"
#include "HeatPump/NetBus.hpp"
#include "HeatPump/NetProtocol.hpp"
#include "Led/StatusLed.hpp"
#include "Led/ErrorLed.hpp"

namespace heatpump
{
    enum class HeatPumpCommandType : uint8_t
    {
        Power = 0,
        SetTemperature = 1,
        SetMode = 2,
        RequestStatus = 3
    };

    struct HeatPumpCommand
    {
        HeatPumpCommandType type = HeatPumpCommandType::RequestStatus;
        bool powerOn = false;
        float targetTemperature = NAN;
        HeatPumpMode mode = HeatPumpMode::Unknown;
    };

    class HeatPumpController
    {
    public:
        HeatPumpController(HeatPumpState& state, NetBus& bus, QueueHandle_t commandQueue);
        void begin();
        void processControl();
        void sendCommandUnsafeUntilProtocolVerified(const HeatPumpCommand& command);

    private:
        bool allowedByMinTimes(bool requestedPowerOn) const;
        led::Fault evaluateFault(const HeatPumpData& snap, uint8_t& detailCode);
        static const char* faultToString(led::Fault fault);

        HeatPumpState& state_;
        NetBus& bus_;
        QueueHandle_t commandQueue_;
        led::StatusLed statusLed_;
        led::ErrorLed errorLed_;
        uint32_t startupMs_{0};
        uint32_t lastPowerChangeMs_{0};
        uint32_t lowCurrentSinceMs_{0};
        bool lastPowerState_{false};
        led::Fault lastFault_{led::Fault::None};
    };
}
