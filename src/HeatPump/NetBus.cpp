#include "HeatPump/NetBus.hpp"
#include "Logger/Logger.hpp"

namespace heatpump
{
    void NetBus::begin()
    {
        releaseBus();
        logger::Logger::log(logger::Type::NetBus, "NET bus initialized on GPIO%u as high-Z input", pin_);
    }

    void NetBus::releaseBus() const
    {
        pinMode(pin_, INPUT); // high impedance, external pull-up / level shifter releases line
    }

    void NetBus::pullLow() const
    {
        digitalWrite(pin_, LOW);
        pinMode(pin_, OUTPUT); // never OUTPUT HIGH
    }

    bool NetBus::readLevel() const
    {
        pinMode(pin_, INPUT);
        return digitalRead(pin_) == HIGH;
    }

    bool NetBus::inRange(const uint32_t value, const uint32_t min, const uint32_t max) noexcept
    {
        return value >= min && value <= max;
    }

    NetBit NetBus::highDurationToBit(const uint32_t highUs) noexcept
    {
        if (inRange(highUs, config::netbus::kBitHighZeroMinUs, config::netbus::kBitHighZeroMaxUs)) 
        {
            return NetBit::Zero;
        }
        if (inRange(highUs, config::netbus::kBitHighOneMinUs, config::netbus::kBitHighOneMaxUs)) 
        {
            return NetBit::One;
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
        const uint8_t bitIndex = 7 - (frame.bitCount % 8); // MSB first placeholder
        if (byteIndex < config::netbus::kMaxBytesPerFrame) 
        {
            if (bit) frame.bytes[byteIndex] |= (1U << bitIndex);
        }
        frame.bitCount++;
        frame.byteCount = (frame.bitCount + 7U) / 8U;
    }

    bool NetBus::waitForLevel(const bool level, const uint32_t timeoutUs) const
    {
        const uint32_t start = micros();
        while ((micros() - start) < timeoutUs) 
        {
            if (readLevel() == level) return true;
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

            (void)bitLowUs; // Usually around 1 ms. Keep for future validation.

            const uint32_t bitHighUs = measureLevelDuration(true, config::netbus::kMaxPulseUs);

            if (bitHighUs >= config::netbus::kFrameGapUs) 
            {
                break;
            }

            const NetBit bit = highDurationToBit(bitHighUs);
            if (bit == NetBit::Unknown) 
            {
                // Unknown pulse means either noise, changed timing, or wrong polarity.
                // End current candidate frame and let protocol layer log raw bytes.
                break;
            }
            appendBit(outFrame, bit == NetBit::One);
        }

        return outFrame.bitCount > 0;
    }

    void NetBus::sendBitsSafe(const bool* bits, const size_t bitCount) const
    {
        if (bits == nullptr || bitCount == 0) return;

        // Header-like wake/start pulse. Validate with logic analyzer before using on hardware.
        pullLow();
        delayMicroseconds(9000);
        releaseBus();
        delayMicroseconds(5000);

        for (size_t i = 0; i < bitCount; ++i) {
            pullLow();
            delayMicroseconds(config::netbus::kTxLowUs);
            releaseBus();
            delayMicroseconds(bits[i] ? config::netbus::kTxHighOneUs : config::netbus::kTxHighZeroUs);
        }

        releaseBus();
    }
}
