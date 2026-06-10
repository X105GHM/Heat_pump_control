#pragma once
#include <cstdint>
#include <cstddef>

namespace config
{
    static constexpr uint32_t kSerialBaud = 115200;

    namespace netbus
    {
        // Timing is intentionally configurable because PC1001/PC1002-like buses
        // can differ depending on display board, level shifter and polarity.
        static constexpr uint32_t kHeaderLowMinUs  = 7000;
        static constexpr uint32_t kHeaderLowMaxUs  = 12000;
        static constexpr uint32_t kHeaderHighMinUs = 4000;
        static constexpr uint32_t kHeaderHighMaxUs = 6500;

        static constexpr uint32_t kBitHighZeroMinUs = 600;
        static constexpr uint32_t kBitHighZeroMaxUs = 1700;
        static constexpr uint32_t kBitHighOneMinUs  = 2200;
        static constexpr uint32_t kBitHighOneMaxUs  = 4200;

        static constexpr uint32_t kFrameGapUs       = 12000;
        static constexpr uint32_t kMaxPulseUs       = 30000;
        static constexpr uint32_t kTxLowUs          = 1000;
        static constexpr uint32_t kTxHighZeroUs     = 1000;
        static constexpr uint32_t kTxHighOneUs      = 3000;
        static constexpr size_t   kMaxBitsPerFrame  = 128;
        static constexpr size_t   kMaxBytesPerFrame = 16;
    }

    namespace current
    {
        static constexpr uint16_t kSampleCount = 256;
        static constexpr uint32_t kSampleRateHz = 2000;
        static constexpr float kAdcReferenceVoltage = 3.3F;
        static constexpr uint16_t kAdcMaxRaw = 4095;

        // SCT-013-030: 30 A -> 1 V RMS at ADC conditioning output.
        static constexpr float kAmpsPerVoltRms = 30.0F;

        static constexpr uint16_t kClipLowRaw = 8;
        static constexpr uint16_t kClipHighRaw = kAdcMaxRaw - 8;
        static constexpr uint32_t kMeasurementPeriodMs = 500;
    }

    namespace control
    {
        static constexpr uint32_t kMinRunTimeMs = 180000;      // 3 min placeholder
        static constexpr uint32_t kMinOffTimeMs = 180000;      // 3 min placeholder
        static constexpr float kCompressorCurrentThresholdA = 0.7F;
    }
}
