#pragma once
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <cstdint>

namespace led
{
    enum class Fault : uint8_t
    {
        None = 0,
        NoNetConnection,
        NoCurrentWhileOn,
        CurrentSensorStale,
        AdcClipping,
        HeatPumpReportedError
    };

    class ErrorLed
    {
    public:
        ErrorLed(uint8_t pin, uint16_t count, uint8_t brightness);

        void begin();
        void setFault(Fault fault, uint8_t detailCode = 0);
        void update();
        Fault fault() const noexcept { return fault_; }

    private:
        struct Pattern
        {
            uint8_t red;
            uint8_t green;
            uint8_t blue;
            uint8_t pulses;
            uint16_t onMs;
            uint16_t offMs;
            uint16_t pauseMs;
        };

        static Pattern patternFor(Fault fault, uint8_t detailCode);
        void setPixel(bool on);
        void restartPattern();

        Adafruit_NeoPixel strip_;
        Fault fault_{Fault::None};
        uint8_t detailCode_{0};
        Pattern pattern_{};
        uint32_t nextTransitionMs_{0};
        uint8_t completedPulses_{0};
        bool pixelOn_{false};
    };
}
