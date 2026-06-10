#pragma once
#include <Arduino.h>

namespace led
{
    class StatusLed
    {
    public:
        explicit StatusLed(uint8_t pin) : pin_(pin) {}
        void begin() const;
        void set(bool on) const;
        void pulse(uint16_t ms = 30) const;

    private:
        uint8_t pin_;
    };
}
