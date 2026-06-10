#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include "Config/Pins.hpp"
#include "Config/AppConfig.hpp"
#include "Core/TaskUtils.hpp"
#include "Logger/Logger.hpp"
#include "Led/StatusLed.hpp"
#include "HeatPump/HeatPumpState.hpp"
#include "HeatPump/NetBus.hpp"
#include "HeatPump/NetProtocol.hpp"
#include "HeatPump/HeatPumpController.hpp"
#include "CurrentSensor/CurrentSensor.hpp"
#include "Communication/EspNowBridge.hpp"

namespace
{
    heatpump::HeatPumpState gState;
    heatpump::NetBus gNetBus(config::pins::kNetBus);
    current::CurrentSensor gCurrentSensor(config::pins::kCurrentAdc);

    QueueHandle_t gRawFrameQueue = nullptr;
    QueueHandle_t gCommandQueue = nullptr;

    led::StatusLed gRxLed(config::pins::kRxLed);
    led::StatusLed gTxLed(config::pins::kTxLed);

    void netBusRxTask(void*)
    {
        heatpump::NetRawFrame frame{};
        for (;;) 
        {
            if (gNetBus.sniffFrame(frame)) 
            {
                gRxLed.pulse(20);

                if (xQueueSend(gRawFrameQueue, &frame, pdMS_TO_TICKS(10)) != pdTRUE) 
                {
                    logger::Logger::log(logger::Type::NetBus, "raw frame queue full; frame dropped");
                }
            }
            taskYIELD();
        }
    }

    void netProtocolTask(void*)
    {
        heatpump::NetProtocol protocol;
        heatpump::NetRawFrame frame{};
        heatpump::HeatPumpData decoded{};

        for (;;) 
        {
            if (xQueueReceive(gRawFrameQueue, &frame, portMAX_DELAY) == pdTRUE) 
            {
                if (protocol.decode(frame, decoded)) 
                {
                    gState.updateFromDecoded(decoded);
                }
            }
        }
    }

    void currentSensorTask(void*)
    {
        for (;;) 
        {
            const auto measurement = gCurrentSensor.measure();
            const bool compressorRunning = measurement.currentRms > config::control::kCompressorCurrentThresholdA;

            gState.updateFromCurrent(measurement.currentRms,
                                     measurement.waveform,
                                     measurement.waveformCount,
                                     measurement.clipping,
                                     compressorRunning);

            logger::Logger::log(logger::Type::Current,
                                "Irms=%.2fA VrmsAC=%.3fV mean=%.3fV clipping=%u",
                                measurement.currentRms,
                                measurement.voltageRmsAc,
                                measurement.adcMeanVoltage,
                                measurement.clipping ? 1 : 0);

            core::delayMs(config::current::kMeasurementPeriodMs);
        }
    }

    void heatPumpControlTask(void*)
    {
        heatpump::HeatPumpController controller(gState, gNetBus, gCommandQueue);
        controller.begin();

        for (;;) 
        {
            controller.processControl();
            core::delayMs(100);
        }
    }

    void communicationTask(void*)
    {
        communication::EspNowBridge bridge(gState, gCommandQueue);
        bridge.begin();

        for (;;) 
        {
            bridge.loopOnce();
            core::delayMs(200);
        }
    }
}

void setup()
{
    logger::Logger::begin(config::kSerialBaud);

    gState.begin();
    gNetBus.begin();
    gCurrentSensor.begin();
    gRxLed.begin();
    gTxLed.begin();

    gRawFrameQueue = xQueueCreate(8, sizeof(heatpump::NetRawFrame));
    gCommandQueue = xQueueCreate(8, sizeof(heatpump::HeatPumpCommand));

    if (gRawFrameQueue == nullptr || gCommandQueue == nullptr)
    {
        logger::Logger::log(logger::Type::General, "queue allocation failed; restarting");
        delay(1000);
        ESP.restart();
    }

    xTaskCreatePinnedToCore(netBusRxTask, "NetBusRx", 4096, nullptr, 3, nullptr, 0);
    xTaskCreatePinnedToCore(netProtocolTask, "NetProtocol", 4096, nullptr, 2, nullptr, 0);
    xTaskCreatePinnedToCore(currentSensorTask, "Current", 4096, nullptr, 1, nullptr, 1);
    xTaskCreatePinnedToCore(heatPumpControlTask, "HPControl", 4096, nullptr, 1, nullptr, 1);
    xTaskCreatePinnedToCore(communicationTask, "Comms", 4096, nullptr, 1, nullptr, 1);

    logger::Logger::log(logger::Type::General, "setup complete; safe sniff-only mode active");
}

void loop()
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}
