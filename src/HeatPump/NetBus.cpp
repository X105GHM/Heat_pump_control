#include "HeatPump/NetBus.hpp"
#include "Logger/Logger.hpp"

namespace heatpump
{
    void NetBus::begin()
    {
        releaseBus();
        logger::Logger::log(logger::Type::NetBus, "NET bus initialized on GPIO%u as high-Z input", static_cast<unsigned>(pin_));
    }

    void NetBus::releaseBus() const
    {
        pinMode(pin_, INPUT);
    }

    void NetBus::pullLow() const
    {
        digitalWrite(pin_, LOW);
        pinMode(pin_, OUTPUT);
    }

    bool NetBus::readLevel() const
    {
        return digitalRead(pin_) == HIGH;
    }

    bool NetBus::inRange(const uint32_t value, const uint32_t min, const uint32_t max) noexcept
    {
        return value >= min && value <= max;
    }

    NetBit NetBus::highDurationToBit(const uint32_t highUs) noexcept
    {
        const bool isShort = inRange(highUs, config::netbus::kShortHighMinUs, config::netbus::kShortHighMaxUs);

        const bool isLong = inRange(highUs, config::netbus::kLongHighMinUs, config::netbus::kLongHighMaxUs);

        if (isShort) 
        {
            return config::netbus::kLongHighMeansOne ? NetBit::Zero : NetBit::One;
        }

        if (isLong) 
        {
            return config::netbus::kLongHighMeansOne ? NetBit::One : NetBit::Zero;
        }

        return NetBit::Unknown;
    }

    void NetBus::appendBit(NetRawFrame& frame, const bool bit) noexcept
    {
        if (frame.bitCount >= config::netbus::kMaxBitsPerFrame) 
        {
            frame.overflow = true;
            return;
        }

        const uint16_t byteIndex = frame.bitCount / 8;
        const uint8_t bitIndex = static_cast<uint8_t>(frame.bitCount % 8U); // LSB first

        if (byteIndex < config::netbus::kMaxBytesPerFrame) 
        {
            if (bit) 
            {
                frame.bytes[byteIndex] |= static_cast<uint8_t>(1U << bitIndex);
            }
        } 
        else 
        {
            frame.overflow = true;
        }

        ++frame.bitCount;
        frame.byteCount = static_cast<uint8_t>((frame.bitCount + 7U) / 8U);
    }

    bool NetBus::waitForLevel(const bool level, const uint32_t timeoutUs) const
    {
        const uint32_t start = micros();

        while ((micros() - start) < timeoutUs) 
        {
            if (readLevel() == level) 
            {
                return true;
            }

            delayMicroseconds(20);
        }

        return false;
    }

    uint32_t NetBus::measureLevelDuration(const bool level, const uint32_t timeoutUs) const
    {
        const uint32_t start = micros();

        while ((micros() - start) < timeoutUs && readLevel() == level) 
        {
            delayMicroseconds(10);
        }

        return micros() - start;
    }

    bool NetBus::sniffFrame(NetRawFrame& outFrame)
    {
        releaseBus();
        outFrame = NetRawFrame{};

        if (!waitForLevel(false, config::netbus::kMaxPulseUs)) 
        {
            return false;
        }

        const uint32_t lowUs = measureLevelDuration(false, config::netbus::kMaxPulseUs);
        if (!inRange(lowUs, config::netbus::kHeaderLowMinUs, config::netbus::kHeaderLowMaxUs)) 
        {
            return false;
        }

        const uint32_t highUs = measureLevelDuration(true, config::netbus::kMaxPulseUs);
        if (!inRange(highUs, config::netbus::kHeaderHighMinUs, config::netbus::kHeaderHighMaxUs)) 
        {
            return false;
        }

        outFrame.timestampMs = millis();

        while (outFrame.bitCount < config::netbus::kMaxBitsPerFrame) 
        {
            if (!waitForLevel(false, config::netbus::kFrameGapUs)) 
            {
                break;
            }

            const uint32_t bitLowUs = measureLevelDuration(false, config::netbus::kMaxPulseUs);

            if (!inRange(bitLowUs, config::netbus::kBitLowMinUs, config::netbus::kBitLowMaxUs)) 
            {
                logger::Logger::log(logger::Type::NetBus, "bit LOW out of range: %lu us", static_cast<unsigned long>(bitLowUs));
                break;
            }

            const uint32_t bitHighUs = measureLevelDuration(true, config::netbus::kMaxPulseUs);
            if (bitHighUs >= config::netbus::kFrameGapUs) 
            {
                break;
            }

            const NetBit bit = highDurationToBit(bitHighUs);
            if (bit == NetBit::Unknown) 
            {
                logger::Logger::log(logger::Type::NetBus, "unknown HIGH pulse: %lu us", static_cast<unsigned long>(bitHighUs));
                break;
            }

            appendBit(outFrame, bit == NetBit::One);
        }

        return outFrame.bitCount >= 8;
    }

    bool NetBus::sendBitsSafe(const bool* bits, const size_t bitCount) const
    {
        if (bits == nullptr || bitCount == 0) 
        {
            return false;
        }

        releaseBus();

        if (!waitForIdleHigh(config::netbus::kFrameGapUs, 100000))
        {
            logger::Logger::log(logger::Type::NetBus, "TX aborted: bus not idle");
            return false;
        }

        pullLow();
        delayMicroseconds(9000);
        releaseBus();
        delayMicroseconds(5000);

        for (size_t i = 0; i < bitCount; ++i) 
        {
            pullLow();
            delayMicroseconds(config::netbus::kTxLowUs);
            releaseBus();
            delayMicroseconds(bits[i] ? config::netbus::kTxHighOneUs : config::netbus::kTxHighZeroUs);
        }

        releaseBus();
        return true;
    }

    bool NetBus::sendBytesSafe(const uint8_t* bytes, const size_t byteCount) const
    {
        if (bytes == nullptr || byteCount == 0) 
        {
            return false;
        }

        if (byteCount != config::netbus::kShortFrameBytes && byteCount != config::netbus::kLongFrameBytes) 
        {
            logger::Logger::log(logger::Type::NetBus, "TX rejected: expected 9 or 12 bytes, got %u", static_cast<unsigned>(byteCount));
            return false;
        }

        bool bits[config::netbus::kMaxBitsPerFrame]{};
        size_t bitCount = 0;

        for (size_t byteIndex = 0; byteIndex < byteCount; ++byteIndex) 
        {
            const uint8_t value = bytes[byteIndex];

            for (uint8_t bit = 0; bit < 8; ++bit) 
            {
                bits[bitCount++] = (value & (1U << bit)) != 0; // LSB first
            }
        }

        logger::Logger::log(logger::Type::NetBus,
                            "TX raw frame bytes=%u bits=%u zeroHigh=%luus oneHigh=%luus",
                            static_cast<unsigned>(byteCount),
                            static_cast<unsigned>(bitCount),
                            static_cast<unsigned long>(config::netbus::kTxHighZeroUs),
                            static_cast<unsigned long>(config::netbus::kTxHighOneUs));

        return sendBitsSafe(bits, bitCount);
    }

    bool NetBus::waitForIdleHigh(const uint32_t idleUs, const uint32_t timeoutUs) const
    {
        const uint32_t start = micros();
        uint32_t highStart = 0;

        while ((micros() - start) < timeoutUs)
        {
            if (readLevel())
            {
                if (highStart == 0)
                {
                    highStart = micros();
                }

                if ((micros() - highStart) >= idleUs)
                {
                    return true;
                }
            }
            else
            {
                highStart = 0;
            }

            delayMicroseconds(50);
        }

        return false;
    }
}
