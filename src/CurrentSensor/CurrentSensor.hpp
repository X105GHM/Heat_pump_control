#pragma once
#include <Arduino.h>
#include "Core/Types.hpp"

namespace current
{
    struct CurrentMeasurement
    {
        float currentRms = 0.0F;
        float voltageRmsAc = 0.0F;
        float adcMeanVoltage = 0.0F;
        bool clipping = false;
        float waveform[WF_SAMPLES]{};
        size_t waveformCount = WF_SAMPLES;
    };

    class CurrentSensor
    {
    public:
        explicit CurrentSensor(uint8_t adcPin) : adcPin_(adcPin) {}
        void begin();
        CurrentMeasurement measure();

    private:
        float rawToVoltage(uint16_t raw) const;
        uint8_t adcPin_;
    };
}
