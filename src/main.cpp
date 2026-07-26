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

    void netBusRxTask(void *)
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
                gState.markNetFrameReceived();

                heatpump::HeatPumpData decoded{};

                if (protocol.decode(frame, decoded))
                {
                    gState.updateFromDecoded(decoded);
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

            logger::Logger::log(logger::Type::Current,
                                "Irms=%.2fA VrmsAC=%.3fV mean=%.3fV clipping=%u",
                                measurement.currentRms,
                                measurement.voltageRmsAc,
                                measurement.adcMeanVoltage,
                                measurement.clipping ? 1 : 0);

            core::delayMs(config::current::kMeasurementPeriodMs);
        }
    }

    void heatPumpControlTask(void *)
    {
        heatpump::HeatPumpController controller(gState, gNetBus, gCommandQueue);
        controller.begin();

        for (;;)
        {
            controller.processControl();
            core::delayMs(100);
        }
    }

    void communicationTask(void *)
    {
        communication::EspNowBridge bridge(gState, gCommandQueue);
        bridge.begin();

        for (;;)
        {
            bridge.loopOnce();
            core::delayMs(200);
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

                        if (!parseHexFrame(
                                hex,
                                bytes,
                                byteCount,
                                sizeof(bytes)))
                        {
                            logger::Logger::log(
                                logger::Type::General,
                                "invalid hex frame");
                        }
                        else
                        {
                            logger::Logger::log(
                                logger::Type::General,
                                "manual NET TX requested, bytes=%u",
                                static_cast<unsigned>(byteCount));

                            gTxLed.pulse(50);
                            gNetBus.sendBytesSafe(bytes, byteCount);
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
            core::delayMs(20);
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

    xTaskCreatePinnedToCore(netBusRxTask, "NetBusRx", 4096, nullptr, 2, nullptr, 0);
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
