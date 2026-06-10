#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "Core/Types.hpp"
#include "Config/AppConfig.hpp"

namespace heatpump
{
    enum class NetBit : uint8_t
    {
        Zero = 0,
        One = 1,
        Unknown = 255
    };

    struct NetRawFrame
    {
        uint32_t timestampMs = 0;
        uint16_t bitCount = 0;
        uint8_t bytes[config::netbus::kMaxBytesPerFrame]{};
        uint8_t byteCount = 0;
        bool overflow = false;
        bool checksumOk = false;
    };

    class NetBus
    {
    public:
        explicit NetBus(uint8_t pin) : pin_(pin) {}

        void begin();
        bool readLevel() const;

        // Safety-critical methods: this bus must never be driven HIGH.
        void releaseBus() const;
        void pullLow() const;

        // Prepared TX only. Do not call automatically until protocol is validated.
        void sendBitsSafe(const bool* bits, size_t bitCount) const;

        bool sniffFrame(NetRawFrame& outFrame);

    private:
        static bool inRange(uint32_t value, uint32_t min, uint32_t max) noexcept;
        static NetBit highDurationToBit(uint32_t highUs) noexcept;
        static void appendBit(NetRawFrame& frame, bool bit) noexcept;
        uint32_t measureLevelDuration(bool level, uint32_t timeoutUs) const;
        bool waitForLevel(bool level, uint32_t timeoutUs) const;

        uint8_t pin_;
    };
}
