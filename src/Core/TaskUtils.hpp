#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace core
{
    inline void delayMs(const uint32_t ms) noexcept
    {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }

    inline uint32_t nowMs() noexcept
    {
        return millis();
    }
}
