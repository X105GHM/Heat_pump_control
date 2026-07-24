#include "CurrentSensor/CurrentSensor.hpp"
#include "Config/AppConfig.hpp"
#include "Logger/Logger.hpp"
#include <cmath>

namespace current
{
    void CurrentSensor::begin()
    {
        analogReadResolution(12);
        analogSetPinAttenuation(adcPin_, ADC_11db); // suitable for roughly 0..3.1 V on many ESP32 variants
        (void)analogRead(adcPin_);
        logger::Logger::log(logger::Type::Current, "Current ADC initialized on GPIO%u", adcPin_);
    }

    float CurrentSensor::rawToVoltage(const uint16_t raw) const
    {
        return (static_cast<float>(raw) / static_cast<float>(config::current::kAdcMaxRaw)) * config::current::kAdcReferenceVoltage;
    }

    CurrentMeasurement CurrentSensor::measure()
    {
        uint16_t raw[config::current::kSampleCount]{};
        uint32_t sumRaw = 0;
        bool clipping = false;

        const uint32_t samplePeriodUs = 1000000UL / config::current::kSampleRateHz;
        uint32_t nextSample = micros();

        for (uint16_t i = 0; i < config::current::kSampleCount; ++i) 
        {
            while (static_cast<int32_t>(micros() - nextSample) < 0) 
            {
                delayMicroseconds(5);
            }
            nextSample += samplePeriodUs;

            const uint16_t value = static_cast<uint16_t>(analogRead(adcPin_));
            raw[i] = value;
            sumRaw += value;
            if (value <= config::current::kClipLowRaw || value >= config::current::kClipHighRaw) 
            {
                clipping = true;
            }
        }

        const float meanRaw = static_cast<float>(sumRaw) / static_cast<float>(config::current::kSampleCount);
        const float meanVoltage = rawToVoltage(static_cast<uint16_t>(meanRaw));

        float squareSum = 0.0F;
        for (uint16_t i = 0; i < config::current::kSampleCount; ++i) 
        {
            const float voltage = rawToVoltage(raw[i]);
            const float ac = voltage - meanVoltage;
            squareSum += ac * ac;
        }

        CurrentMeasurement result{};
        result.adcMeanVoltage = meanVoltage;
        result.voltageRmsAc = sqrtf(squareSum / static_cast<float>(config::current::kSampleCount));
        result.currentRms = result.voltageRmsAc * config::current::kAmpsPerVoltRms;

        if (result.currentRms < config::current::kNoiseFloorA)
        {
            result.currentRms = 0.0F;
        }

        result.clipping = clipping;

        // Downsample to waveform buffer, preserving voltage AC component in volts.
        for (size_t i = 0; i < WF_SAMPLES; ++i) 
        {
            const size_t src = (i * config::current::kSampleCount) / WF_SAMPLES;
            result.waveform[i] = rawToVoltage(raw[src]) - meanVoltage;
        }

        return result;
    }
}
