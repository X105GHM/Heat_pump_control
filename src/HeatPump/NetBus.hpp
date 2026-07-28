#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "Core/Types.hpp"
#include "Config/AppConfig.hpp"
#include "HeatPump/HeatPumpCommand.hpp"
#include "HeatPump/NetFrame.hpp"

namespace heatpump
{
    enum class NetBit : uint8_t
    {
        Zero = 0,
        One = 1,
        Unknown = 255
    };

    enum class NetTxStatus : uint8_t
    {
        Sent,
        Failed,
        Expired
    };

    struct NetTxRequest
    {
        uint8_t bytes[config::netbus::kMaxBytesPerFrame]{};
        uint8_t byteCount = 0U;
        uint32_t commandId = 0U;
        HeatPumpCommandType commandType = HeatPumpCommandType::RequestStatus;
        uint32_t localExpiresAtMs = 0U;
        bool reportResult = false;
    };

    struct NetTxResult
    {
        uint32_t commandId = 0U;
        HeatPumpCommandType commandType = HeatPumpCommandType::RequestStatus;
        uint32_t completedAtMs = 0U;
        NetTxStatus status = NetTxStatus::Failed;
    };

    class NetBus
    {
    public:
        explicit NetBus(uint8_t pin) : pin_(pin) {}

        void begin();
        bool readLevel() const;

        void releaseBus() const;
        void pullLow() const;

        bool sendBitsSafe(const bool* bits, size_t bitCount) const;

        bool sendBytesSafe(const uint8_t* bytes, size_t byteCount) const;

        bool sniffFrame(NetRawFrame& outFrame);

        bool waitForIdleHigh(uint32_t idleUs, uint32_t timeoutUs) const;

    private:
        static bool inRange(uint32_t value, uint32_t min, uint32_t max) noexcept;
        static NetBit highDurationToBit(uint32_t highUs) noexcept;
        static void appendBit(NetRawFrame& frame, bool bit) noexcept;
        uint32_t measureLevelDuration(bool level, uint32_t timeoutUs) const;
        bool waitForLevel(bool level, uint32_t timeoutUs) const;

        uint8_t pin_;
    };
}
