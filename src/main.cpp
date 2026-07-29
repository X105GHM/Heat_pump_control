#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include <cstring>

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

    int hexNibbleToInt(const char c)
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'A' && c <= 'F')
            return 10 + c - 'A';
        if (c >= 'a' && c <= 'f')
            return 10 + c - 'a';
        return -1;
    }

    bool parseHexFrame(const String &text, uint8_t *outBytes, size_t &outCount, const size_t maxBytes)
    {
        outCount = 0;
        int highNibble = -1;

        for (size_t i = 0; i < text.length(); ++i)
        {
            const char c = text[i];

            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ':' || c == '-')
            {
                continue;
            }

            const int nibble = hexNibbleToInt(c);
            if (nibble < 0)
            {
                return false;
            }

            if (highNibble < 0)
            {
                highNibble = nibble;
            }
            else
            {
                if (outCount >= maxBytes)
                {
                    return false;
                }

                outBytes[outCount++] = static_cast<uint8_t>((highNibble << 4) | nibble);
                highNibble = -1;
            }
        }

        return highNibble < 0 && outCount > 0;
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

            if (!hasPendingTx &&
                xQueueReceive(gNetTxQueue, &pendingTx, 0U) == pdTRUE)
            {
                hasPendingTx = true;
            }

            if (hasPendingTx &&
                pendingTx.localExpiresAtMs != UINT32_MAX &&
                static_cast<int32_t>(pendingTx.localExpiresAtMs - now) <= 0)
            {
                if (pendingTx.reportResult)
                {
                    heatpump::NetTxResult result{};
                    result.commandId = pendingTx.commandId;
                    result.commandType = pendingTx.commandType;
                    result.completedAtMs = now;
                    result.status = heatpump::NetTxStatus::Expired;
                    xQueueSend(gNetTxResultQueue, &result, 0U);
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
                        logger::Logger::log(logger::Type::NetBus,
                                            "raw frame queue full; frame dropped");
                    }

                    const bool replyTrigger =
                        frame.completedAtUs != 0U &&
                        frame.byteCount == config::netbus::kLongFrameBytes &&
                        frame.bytes[1] == 0xB1U &&
                        protocol.checksumLooksValid(frame);

                    if (hasPendingTx && replyTrigger)
                    {
                        const uint32_t replyAtUs =
                            frame.completedAtUs + config::netbus::kTxReplyDelayUs;
                        const uint32_t sendCallAtUs =
                            replyAtUs - config::netbus::kFrameGapUs;
                        logger::Logger::log(logger::Type::NetBus,
                                            "NET TX scheduled commandId=%lu trigger=0x%02X/0x%02X frameEndUs=%lu replyAtUs=%lu",
                                            static_cast<unsigned long>(pendingTx.commandId),
                                            frame.bytes[0],
                                            frame.bytes[1],
                                            static_cast<unsigned long>(frame.completedAtUs),
                                            static_cast<unsigned long>(replyAtUs));

                        for (;;)
                        {
                            const int32_t remainingUs =
                                static_cast<int32_t>(sendCallAtUs - micros());
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
                        const bool expired =
                            pendingTx.localExpiresAtMs != UINT32_MAX &&
                            static_cast<int32_t>(pendingTx.localExpiresAtMs - txNow) <= 0;

                        if (expired)
                        {
                            if (pendingTx.reportResult)
                            {
                                heatpump::NetTxResult result{};
                                result.commandId = pendingTx.commandId;
                                result.commandType = pendingTx.commandType;
                                result.completedAtMs = txNow;
                                result.status = heatpump::NetTxStatus::Expired;
                                xQueueSend(gNetTxResultQueue, &result, 0U);
                            }
                            hasPendingTx = false;
                        }
                        else if (arbiter.beginTransmit(txNow))
                        {
                            const bool sent = gNetBus.sendBytesSafe(pendingTx.bytes,
                                                                    pendingTx.byteCount);
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
                                result.status = sent
                                    ? heatpump::NetTxStatus::Sent
                                    : heatpump::NetTxStatus::Failed;
                                xQueueSend(gNetTxResultQueue, &result, 0U);
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

           /* logger::Logger::log(logger::Type::Current,
                                "Irms=%.2fA VrmsAC=%.3fV mean=%.3fV clipping=%u",
                                measurement.currentRms,
                                measurement.voltageRmsAc,
                                measurement.adcMeanVoltage,
                                measurement.clipping ? 1 : 0); */

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
        bridge.begin();

        for (;;)
        {
            bridge.loopOnce();
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }

    void serialCommandTask(void *)
    {
        logger::Logger::log(logger::Type::General, "Serial commands ready: help, nettx HEXFRAME");

        String line;
        line.reserve(128);

        for (;;)
        {
            while (Serial.available() > 0)
            {
                const char c = static_cast<char>(Serial.read());

                if (c == '\r' || c == '\n')
                {
                    line.trim();

                    if (line.length() == 0)
                    {
                        continue;
                    }

                    if (line == "help")
                    {
                        logger::Logger::log(
                            logger::Type::General,
                            "commands: RX, TX, reset, heap, nettx HEXFRAME");
                    }
                    else if (line == "reset")
                    {
                        logger::Logger::log(
                            logger::Type::General,
                            "reset requested");

                        delay(1000);
                        ESP.restart();
                    }
                    else if (line == "RX")
                    {
                        logger::Logger::log(
                            logger::Type::General,
                            "RX LED pulse");

                        gRxLed.pulse(100);
                    }
                    else if (line == "TX")
                    {
                        logger::Logger::log(
                            logger::Type::General,
                            "TX LED pulse");

                        gTxLed.pulse(100);
                    }
                    else if (line == "heap")
                    {
                        logger::Logger::log(
                            logger::Type::General,
                            "heap: free=%u minFree=%u maxAlloc=%u",
                            static_cast<unsigned>(ESP.getFreeHeap()),
                            static_cast<unsigned>(ESP.getMinFreeHeap()),
                            static_cast<unsigned>(ESP.getMaxAllocHeap()));
                    }
                    else if (line.startsWith("nettx "))
                    {
                        const String hex = line.substring(6);

                        uint8_t bytes[config::netbus::kMaxBytesPerFrame]{};
                        size_t byteCount = 0;

                        if (!parseHexFrame(hex, bytes, byteCount, sizeof(bytes)))
                        {
                            logger::Logger::log(logger::Type::General, "invalid hex frame");
                        }
                        else
                        {
                            logger::Logger::log(logger::Type::General, "manual NET TX requested, bytes=%u", static_cast<unsigned>(byteCount));

                            heatpump::NetTxRequest request{};
                            std::memcpy(request.bytes, bytes, byteCount);
                            request.byteCount = static_cast<uint8_t>(byteCount);
                            request.localExpiresAtMs = millis() + 5000U;
                            request.reportResult = false;

                            if (xQueueSend(gNetTxQueue, &request, 0U) != pdTRUE)
                            {
                                logger::Logger::log(logger::Type::General,
                                                    "manual NET TX queue full");
                            }
                        }
                    }
                    else
                    {
                        logger::Logger::log(logger::Type::General, "unknown command: %s", line.c_str());
                    }

                    line = "";
                }
                else if (line.length() < 120)
                {
                    line += c;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

void setup()
{
    logger::Logger::begin(config::kSerialBaud);

    gState.begin();
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
        logger::Logger::log(logger::Type::General, "queue allocation failed; restarting");
        delay(1000);
        ESP.restart();
    }

    xTaskCreatePinnedToCore(netBusOwnerTask, "NetBusOwner", 4096, nullptr, 2, nullptr, 0);
    xTaskCreatePinnedToCore(netProtocolTask, "NetProtocol", 4096, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(currentSensorTask, "Current", 4096, nullptr, 1, nullptr, 1);
    xTaskCreatePinnedToCore(heatPumpControlTask, "HPControl", 4096, nullptr, 1, nullptr, 1);
    xTaskCreatePinnedToCore(communicationTask, "Comms", 4096, nullptr, 1, nullptr, 1);
    xTaskCreatePinnedToCore(serialCommandTask, "SerialCmd", 4096, nullptr, 1, nullptr, 1);

    logger::Logger::log(logger::Type::General, "setup complete; sniffing active; manual NET TX via nettx only");
}

void loop()
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}
