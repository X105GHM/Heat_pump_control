#pragma once
#include <cstdint>
#include <cstddef>

namespace config
{
    static constexpr uint32_t kSerialBaud = 115200;

    namespace netbus
    {
        static constexpr uint32_t kHeaderLowMinUs = 7000;
        static constexpr uint32_t kHeaderLowMaxUs = 12000;

        static constexpr uint32_t kHeaderHighMinUs = 4000;
        static constexpr uint32_t kHeaderHighMaxUs = 6500;

        static constexpr uint32_t kBitLowMinUs = 500;
        static constexpr uint32_t kBitLowMaxUs = 1800;

        static constexpr uint32_t kShortHighMinUs = 600;
        static constexpr uint32_t kShortHighMaxUs = 1700;

        static constexpr uint32_t kLongHighMinUs = 2200;
        static constexpr uint32_t kLongHighMaxUs = 4200;

        /*
         * WICHTIG:
         * Je nach Display/Bus/Level-Shifter kann die Bedeutung invertiert sein.
         *
         * Viele PC1001-Reverse-Engineering-Notizen:
         * HIGH ca. 1 ms = Bit 1
         * HIGH ca. 3 ms = Bit 0
         *
         * Falls deine Rohframes komisch aussehen, diese Konstante testweise ändern.
         */
        static constexpr bool kLongHighMeansOne = false;

        /*
         * Wenn während eines Frames länger nichts kommt,
         * wird das aktuelle Frame beendet.
         */
        static constexpr uint32_t kFrameIdleTimeoutMs = 25;

        /*
         * Sehr lange HIGH-Zeit bedeutet Frame-Pause.
         * Wird erkannt, falls die nächste fallende Flanke noch rechtzeitig kommt.
         */
        static constexpr uint32_t kFrameGapUs = 12000;
        static constexpr uint32_t kMaxPulseUs = 30000;

        static constexpr uint32_t kTxLowUs = 1000;
        static constexpr uint32_t kTxHighZeroUs = 1000;
        static constexpr uint32_t kTxHighOneUs = 3000;

        static constexpr size_t kMaxBitsPerFrame = 128;
        static constexpr size_t kMaxBytesPerFrame = 16;
        static constexpr size_t kPulseQueueLength = 256;
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
        static constexpr uint32_t kMinRunTimeMs = 180000; // 3 min placeholder
        static constexpr uint32_t kMinOffTimeMs = 180000; // 3 min placeholder
        static constexpr float kCompressorCurrentThresholdA = 0.7F;
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
        #define LED_BRIGHTNESS 255
        #endif

        static constexpr uint16_t kCount = LED_COUNT;
        static constexpr uint8_t kBrightness = LED_BRIGHTNESS;

        static constexpr uint32_t kPatternPauseMs = 1400;
        static constexpr uint32_t kPulseOnMs = 140;
        static constexpr uint32_t kPulseOffMs = 180;
    }
}
