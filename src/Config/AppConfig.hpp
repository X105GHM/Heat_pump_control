#pragma once
#include <cstdint>
#include <cstddef>

namespace config
{
    static constexpr uint32_t kSerialBaud = 115200;

    namespace netbus
    {
        static constexpr uint32_t kHeaderLowMinUs  = 7000;
        static constexpr uint32_t kHeaderLowMaxUs  = 12000;

        static constexpr uint32_t kHeaderHighMinUs = 4000;
        static constexpr uint32_t kHeaderHighMaxUs = 6500;

        static constexpr uint32_t kBitLowMinUs = 500;
        static constexpr uint32_t kBitLowMaxUs = 1800;

        static constexpr uint32_t kShortHighMinUs = 600;
        static constexpr uint32_t kShortHighMaxUs = 1700;

        static constexpr uint32_t kLongHighMinUs = 2200;
        static constexpr uint32_t kLongHighMaxUs = 4200;

        static constexpr bool kLongHighMeansOne = false;

        static constexpr uint32_t kFrameGapUs = 12000;
        static constexpr uint32_t kMaxPulseUs = 30000;

        static constexpr uint32_t kTxLowUs = 1000;

        static constexpr uint32_t kTxHighShortUs = 1000;
        static constexpr uint32_t kTxHighLongUs = 3000;
        static constexpr uint32_t kTxHighZeroUs =
            kLongHighMeansOne ? kTxHighShortUs : kTxHighLongUs;
        static constexpr uint32_t kTxHighOneUs =
            kLongHighMeansOne ? kTxHighLongUs : kTxHighShortUs;
        static constexpr uint8_t kTxCommandRepeatCount = 8U;
        static constexpr uint32_t kTxInterFrameLowUs = 1000U;
        static constexpr uint32_t kTxInterFrameHighUs = 100000U;

        static constexpr size_t kShortFrameBytes = 9;
        static constexpr size_t kLongFrameBytes  = 12;

        static constexpr size_t kMaxBitsPerFrame  = 128;
        static constexpr size_t kMaxBytesPerFrame = 16;

        static constexpr uint32_t kBitHighZeroMinUs =
            kLongHighMeansOne ? kShortHighMinUs : kLongHighMinUs;

        static constexpr uint32_t kBitHighZeroMaxUs =
            kLongHighMeansOne ? kShortHighMaxUs : kLongHighMaxUs;

        static constexpr uint32_t kBitHighOneMinUs =
            kLongHighMeansOne ? kLongHighMinUs : kShortHighMinUs;

        static constexpr uint32_t kBitHighOneMaxUs =
            kLongHighMeansOne ? kLongHighMaxUs : kShortHighMaxUs;

        static_assert(kTxHighZeroUs != kTxHighOneUs);
        static_assert(kTxCommandRepeatCount > 1U);
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
        static constexpr uint32_t kMinRunTimeMs = 180000;
        static constexpr uint32_t kMinOffTimeMs = 180000;
        static constexpr uint32_t kMinNetCommandIntervalMs = 1000;
        static constexpr uint32_t kCommandConfirmationTimeoutMs = 8000;
        static constexpr float kCompressorCurrentThresholdA = 0.7F;
        static constexpr uint32_t kStartupGraceMs = 15000;
        static constexpr uint32_t kNetConnectionTimeoutMs = 10000;
        static constexpr uint32_t kBridgeConnectionTimeoutMs = 10000;
        static constexpr uint32_t kCurrentMeasurementTimeoutMs = 3000;
        static constexpr uint32_t kNoCurrentDetectionDelayMs = 20000;
        static constexpr float kExpectedRunningCurrentMinA = 0.5F;

        static_assert(kMinRunTimeMs > 0U);
        static_assert(kMinOffTimeMs > 0U);
        static_assert(kMinNetCommandIntervalMs > 0U);
    }

    namespace errorLed
    {
        #ifndef LED_COUNT
        #define LED_COUNT 1
        #endif

        #ifndef LED_BRIGHTNESS
        #define LED_BRIGHTNESS 255
        #endif

        static constexpr uint16_t kCount = LED_COUNT;
        static constexpr uint8_t kBrightness = LED_BRIGHTNESS;

        static constexpr uint32_t kPatternPauseMs = 1400;
        static constexpr uint32_t kPulseOnMs = 140;
        static constexpr uint32_t kPulseOffMs = 180;
    }

    namespace espnow
    {
        static constexpr uint8_t kWifiChannel = 1;
        static constexpr uint32_t kStatusSendPeriodMs = 1000;
        static constexpr bool kEnableStatusSend = true;
        static constexpr bool kBroadcastStatusWhenNoPeer = true;
        static constexpr uint32_t kBridgeHeartbeatPeriodMs = 2000;
    }
}
