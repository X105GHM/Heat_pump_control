#include "Communication/EspNowBridge.hpp"
#include "Logger/Logger.hpp"

namespace communication
{
    EspNowBridge::EspNowBridge(heatpump::HeatPumpState& state, QueueHandle_t commandQueue)
        : state_(state), commandQueue_(commandQueue)
    {
    }

    void EspNowBridge::begin()
    {
        logger::Logger::log(logger::Type::Comms,
                            "ESP-NOW bridge stub ready. Add WiFi/esp_now init and RX callback here later.");
    }

    bool EspNowBridge::injectCommand(const heatpump::HeatPumpCommand& command)
    {
        return xQueueSend(commandQueue_, &command, pdMS_TO_TICKS(20)) == pdTRUE;
    }

    void EspNowBridge::loopOnce()
    {
        const uint32_t now = millis();
        if (now - lastStatusLogMs_ < 5000) return;
        lastStatusLogMs_ = now;

        const auto snap = state_.snapshot();
        logger::Logger::log(logger::Type::Comms,
                            "status stub: water=%.1f target=%.1f current=%.2fA power=%u comp=%u err=%u clip=%u",
                            snap.waterTemperature,
                            snap.targetTemperature,
                            snap.currentRMS,
                            snap.powerOn ? 1 : 0,
                            snap.compressorRunning ? 1 : 0,
                            snap.errorActive ? 1 : 0,
                            snap.currentClipping ? 1 : 0);
    }
}
