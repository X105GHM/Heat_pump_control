#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "HeatPump/HeatPumpData.hpp"

namespace heatpump
{
    class HeatPumpState
    {
    public:
        HeatPumpState() = default;

        bool begin()
        {
            mutex_ = xSemaphoreCreateMutex();
            return mutex_ != nullptr;
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
            data_.compressorRunning = compressorRunning;
            data_.lastCurrentUpdateMs = millis();

            const size_t n = waveformCount > WF_SAMPLES ? WF_SAMPLES : waveformCount;
            for (size_t i = 0; i < n; ++i) data_.waveform[i] = waveform[i];
            for (size_t i = n; i < WF_SAMPLES; ++i) data_.waveform[i] = 0.0F;

            unlock();
        }

        void markNetFrameReceived(const uint32_t receivedAtMs)
        {
            if (!lock()) return;
            data_.lastNetFrameMs = receivedAtMs;
            unlock();
        }

        void updateFromDecoded(const HeatPumpData& partial, const uint32_t receivedAtMs)
        {
            if (!lock()) return;
            mergeDecodedData(data_, partial, receivedAtMs);
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
