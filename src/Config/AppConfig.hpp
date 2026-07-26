#pragma once
#include <cstdint>
#include <cstddef>

namespace config
{
    static constexpr uint32_t kSerialBaud = 115200;

    namespace netbus
    {
        // PC1001/PC1002-like single-wire pulse bus.
        // Header: LOW ~9 ms, HIGH ~5 ms
        // Bit:    LOW ~1 ms, HIGH ~1 ms or ~3 ms
        static constexpr uint32_t kHeaderLowMinUs  = 7000;
        static constexpr uint32_t kHeaderLowMaxUs  = 12000;
        static constexpr uint32_t kHeaderHighMinUs = 4000;
        static constexpr uint32_t kHeaderHighMaxUs = 6500;

        static constexpr uint32_t kBitLowMinUs = 500;
        static constexpr uint32_t kBitLowMaxUs = 1800;

        static constexpr uint32_t kShortHighMinUs = 600;
        static constexpr uint32_t kShortHighMaxUs = 1700;
        static constexpr uint32_t kLongHighMinUs  = 2200;
        static constexpr uint32_t kLongHighMaxUs  = 4200;

        // Common PC1001 captures: HIGH ~1 ms = 1, HIGH ~3 ms = 0.
        // If your captured raw frames are inverted, change only this flag.
        static constexpr bool kLongHighMeansOne = false;

        static constexpr uint32_t kFrameGapUs       = 12000;
        static constexpr uint32_t kMaxPulseUs       = 30000;
        static constexpr uint32_t kTxLowUs          = 1000;

        // Keep TX automatically consistent with RX bit mapping.
        static constexpr uint32_t kTxHighZeroUs = kLongHighMeansOne ? 1000 : 3000;
        static constexpr uint32_t kTxHighOneUs  = kLongHighMeansOne ? 3000 : 1000;

        static constexpr size_t   kShortFrameBytes = 9;
        static constexpr size_t   kLongFrameBytes  = 12;
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
        static constexpr float kNoiseFloorA = 0.25F;

        static constexpr uint16_t kClipLowRaw = 8;
        static constexpr uint16_t kClipHighRaw = kAdcMaxRaw - 8;
        static constexpr uint32_t kMeasurementPeriodMs = 500;
    }

    namespace control
    {
        static constexpr uint32_t kMinRunTimeMs = 180000;      // 3 min placeholder
        static constexpr uint32_t kMinOffTimeMs = 180000;      // 3 min placeholder
        static constexpr float kCompressorCurrentThresholdA = 0.7F;

        // Fault monitoring. Values are deliberately conservative during reverse engineering.
        static constexpr uint32_t kStartupGraceMs = 15000;
        static constexpr uint32_t kNetConnectionTimeoutMs = 10000;
        static constexpr uint32_t kCurrentMeasurementTimeoutMs = 3000;
        static constexpr uint32_t kNoCurrentDetectionDelayMs = 20000;
        static constexpr float kExpectedRunningCurrentMinA = 0.5F;
    }

    namespace errorLed
    {
        #ifndef LED_COUNT
        #define LED_COUNT 1
        #endif
        #ifndef LED_BRIGHTNESS
        #define LED_BRIGHTNESS 40
        #endif
        static constexpr uint16_t kCount = LED_COUNT;
        static constexpr uint8_t kBrightness = LED_BRIGHTNESS;
        static constexpr uint32_t kPatternPauseMs = 1400;
        static constexpr uint32_t kPulseOnMs = 140;
        static constexpr uint32_t kPulseOffMs = 180;
    }
}
