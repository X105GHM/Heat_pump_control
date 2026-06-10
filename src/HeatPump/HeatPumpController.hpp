#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "HeatPump/HeatPumpState.hpp"
#include "HeatPump/NetBus.hpp"
#include "HeatPump/NetProtocol.hpp"
#include "Led/StatusLed.hpp"

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

        HeatPumpState& state_;
        NetBus& bus_;
        QueueHandle_t commandQueue_;
        led::StatusLed statusLed_;
        uint32_t lastPowerChangeMs_{0};
        bool lastPowerState_{false};
    };
}
