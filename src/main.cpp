#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include "Config/Pins.hpp"
#include "Config/AppConfig.hpp"
#include "Logger/Logger.hpp"
#include "Led/StatusLed.hpp"
#include "HeatPump/HeatPumpState.hpp"
#include "HeatPump/NetBus.hpp"
#include "HeatPump/NetBusArbiter.hpp"
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
    QueueHandle_t gCommandResultQueue = nullptr;
    QueueHandle_t gNetTxQueue = nullptr;
    QueueHandle_t gNetTxResultQueue = nullptr;

    led::StatusLed gRxLed(config::pins::kRxLed);
    led::StatusLed gTxLed(config::pins::kTxLed);

    [[noreturn]] void restartAfterInitializationFailure(const char* reason)
    {
        logger::Logger::log(logger::Level::Error, logger::Type::General, "%s; restarting", reason);
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP.restart();
        for (;;) vTaskDelay(portMAX_DELAY);
    }

    void netBusOwnerTask(void *)
    {
        gNetBus.begin();
        heatpump::NetBusArbiter arbiter(config::control::kMinNetCommandIntervalMs);
        heatpump::NetProtocol protocol;
        heatpump::NetRawFrame frame{};
        heatpump::NetTxRequest pendingTx{};
        bool hasPendingTx = false;
        uint32_t rxLedOffAtMs = 0U;
        uint32_t txLedOffAtMs = 0U;

        for (;;)
        {
            const uint32_t now = millis();
            if (rxLedOffAtMs != 0U && static_cast<int32_t>(now - rxLedOffAtMs) >= 0)
            {
                gRxLed.set(false);
                rxLedOffAtMs = 0U;
            }
            if (txLedOffAtMs != 0U && static_cast<int32_t>(now - txLedOffAtMs) >= 0)
            {
                gTxLed.set(false);
                txLedOffAtMs = 0U;
            }

            if (!hasPendingTx && xQueueReceive(gNetTxQueue, &pendingTx, 0U) == pdTRUE)
            {
                hasPendingTx = true;
            }

            if (hasPendingTx && pendingTx.localExpiresAtMs != UINT32_MAX && static_cast<int32_t>(pendingTx.localExpiresAtMs - now) <= 0)
            {
                if (pendingTx.reportResult)
                {
                    heatpump::NetTxResult result{};
                    result.commandId = pendingTx.commandId;
                    result.commandType = pendingTx.commandType;
                    result.completedAtMs = now;
                    result.status = heatpump::NetTxStatus::Expired;
                    if (xQueueSend(gNetTxResultQueue, &result, 0U) != pdTRUE)
                    {
                        logger::Logger::log(logger::Level::Error, logger::Type::Control, "NET TX result queue full id=%lu", static_cast<unsigned long>(result.commandId));
                    }
                }
                hasPendingTx = false;
            }

            if (arbiter.beginReceive())
            {
                const bool received = gNetBus.sniffFrame(frame);
                arbiter.endReceive();

                if (received)
                {
                    gRxLed.set(true);
                    rxLedOffAtMs = millis() + 20U;

                    if (xQueueSend(gRawFrameQueue, &frame, 0U) != pdTRUE)
                    {
                        logger::Logger::log(logger::Level::Warn, logger::Type::NetBus, "raw frame queue full; frame dropped");
                    }

                    const bool replyTrigger =
                        frame.completedAtUs != 0U &&
                        frame.byteCount == config::netbus::kLongFrameBytes &&
                        frame.bytes[1] == 0xB1U &&
                        protocol.checksumLooksValid(frame);

                    if (hasPendingTx && replyTrigger)
                    {
                        const uint32_t replyAtUs = frame.completedAtUs + config::netbus::kTxReplyDelayUs;
                        const uint32_t sendCallAtUs = replyAtUs - config::netbus::kFrameGapUs;
                        #if HEAT_PUMP_ENABLE_NET_DEBUG
                        logger::Logger::log(logger::Level::Debug,
                                            logger::Type::NetBus,
                                            "NET TX scheduled commandId=%lu trigger=0x%02X/0x%02X frameEndUs=%lu replyAtUs=%lu",
                                            static_cast<unsigned long>(pendingTx.commandId),
                                            frame.bytes[0],
                                            frame.bytes[1],
                                            static_cast<unsigned long>(frame.completedAtUs),
                                            static_cast<unsigned long>(replyAtUs));
                        #endif

                        for (;;)
                        {
                            const int32_t remainingUs = static_cast<int32_t>(sendCallAtUs - micros());
                            if (remainingUs <= 0) break;
                            if (remainingUs > 1500)
                            {
                                vTaskDelay(pdMS_TO_TICKS(1));
                            }
                            else
                            {
                                delayMicroseconds(static_cast<uint32_t>(remainingUs));
                            }
                        }

                        const uint32_t txNow = millis();
                        const bool expired = pendingTx.localExpiresAtMs != UINT32_MAX && static_cast<int32_t>(pendingTx.localExpiresAtMs - txNow) <= 0;

                        if (expired)
                        {
                            if (pendingTx.reportResult)
                            {
                                heatpump::NetTxResult result{};
                                result.commandId = pendingTx.commandId;
                                result.commandType = pendingTx.commandType;
                                result.completedAtMs = txNow;
                                result.status = heatpump::NetTxStatus::Expired;
                                if (xQueueSend(gNetTxResultQueue, &result, 0U) != pdTRUE)
                                {
                                    logger::Logger::log(logger::Level::Error, logger::Type::Control,
                                                        "NET TX result queue full id=%lu",
                                                        static_cast<unsigned long>(result.commandId));
                                }
                            }
                            hasPendingTx = false;
                        }
                        else if (arbiter.beginTransmit(txNow))
                        {
                            const bool sent = gNetBus.sendBytesSafe(pendingTx.bytes, pendingTx.byteCount);
                            const uint32_t completedAtMs = millis();
                            arbiter.endTransmit(completedAtMs);
                            gTxLed.set(true);
                            txLedOffAtMs = completedAtMs + 50U;

                            if (pendingTx.reportResult)
                            {
                                heatpump::NetTxResult result{};
                                result.commandId = pendingTx.commandId;
                                result.commandType = pendingTx.commandType;
                                result.completedAtMs = completedAtMs;
                                result.status = sent ? heatpump::NetTxStatus::Sent : heatpump::NetTxStatus::Failed;
                                if (xQueueSend(gNetTxResultQueue, &result, 0U) != pdTRUE)
                                {
                                    logger::Logger::log(logger::Level::Error,
                                                        logger::Type::Control,
                                                        "NET TX result queue full id=%lu",
                                                        static_cast<unsigned long>(result.commandId));
                                }
                            }
                            hasPendingTx = false;
                        }
                    }
                }
            }

            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    void netProtocolTask(void *)
    {
        heatpump::NetProtocol protocol;
        heatpump::NetRawFrame frame{};

        for (;;)
        {
            if (xQueueReceive(gRawFrameQueue, &frame, portMAX_DELAY) == pdTRUE)
            {
                heatpump::HeatPumpData decoded{};
                const bool decodedOk = protocol.decode(frame, decoded);
                if (protocol.checksumLooksValid(frame))
                {
                    gState.markNetFrameReceived(frame.timestampMs);
                    if (decodedOk)
                    {
                        gState.updateFromDecoded(decoded, frame.timestampMs);
                    }
                }
            }
        }
    }

    void currentSensorTask(void *)
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

            vTaskDelay(pdMS_TO_TICKS(config::current::kMeasurementPeriodMs));
        }
    }

    void heatPumpControlTask(void *)
    {
        heatpump::HeatPumpController controller(gState,
                                                gCommandQueue,
                                                gCommandResultQueue,
                                                gNetTxQueue,
                                                gNetTxResultQueue);
        controller.begin();

        for (;;)
        {
            controller.processControl();
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    void communicationTask(void *)
    {
        communication::EspNowBridge bridge(gState, gCommandQueue, gCommandResultQueue);
        if (!bridge.begin())
        {
            restartAfterInitializationFailure("ESP-NOW initialization failed");
        }

        for (;;)
        {
            bridge.loopOnce();
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }

}

void setup()
{
    logger::Logger::begin(config::kSerialBaud);

    if (!gState.begin())
    {
        restartAfterInitializationFailure("state mutex allocation failed");
    }
    gCurrentSensor.begin();
    gRxLed.begin();
    gTxLed.begin();

    gRawFrameQueue = xQueueCreate(8, sizeof(heatpump::NetRawFrame));
    gCommandQueue = xQueueCreate(8, sizeof(heatpump::HeatPumpCommand));
    gCommandResultQueue = xQueueCreate(8, sizeof(heatpump::HeatPumpCommandResult));
    gNetTxQueue = xQueueCreate(8, sizeof(heatpump::NetTxRequest));
    gNetTxResultQueue = xQueueCreate(8, sizeof(heatpump::NetTxResult));

    if (gRawFrameQueue == nullptr ||
        gCommandQueue == nullptr ||
        gCommandResultQueue == nullptr ||
        gNetTxQueue == nullptr ||
        gNetTxResultQueue == nullptr)
    {
        restartAfterInitializationFailure("queue allocation failed");
    }

    const bool tasksCreated =
        xTaskCreatePinnedToCore(netBusOwnerTask, "NetBusOwner", 4096, nullptr, 2, nullptr, 0) == pdPASS &&
        xTaskCreatePinnedToCore(netProtocolTask, "NetProtocol", 4096, nullptr, 1, nullptr, 0) == pdPASS &&
        xTaskCreatePinnedToCore(currentSensorTask, "Current", 4096, nullptr, 1, nullptr, 1) == pdPASS &&
        xTaskCreatePinnedToCore(heatPumpControlTask, "HPControl", 4096, nullptr, 1, nullptr, 1) == pdPASS &&
        xTaskCreatePinnedToCore(communicationTask, "Comms", 4096, nullptr, 1, nullptr, 1) == pdPASS;

    if (!tasksCreated)
    {
        restartAfterInitializationFailure("task creation failed");
    }

    logger::Logger::log(logger::Level::Info, logger::Type::General, "setup complete; NET-bus and ESP-NOW tasks active");
}

void loop()
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}
