#include "Led/StatusLed.hpp"

namespace led
{
    void StatusLed::begin() const
    {
        pinMode(pin_, OUTPUT);
        digitalWrite(pin_, LOW);
    }

    void StatusLed::set(const bool on) const
    {
        digitalWrite(pin_, on ? HIGH : LOW);
    }

    void StatusLed::pulse(const uint16_t ms) const
    {
        digitalWrite(pin_, HIGH);
        delay(ms);
        digitalWrite(pin_, LOW);
    }
}
