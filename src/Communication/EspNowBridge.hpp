#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "HeatPump/HeatPumpState.hpp"
#include "HeatPump/HeatPumpController.hpp"

namespace communication
{
    class EspNowBridge
    {
    public:
        EspNowBridge(heatpump::HeatPumpState& state, QueueHandle_t commandQueue);
        void begin();
        void loopOnce();

        // Later ESP-NOW receive callback should translate incoming packets and call this.
        bool injectCommand(const heatpump::HeatPumpCommand& command);

    private:
        heatpump::HeatPumpState& state_;
        QueueHandle_t commandQueue_;
        uint32_t lastStatusLogMs_{0};
    };
}
