#include "HeatPump/NetBus.hpp"

#include "Logger/Logger.hpp"

#include <driver/gpio.h>

namespace heatpump
{
    NetBus::NetBus(const uint8_t pin) noexcept : pin_(pin){}

    void NetBus::begin()
    {
        releaseBus();

        pulseQueue_ = xQueueCreate(config::netbus::kPulseQueueLength, sizeof(Pulse));

        if (pulseQueue_ == nullptr) 
        {
            logger::Logger::log(logger::Type::NetBus,"NET pulse queue allocation failed");

            return;
        }

        lastLevel_ = readLevelFast();
        lastEdgeUs_ = micros();

        attachInterruptArg(digitalPinToInterrupt(pin_), &NetBus::isrThunk, this, CHANGE);

        logger::Logger::log(logger::Type::NetBus, "NET bus initialized on GPIO%u with CHANGE interrupt", static_cast<unsigned>(pin_));
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

    bool NetBus::readLevelFast() const
    {
        return gpio_get_level(static_cast<gpio_num_t>(pin_)) != 0;
    }

    void IRAM_ATTR NetBus::isrThunk(void* arg)
    {
        if (arg == nullptr) 
        {
            return;
        }

        static_cast<NetBus*>(arg)->handleInterrupt();
    }

    void IRAM_ATTR NetBus::handleInterrupt()
    {
        const uint32_t nowUs = micros();
        const bool newLevel = readLevelFast();

        const Pulse pulse{lastLevel_, nowUs - lastEdgeUs_};

        lastLevel_ = newLevel;
        lastEdgeUs_ = nowUs;

        if (pulseQueue_ == nullptr) 
        {
            return;
        }

        BaseType_t higherPriorityTaskWoken = pdFALSE;

        if (xQueueSendFromISR(pulseQueue_, &pulse,&higherPriorityTaskWoken) != pdTRUE) 
        {   
            ++droppedPulses_;
        }

        if (higherPriorityTaskWoken == pdTRUE) 
        {
            portYIELD_FROM_ISR();
        }
    }

    bool NetBus::popPulse(Pulse& pulse, const TickType_t timeoutTicks)
    {
        if (pulseQueue_ == nullptr) 
        {
            return false;
        }

        return xQueueReceive(pulseQueue_, &pulse, timeoutTicks) == pdTRUE;
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

        const uint8_t bitIndex = 7U - static_cast<uint8_t>(frame.bitCount % 8);

        if (byteIndex < config::netbus::kMaxBytesPerFrame) 
        {
            if (bit) 
            {
                frame.bytes[byteIndex] |=
                    static_cast<uint8_t>(1U << bitIndex);
            }
        } 
        else 
        {
            frame.overflow = true;
        }

        ++frame.bitCount;

        frame.byteCount = static_cast<uint8_t>((frame.bitCount + 7U) / 8U);
    }

    bool NetBus::waitForHeader(NetRawFrame& frame)
    {
        Pulse pulse{};

        for (;;) 
        {
            if (!popPulse(pulse, pdMS_TO_TICKS(1000))) 
            {
                return false;
            }

            if (!pulse.level && inRange(pulse.durationUs, config::netbus::kHeaderLowMinUs, config::netbus::kHeaderLowMaxUs)) 
            {
                Pulse headerHigh{};
       
                if (!popPulse(headerHigh, pdMS_TO_TICKS(config::netbus::kFrameIdleTimeoutMs))) 
                {
                    return false;
                }

                if (headerHigh.level && inRange(headerHigh.durationUs, config::netbus::kHeaderHighMinUs, config::netbus::kHeaderHighMaxUs)) 
                {
                    frame.timestampMs = millis();
                    return true;
                }
            }
        }
    }

    bool NetBus::readBitAfterHeader(NetRawFrame& frame)
    {
        Pulse lowPulse{};

        if (!popPulse(lowPulse, pdMS_TO_TICKS(config::netbus::kFrameIdleTimeoutMs))) 
        {
            return false;
        }

        if (lowPulse.level && lowPulse.durationUs >= config::netbus::kFrameGapUs) 
        {
            return false;
        }

        if (lowPulse.level) 
        {
            return false;
        }

        if (!inRange(lowPulse.durationUs, config::netbus::kBitLowMinUs, config::netbus::kBitLowMaxUs)) 
        {

            return false;
        }

        Pulse highPulse{};

        if (!popPulse(highPulse, pdMS_TO_TICKS(config::netbus::kFrameIdleTimeoutMs))) 
        {
            return false;
        }

        if (!highPulse.level) 
        {
            return false;
        }

        if (highPulse.durationUs >= config::netbus::kFrameGapUs) 
        {
            return false;
        }

        const NetBit bit = highDurationToBit(highPulse.durationUs);

        if (bit == NetBit::Unknown) 
        {
            logger::Logger::log(logger::Type::NetBus, "unknown HIGH pulse: %lu us", static_cast<unsigned long>(highPulse.durationUs));

            return false;
        }

        appendBit(frame, bit == NetBit::One);
        return true;
    }

    bool NetBus::sniffFrame(NetRawFrame& outFrame)
    {        outFrame = NetRawFrame{};

        if (pulseQueue_ == nullptr) 
        {
            return false;
        }

        if (!waitForHeader(outFrame)) 
        {
            return false;
        }

        while (outFrame.bitCount < config::netbus::kMaxBitsPerFrame) 
        {
            if (!readBitAfterHeader(outFrame)) 
            {
                break;
            }
        }

        return outFrame.bitCount >= 8;
    }

    void NetBus::sendBitsSafe(
        const bool* bits,
        const size_t bitCount) const
    {
        if (bits == nullptr || bitCount == 0) {
            return;
        }

        detachInterrupt(digitalPinToInterrupt(pin_));
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

        const_cast<NetBus*>(this)->lastLevel_ = const_cast<NetBus*>(this)->readLevelFast();

        const_cast<NetBus*>(this)->lastEdgeUs_ = micros();

        attachInterruptArg(digitalPinToInterrupt(pin_), &NetBus::isrThunk, const_cast<NetBus*>(this), CHANGE);
    }
}