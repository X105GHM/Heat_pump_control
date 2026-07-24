#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstring>
#include "Core/Types.hpp"

namespace heatpump
{
    enum class HeatPumpMode : uint8_t
    {
        Heat = 0,
        Cool = 1,
        Auto = 2,
        Unknown = 255
    };

    struct HeatPumpData
    {
        float waterTemperature = NAN;
        float targetTemperature = NAN;
        float currentRMS = 0.0F;
        bool powerOn = false;
        bool powerStateValid = false;
        bool compressorRunning = false;
        bool errorActive = false;
        uint8_t errorCode = 0;
        HeatPumpMode mode = HeatPumpMode::Unknown;
        float waveform[WF_SAMPLES]{};
        bool currentClipping = false;
        uint32_t lastNetFrameMs = 0;
        uint32_t lastCurrentUpdateMs = 0;
    };

    class HeatPumpState
    {
    public:
        HeatPumpState() = default;

        void begin()
        {
            mutex_ = xSemaphoreCreateMutex();
        }

        void updateFromCurrent(const float currentRms,
                               const float* waveform,
                               const size_t waveformCount,
                               const bool clipping,
                               const bool compressorRunning)
        {
            if (!lock()) return;
            data_.currentRMS = currentRms;
            data_.currentClipping = clipping;
            data_.lastCurrentUpdateMs = millis();
            data_.compressorRunning = compressorRunning;
            const size_t n = waveformCount > WF_SAMPLES ? WF_SAMPLES : waveformCount;
            for (size_t i = 0; i < n; ++i) data_.waveform[i] = waveform[i];
            for (size_t i = n; i < WF_SAMPLES; ++i) data_.waveform[i] = 0.0F;
            unlock();
        }

        void markNetFrameReceived()
        {
            if (!lock()) 
            {
                return;
            }

            data_.lastNetFrameMs = millis();

            unlock();
        }

        void updateFromDecoded(const HeatPumpData& partial)
        {
            if (!lock()) return;
            if (!isnan(partial.waterTemperature)) data_.waterTemperature = partial.waterTemperature;
            if (!isnan(partial.targetTemperature)) data_.targetTemperature = partial.targetTemperature;
            if (partial.mode != HeatPumpMode::Unknown) data_.mode = partial.mode;
            if (partial.powerStateValid) {data_.powerOn = partial.powerOn; data_.powerStateValid = true;}
            data_.errorActive = partial.errorActive;
            data_.errorCode = partial.errorCode;
            data_.lastNetFrameMs = millis();
            unlock();
        }

        HeatPumpData snapshot() const
        {
            HeatPumpData copy{};
            if (mutex_ != nullptr && xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) == pdTRUE) 
            {
                copy = data_;
                xSemaphoreGive(mutex_);
            }
            return copy;
        }

    private:
        bool lock() const
        {
            return mutex_ != nullptr && xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) == pdTRUE;
        }

        void unlock() const
        {
            xSemaphoreGive(mutex_);
        }

        mutable SemaphoreHandle_t mutex_{nullptr};
        HeatPumpData data_{};
    };
}
