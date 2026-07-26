#include "Led/ErrorLed.hpp"
#include "Config/AppConfig.hpp"

namespace led
{
    ErrorLed::ErrorLed(const uint8_t pin, const uint16_t count, const uint8_t brightness)
        : strip_(count, pin, NEO_GRB + NEO_KHZ800)
    {
        strip_.setBrightness(brightness);
    }

    void ErrorLed::begin()
    {
        strip_.begin();
        strip_.clear();
        strip_.show();
        restartPattern();
    }

    ErrorLed::Pattern ErrorLed::patternFor(const Fault fault, const uint8_t detailCode)
    {
        using namespace config::errorLed;
        switch (fault) {
            case Fault::NoNetConnection:
                return {0, 0, 255, 2, kPulseOnMs, kPulseOffMs, kPatternPauseMs};
            case Fault::NoCurrentWhileOn:
                return {255, 120, 0, 3, kPulseOnMs, kPulseOffMs, kPatternPauseMs};
            case Fault::CurrentSensorStale:
                return {0, 255, 255, 2, 350, 300, 1200};
            case Fault::AdcClipping:
                return {255, 0, 255, 5, 80, 100, 1000};
            case Fault::HeatPumpReportedError: {
                // Error code 0 still produces four red flashes. Codes 1..8 are shown directly.
                const uint8_t pulses = (detailCode >= 1 && detailCode <= 8) ? detailCode : 4;
                return {255, 0, 0, pulses, kPulseOnMs, kPulseOffMs, 1800};
            }
            case Fault::None:
            default:
                return {0, 0, 0, 0, 0, 0, 0};
        }
    }

    void ErrorLed::setFault(const Fault fault, const uint8_t detailCode)
    {
        if (fault_ == fault && detailCode_ == detailCode) return;
        fault_ = fault;
        detailCode_ = detailCode;
        pattern_ = patternFor(fault_, detailCode_);
        restartPattern();
    }

    void ErrorLed::restartPattern()
    {
        completedPulses_ = 0;
        pixelOn_ = false;
        setPixel(false);
        nextTransitionMs_ = millis();
    }

    void ErrorLed::setPixel(const bool on)
    {
        pixelOn_ = on;
        const uint32_t color = on ? strip_.Color(pattern_.red, pattern_.green, pattern_.blue) : 0;
        strip_.setPixelColor(0, color);
        strip_.show();
    }

    void ErrorLed::update()
    {
        if (fault_ == Fault::None || pattern_.pulses == 0) 
        {
            if (pixelOn_) setPixel(false);
            return;
        }

        const uint32_t now = millis();
        if (static_cast<int32_t>(now - nextTransitionMs_) < 0) return;

        if (pixelOn_) 
        {
            setPixel(false);
            ++completedPulses_;
            nextTransitionMs_ = now + ((completedPulses_ >= pattern_.pulses) ? pattern_.pauseMs : pattern_.offMs);
            if (completedPulses_ >= pattern_.pulses) completedPulses_ = 0;
        } 
        else 
        {
            setPixel(true);
            nextTransitionMs_ = now + pattern_.onMs;
        }
    }
}
