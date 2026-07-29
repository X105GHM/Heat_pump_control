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

    private:
        uint8_t pin_;
    };
}
