#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <cstddef>
#include <cstdint>
#include "Config/AppConfig.hpp"

namespace heatpump
{
    enum class NetBit : uint8_t
    {
        Zero,
        One,
        Unknown
    };

    struct NetRawFrame
    {
        uint8_t bytes[config::netbus::kMaxBytesPerFrame]{};
        uint16_t bitCount = 0;
        uint8_t byteCount = 0;
        bool overflow = false;
        uint32_t timestampMs = 0;
    };

    class NetBus
    {
    public:
        explicit NetBus(uint8_t pin) noexcept;

        void begin();

        bool sniffFrame(NetRawFrame& outFrame);

        void sendBitsSafe(const bool* bits, size_t bitCount) const;

        void releaseBus() const;
        void pullLow() const;

    private:
        struct Pulse
        {
            bool level;
            uint32_t durationUs;
        };

        static void IRAM_ATTR isrThunk(void* arg);
        void IRAM_ATTR handleInterrupt();

        [[nodiscard]] bool readLevelFast() const;
        [[nodiscard]] bool popPulse(Pulse& pulse, TickType_t timeoutTicks);

        static bool inRange(uint32_t value, uint32_t min, uint32_t max) noexcept;
        static NetBit highDurationToBit(uint32_t highUs) noexcept;

        static void appendBit(NetRawFrame& frame, bool bit) noexcept;

        bool waitForHeader(NetRawFrame& frame);
        bool readBitAfterHeader(NetRawFrame& frame);

        uint8_t pin_{0};

        QueueHandle_t pulseQueue_{nullptr};

        volatile bool lastLevel_{true};
        volatile uint32_t lastEdgeUs_{0};
        volatile uint32_t droppedPulses_{0};
    };
}